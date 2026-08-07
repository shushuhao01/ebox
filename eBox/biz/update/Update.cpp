module;
#include <Windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#pragma comment(lib, "bcrypt.lib")
module biz.Update;

import std;
import MainApp;
import WinHttp;
import Coroutine;

namespace biz::update
{
	namespace
	{
		// ===== 注册表键名（与 License.cpp 共用 HKCU\Software\eBox）=====
		constexpr wchar_t kRegSubKey[] = L"Software\\eBox";
		constexpr wchar_t kLegacyRegSubKey[] = L"Software\\2Box";  // 兼容老版本
		constexpr wchar_t kRegLastCheck[] = L"LastUpdateCheck";
		constexpr wchar_t kRegIgnoredVer[] = L"IgnoredVersionCode";
		constexpr wchar_t kRegLastSeenVer[] = L"LastSeenVersionCode";

		// ===== 注册表读写工具（复用 License.cpp 模式）=====
		void writeRegDword(const wchar_t* valueName, DWORD value)
		{
			HKEY hKey = nullptr;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0,
			                    KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
			{
				RegSetValueExW(hKey, valueName, 0, REG_DWORD,
				               reinterpret_cast<const BYTE*>(&value), sizeof(value));
				RegCloseKey(hKey);
			}
		}

		DWORD readRegDword(const wchar_t* valueName, DWORD defaultValue = 0)
		{
			HKEY hKey = nullptr;
			DWORD value = defaultValue;
			// 优先读新键 Software\eBox
			if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
			{
				DWORD cb = sizeof(value);
				RegQueryValueExW(hKey, valueName, nullptr, nullptr,
				                 reinterpret_cast<LPBYTE>(&value), &cb);
				RegCloseKey(hKey);
			}
			// 兼容老版本：未命中则读旧键 Software\2Box
			if (value == defaultValue &&
			    RegOpenKeyExW(HKEY_CURRENT_USER, kLegacyRegSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
			{
				DWORD cb = sizeof(value);
				RegQueryValueExW(hKey, valueName, nullptr, nullptr,
				                 reinterpret_cast<LPBYTE>(&value), &cb);
				RegCloseKey(hKey);
			}
			return value;
		}

		void writeRegString(const wchar_t* valueName, const std::wstring& value)
		{
			HKEY hKey = nullptr;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0,
			                    KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
			{
				RegSetValueExW(hKey, valueName, 0, REG_SZ,
				               reinterpret_cast<const BYTE*>(value.c_str()),
				               static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
				RegCloseKey(hKey);
			}
		}

		// ===== URL 拆分：https://host:port/path → (host, /path, port, isHttps) =====
		struct UrlParts
		{
			std::wstring host;
			std::wstring path;
			unsigned short port{443};
			bool isHttps{true};
		};

		bool splitUrl(const std::wstring& url, UrlParts& out)
		{
			bool isHttps = false;
			size_t schemeLen = 0;
			if (url.compare(0, 8, L"https://") == 0) { isHttps = true; schemeLen = 8; }
			else if (url.compare(0, 7, L"http://") == 0) { isHttps = false; schemeLen = 7; }
			else { return false; }

			const size_t hostStart = schemeLen;
			const size_t pathStart = url.find(L'/', hostStart);
			const size_t hostEnd = (pathStart == std::wstring::npos) ? url.size() : pathStart;
			std::wstring hostPort = url.substr(hostStart, hostEnd - hostStart);

			// 拆端口
			size_t colon = hostPort.find(L':');
			if (colon != std::wstring::npos)
			{
				out.host = hostPort.substr(0, colon);
				try { out.port = static_cast<unsigned short>(std::stoul(hostPort.substr(colon + 1))); }
				catch (...) { return false; }
			}
			else
			{
				out.host = hostPort;
				out.port = isHttps ? 443 : 80;
			}
			out.path = (pathStart == std::wstring::npos) ? L"/" : url.substr(pathStart);
			out.isHttps = isHttps;
			return !out.host.empty();
		}

		// ===== UTF-8 ↔ UTF-16 转换 =====
		std::wstring utf8ToWstring(std::string_view sv)
		{
			if (sv.empty()) return {};
			int wlen = MultiByteToWideChar(CP_UTF8, 0, sv.data(),
			                               static_cast<int>(sv.size()), nullptr, 0);
			std::wstring wstr(wlen, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, sv.data(),
			                    static_cast<int>(sv.size()), wstr.data(), wlen);
			return wstr;
		}

		std::string wstringToUtf8(std::wstring_view wsv)
		{
			if (wsv.empty()) return {};
			int len = WideCharToMultiByte(CP_UTF8, 0, wsv.data(),
			                             static_cast<int>(wsv.size()),
			                             nullptr, 0, nullptr, nullptr);
			std::string str(len, '\0');
			WideCharToMultiByte(CP_UTF8, 0, wsv.data(),
			                    static_cast<int>(wsv.size()),
			                    str.data(), len, nullptr, nullptr);
			return str;
		}

		// ===== 极简 JSON 字段提取（仅适配 manifest 固定结构）=====
		bool extractJsonString(std::string_view json, std::string_view key, std::string& out)
		{
			// 查找 "key":"value"
			const std::string pattern = std::string("\"") + std::string(key) + "\":\"";
			const auto pos = json.find(pattern);
			if (pos == std::string_view::npos) return false;
			const size_t start = pos + pattern.size();
			const size_t end = json.find('"', start);
			if (end == std::string_view::npos) return false;
			out = json.substr(start, end - start);
			return true;
		}

		bool extractJsonInt(std::string_view json, std::string_view key, long long& out)
		{
			const std::string pattern = std::string("\"") + std::string(key) + "\":";
			const auto pos = json.find(pattern);
			if (pos == std::string_view::npos) return false;
			size_t i = pos + pattern.size();
			while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
			if (i >= json.size() || !std::isdigit(static_cast<unsigned char>(json[i]))) return false;
			char* endPtr = nullptr;
			out = std::strtoll(json.data() + i, &endPtr, 10);
			return true;
		}

		bool extractJsonBool(std::string_view json, std::string_view key, bool& out)
		{
			const std::string pattern = std::string("\"") + std::string(key) + "\":";
			const auto pos = json.find(pattern);
			if (pos == std::string_view::npos) return false;
			size_t i = pos + pattern.size();
			while (i < json.size() && (json[i] == ' ' || json[i] == '\t')) ++i;
			if (json.compare(i, 4, "true") == 0) { out = true; return true; }
			if (json.compare(i, 5, "false") == 0) { out = false; return true; }
			return false;
		}

		// 解析 changelog 数组："changelog": ["...", "..."]
		std::vector<std::wstring> extractJsonChangelog(std::string_view json)
		{
			std::vector<std::wstring> result;
			const auto arrKey = json.find("\"changelog\":");
			if (arrKey == std::string_view::npos) return result;
			const auto openBracket = json.find('[', arrKey);
			if (openBracket == std::string_view::npos) return result;
			const auto closeBracket = json.find(']', openBracket);
			if (closeBracket == std::string_view::npos) return result;

			size_t pos = openBracket + 1;
			while (pos < closeBracket)
			{
				const auto quoteStart = json.find('"', pos);
				if (quoteStart == std::string_view::npos || quoteStart >= closeBracket) break;
				const auto quoteEnd = json.find('"', quoteStart + 1);
				if (quoteEnd == std::string_view::npos) break;
				result.push_back(utf8ToWstring(json.substr(quoteStart + 1, quoteEnd - quoteStart - 1)));
				pos = quoteEnd + 1;
			}
			return result;
		}

		// 解析整个 manifest JSON
		bool parseManifest(std::string_view json, UpdateManifest& out)
		{
			std::string tmpStr;
			long long tmpInt = 0;
			bool tmpBool = false;

			if (!extractJsonString(json, "latestVersion", tmpStr)) return false;
			out.latestVersion = utf8ToWstring(tmpStr);

			if (!extractJsonInt(json, "latestVersionCode", tmpInt)) return false;
			out.latestVersionCode = static_cast<int>(tmpInt);

			if (extractJsonString(json, "releaseDate", tmpStr))
				out.releaseDate = utf8ToWstring(tmpStr);
			if (extractJsonString(json, "downloadUrl", tmpStr))
				out.downloadUrl = utf8ToWstring(tmpStr);
			if (extractJsonString(json, "downloadSha256", tmpStr))
				out.downloadSha256 = utf8ToWstring(tmpStr);
			if (extractJsonInt(json, "downloadSize", tmpInt))
				out.downloadSize = static_cast<std::uint64_t>(tmpInt);
			extractJsonBool(json, "forceUpdate", tmpBool);
			out.forceUpdate = tmpBool;
			if (extractJsonInt(json, "minSkipVersionCode", tmpInt))
				out.minSkipVersionCode = static_cast<int>(tmpInt);
			out.changelog = extractJsonChangelog(json);
			return true;
		}

		// ===== SHA-256 文件校验（BCrypt 增量哈希，避免大文件全量加载）=====
		bool computeFileSha256(const std::wstring& filePath, std::wstring& outHex)
		{
			constexpr DWORD kChunkSize = 64 * 1024;  // 64KB 分块读取

			std::ifstream f(filePath, std::ios::binary);
			if (!f) return false;

			BCRYPT_ALG_HANDLE hAlg = nullptr;
			if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
				return false;

			// 查询哈希对象长度
			DWORD cbHashObject = 0;
			DWORD cbData = 0;
			if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
			                     reinterpret_cast<PUCHAR>(&cbHashObject),
			                     sizeof(cbHashObject), &cbData, 0) != 0)
			{
				BCryptCloseAlgorithmProvider(hAlg, 0);
				return false;
			}

			// 查询哈希值长度
			DWORD cbHash = 0;
			if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
			                     reinterpret_cast<PUCHAR>(&cbHash),
			                     sizeof(cbHash), &cbData, 0) != 0)
			{
				BCryptCloseAlgorithmProvider(hAlg, 0);
				return false;
			}

			// 分配哈希对象缓冲区
			std::vector<BYTE> hashObject(cbHashObject);
			std::vector<BYTE> hashValue(cbHash);

			BCRYPT_HASH_HANDLE hHash = nullptr;
			if (BCryptCreateHash(hAlg, &hHash, hashObject.data(),
			                     static_cast<ULONG>(hashObject.size()),
			                     nullptr, 0, 0) != 0)
			{
				BCryptCloseAlgorithmProvider(hAlg, 0);
				return false;
			}

			// 分块读取并增量哈希
			std::vector<BYTE> chunk(kChunkSize);
			bool ok = true;
			while (ok)
			{
				f.read(reinterpret_cast<char*>(chunk.data()), kChunkSize);
				const std::streamsize bytesRead = f.gcount();
				if (bytesRead == 0) break;
				if (BCryptHashData(hHash, chunk.data(),
				                   static_cast<ULONG>(bytesRead), 0) != 0)
				{
					ok = false;
					break;
				}
			}
			if (ok)
			{
				if (BCryptFinishHash(hHash, hashValue.data(),
				                     static_cast<ULONG>(hashValue.size()), 0) != 0)
				{
					ok = false;
				}
			}

			BCryptDestroyHash(hHash);
			BCryptCloseAlgorithmProvider(hAlg, 0);

			if (!ok) return false;

			// 转小写十六进制字符串
			static const wchar_t kHexTable[] = L"0123456789abcdef";
			outHex.clear();
			outHex.reserve(hashValue.size() * 2);
			for (BYTE b : hashValue)
			{
				outHex.push_back(kHexTable[b >> 4]);
				outHex.push_back(kHexTable[b & 0x0F]);
			}
			return true;
		}
	}

	// ===== 检查更新 =====
	coro::LazyTask<CheckOutcome> checkUpdateAsync()
	{
		CheckOutcome outcome;
		outcome.result = CheckResult::NetworkError;

		// 解析 manifest URL
		UrlParts urlParts;
		if (!splitUrl(std::wstring{MainApp::kUpdateManifestUrl}, urlParts))
		{
			co_return outcome;
		}

		// 添加时间戳查询参数破除 CDN 缓存
		// 精确到小时：每小时内复用 CDN 缓存（省流量），1 小时后自动拉最新 manifest
		// jsDelivr 默认缓存约 12 小时，加时间戳后最长 1 小时即可拿到新版本
		// 配合发布后手动 purge（访问 https://purge.jsdelivr.net/...）可做到分钟级生效
		SYSTEMTIME nowCacheBuster{};
		GetLocalTime(&nowCacheBuster);
		const std::wstring cacheBuster = std::format(L"t={:04}{:02}{:02}{:02}",
		                                             nowCacheBuster.wYear, nowCacheBuster.wMonth,
		                                             nowCacheBuster.wDay, nowCacheBuster.wHour);
		std::wstring pathWithCb = urlParts.path;
		if (pathWithCb.find(L'?') != std::wstring::npos)
			pathWithCb += L"&" + cacheBuster;
		else
			pathWithCb += L"?" + cacheBuster;

		try
		{
			auto session = ms::get_default_win_http_session();
			auto conn = session->createConnection(urlParts.host, urlParts.port);
			DWORD flags = WINHTTP_FLAG_REFRESH;
			if (urlParts.isHttps) flags |= WINHTTP_FLAG_SECURE;
			auto req = conn->openRequest(L"GET", pathWithCb, L"HTTP/1.1",
			                             MainApp::kUpdateUserAgent, {}, flags);

			std::vector<std::byte> response = co_await req->request({}, {});

			// 解析 JSON
			std::string jsonText;
			if (!response.empty())
			{
				jsonText.assign(reinterpret_cast<const char*>(response.data()), response.size());
			}
			UpdateManifest manifest;
			if (!parseManifest(jsonText, manifest))
			{
				co_return outcome;  // NetworkError
			}

			// 记录上次检查时间与发现版本
			SYSTEMTIME st{};
			GetLocalTime(&st);
			const std::wstring isoTime = std::format(L"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}",
			                                          st.wYear, st.wMonth, st.wDay,
			                                          st.wHour, st.wMinute, st.wSecond);
			writeRegString(kRegLastCheck, isoTime);
			writeRegDword(kRegLastSeenVer, static_cast<DWORD>(manifest.latestVersionCode));

			// 版本比较：latestVersionCode > 本地 kVerCode 才算有更新
			if (manifest.latestVersionCode <= MainApp::kVerCode)
			{
				outcome.result = CheckResult::NoUpdate;
				co_return outcome;
			}

			// 查忽略状态：若用户曾忽略此版本且非强制，返回 SkippedThisVersion
			const DWORD ignored = readRegDword(kRegIgnoredVer, 0);
			if (!manifest.forceUpdate &&
			    static_cast<int>(ignored) == manifest.latestVersionCode)
			{
				outcome.result = CheckResult::SkippedThisVersion;
				co_return outcome;
			}

			outcome.result = CheckResult::HasUpdate;
			outcome.manifest = std::move(manifest);
		}
		catch (...)
		{
			// 保持 NetworkError
		}

		co_return outcome;
	}

	// ===== 下载 + SHA-256 校验 =====
	coro::LazyTask<DownloadOutcome> downloadAndVerifyAsync(
		const UpdateManifest& manifest,
		std::function<void(const DownloadProgress&)> progressCb)
	{
		DownloadOutcome outcome;
		outcome.result = DownloadResult::NetworkError;

		// 解析下载 URL
		UrlParts urlParts;
		if (!splitUrl(manifest.downloadUrl, urlParts))
		{
			outcome.errorMessage = L"invalid download url";
			co_return outcome;
		}

		// 检查取消
		std::stop_token token = co_await coro::get_current_cancellation_token();
		if (token.stop_requested())
		{
			outcome.result = DownloadResult::Cancelled;
			co_return outcome;
		}

		std::vector<std::byte> data;
		try
		{
			auto session = ms::get_default_win_http_session();
			auto conn = session->createConnection(urlParts.host, urlParts.port);
			DWORD flags = WINHTTP_FLAG_REFRESH;
			if (urlParts.isHttps) flags |= WINHTTP_FLAG_SECURE;
			auto req = conn->openRequest(L"GET", urlParts.path, L"HTTP/1.1",
			                             MainApp::kUpdateUserAgent, {}, flags);

			// 共享状态：累计下载字节数（move_only_function 不能拷贝，用 shared_ptr 传递）
			auto downloaded = std::make_shared<std::atomic<std::uint64_t>>(0);
			auto total = std::make_shared<std::atomic<std::uint64_t>>(manifest.downloadSize);
			auto userCb = std::move(progressCb);

			auto contentLenCb = [total](std::uint64_t len)
			{
				if (len > 0) total->store(len, std::memory_order_relaxed);
			};
			auto progressReporter = [downloaded, total, userCb](std::uint64_t chunk) mutable
			{
				const std::uint64_t d = downloaded->fetch_add(chunk, std::memory_order_relaxed) + chunk;
				if (userCb)
				{
					userCb({d, total->load(std::memory_order_relaxed)});
				}
			};

			// 启动下载（WinHttp 内部已挂接 stop_token 取消）
			data = co_await req->request(contentLenCb, progressReporter);
		}
		catch (const std::exception& e)
		{
			outcome.errorMessage = utf8ToWstring(e.what());
			co_return outcome;
		}
		catch (...)
		{
			outcome.errorMessage = L"unknown download error";
			co_return outcome;
		}

		// 下载完成后再次检查取消
		if (token.stop_requested())
		{
			outcome.result = DownloadResult::Cancelled;
			co_return outcome;
		}

		// 写入临时文件
		const std::wstring tempPath = getTempDownloadPath(manifest.latestVersionCode);
		try
		{
			std::ofstream f(tempPath, std::ios::binary | std::ios::trunc);
			if (!f)
			{
				outcome.result = DownloadResult::DiskError;
				outcome.errorMessage = L"cannot open temp file for writing";
				co_return outcome;
			}
			f.write(reinterpret_cast<const char*>(data.data()),
			        static_cast<std::streamsize>(data.size()));
			if (!f)
			{
				outcome.result = DownloadResult::DiskError;
				outcome.errorMessage = L"write temp file failed";
				co_return outcome;
			}
			f.close();
		}
		catch (const std::exception& e)
		{
			outcome.result = DownloadResult::DiskError;
			outcome.errorMessage = utf8ToWstring(e.what());
			co_return outcome;
		}
		catch (...)
		{
			outcome.result = DownloadResult::DiskError;
			outcome.errorMessage = L"unknown disk error";
			co_return outcome;
		}

		// SHA-256 校验
		if (!manifest.downloadSha256.empty())
		{
			std::wstring actualSha256;
			if (!computeFileSha256(tempPath, actualSha256))
			{
				outcome.result = DownloadResult::VerifyFailed;
				outcome.errorMessage = L"failed to compute sha256";
				DeleteFileW(tempPath.c_str());
				co_return outcome;
			}
			// 大小写不敏感比较
			if (_wcsicmp(actualSha256.c_str(), manifest.downloadSha256.c_str()) != 0)
			{
				outcome.result = DownloadResult::VerifyFailed;
				outcome.errorMessage = L"sha256 mismatch: expected=" + manifest.downloadSha256 +
				                       L", actual=" + actualSha256;
				DeleteFileW(tempPath.c_str());
				co_return outcome;
			}
		}

		outcome.result = DownloadResult::Success;
		outcome.filePath = tempPath;
		co_return outcome;
	}

	// ===== 同步辅助接口 =====

	void ignoreVersion(int versionCode)
	{
		writeRegDword(kRegIgnoredVer, static_cast<DWORD>(versionCode));
	}

	std::wstring getTempDownloadPath(int versionCode)
	{
		wchar_t tempDir[MAX_PATH]{};
		GetTempPathW(MAX_PATH, tempDir);
		return std::wstring{tempDir} + L"eBox_update_v" +
		       std::to_wstring(versionCode) + L".exe";
	}

	bool applyUpdate(const std::wstring& downloadedExePath)
	{
		namespace fs = std::filesystem;
		const std::wstring exePath{app().exeFullName()};
		const std::wstring exeDir{app().exeDir()};
		const std::wstring bakPath = exePath + L".bak";
		const std::wstring batPath = exeDir + L"\\eBox_updater.bat";
		const DWORD currentPid = GetCurrentProcessId();

		// 校验下载文件存在
		std::error_code ec;
		if (!fs::exists(downloadedExePath, ec) || ec)
		{
			return false;
		}

		// 备份当前 exe（覆盖前备份，便于回滚）
		fs::copy_file(exePath, bakPath, fs::copy_options::overwrite_existing, ec);
		if (ec) return false;

		// 生成批处理脚本（UTF-8 无 BOM，cmd.exe 在 chcp 65001 下可执行）
		// 流程：等待主进程退出 → 覆盖 exe → 启动新版本 → 清理临时文件 → 自删除
		// 注意：std::wofstream 在 MSVC 默认写 UTF-16，cmd.exe 无法执行，必须用 UTF-8
		std::wstring batContent;
		batContent.reserve(2048);
		batContent += L"@echo off\r\n";
		batContent += L"chcp 65001 >nul\r\n";
		batContent += L"setlocal enabledelayedexpansion\r\n";
		batContent += L"\r\n";
		batContent += L":: wait for main process exit (max 30s)\r\n";
		batContent += L"set /a wait_count=0\r\n";
		batContent += L":wait\r\n";
		batContent += L"tasklist /FI \"PID eq " + std::to_wstring(currentPid) + L"\" 2>nul | find \""
		             + std::to_wstring(currentPid) + L"\" >nul\r\n";
		batContent += L"if not errorlevel 1 (\r\n";
		batContent += L"  set /a wait_count+=1\r\n";
		batContent += L"  if !wait_count! geq 30 (\r\n";
		batContent += L"    goto abort\r\n";
		batContent += L"  )\r\n";
		batContent += L"  timeout /t 1 /nobreak >nul\r\n";
		batContent += L"  goto wait\r\n";
		batContent += L")\r\n";
		batContent += L"\r\n";
		batContent += L":: overwrite exe\r\n";
		batContent += L"copy /Y \"" + downloadedExePath + L"\" \"" + exePath + L"\"\r\n";
		batContent += L"if errorlevel 1 (\r\n";
		batContent += L"  :: restore backup on failure\r\n";
		batContent += L"  copy /Y \"" + bakPath + L"\" \"" + exePath + L"\" >nul 2>&1\r\n";
		batContent += L"  start \"\" \"" + exePath + L"\"\r\n";
		batContent += L"  goto cleanup\r\n";
		batContent += L")\r\n";
		batContent += L"\r\n";
		batContent += L":: launch new version\r\n";
		batContent += L"start \"\" \"" + exePath + L"\"\r\n";
		batContent += L"\r\n";
		batContent += L":cleanup\r\n";
		batContent += L":: cleanup temp files\r\n";
		batContent += L"del /f /q \"" + downloadedExePath + L"\" >nul 2>&1\r\n";
		batContent += L"del /f /q \"" + batPath + L"\" >nul 2>&1\r\n";
		batContent += L"exit /b 0\r\n";
		batContent += L"\r\n";
		batContent += L":abort\r\n";
		batContent += L":: timeout: restore backup\r\n";
		batContent += L"copy /Y \"" + bakPath + L"\" \"" + exePath + L"\" >nul 2>&1\r\n";
		batContent += L"start \"\" \"" + exePath + L"\"\r\n";
		batContent += L"del /f /q \"" + batPath + L"\" >nul 2>&1\r\n";
		batContent += L"exit /b 1\r\n";

		// 转为 UTF-8 并写入文件
		const std::string utf8Content = wstringToUtf8(batContent);
		{
			std::ofstream bat(batPath, std::ios::binary | std::ios::trunc);
			if (!bat)
			{
				return false;
			}
			bat.write(utf8Content.data(), static_cast<std::streamsize>(utf8Content.size()));
			if (!bat)
			{
				return false;
			}
		}

		// 启动批处理（隐藏窗口）
		STARTUPINFOW si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		PROCESS_INFORMATION pi{};
		const std::wstring cmd = L"cmd.exe /c \"" + batPath + L"\"";
		std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
		cmdBuf.push_back(L'\0');

		if (!CreateProcessW(nullptr, cmdBuf.data(),
		                    nullptr, nullptr, FALSE,
		                    CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		{
			return false;
		}
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}

	void cleanupBackupIfNeeded()
	{
		namespace fs = std::filesystem;
		const std::wstring bakPath{std::wstring{app().exeFullName()} + L".bak"};
		std::error_code ec;
		if (fs::exists(bakPath, ec) && !ec)
		{
			fs::remove(bakPath, ec);
		}
	}
}
