module;
#include <windows.h>
#include <tlhelp32.h>
// SHGetKnownFolderPath：定位真实的 %APPDATA%，用于首次复用宿主机已有的浏览器内核
#include <shlobj.h>
#include <objbase.h>

module Launcher;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import std;
import MainApp;
import EssentialData;
import Utility.SystemInfo;
import Biz.Core;
import biz.License;

namespace
{
	// 判断目标 exe 是否为 32 位 PE（IMAGE_FILE_MACHINE_I386）。
	// 返回 nullopt 表示无法判定（文件不存在/头损坏），调用方回退原有启动路径。
	std::optional<bool> is_32bit_pe(std::wstring_view exePath)
	{
		const std::wstring pathText{exePath};
		const HANDLE hFile = CreateFileW(pathText.c_str(), GENERIC_READ,
		                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		                                 nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			return std::nullopt;
		}
		struct FileCloser
		{
			HANDLE handle;

			~FileCloser() { CloseHandle(handle); }
		} closer{hFile};

		DWORD bytesRead = 0;
		IMAGE_DOS_HEADER dosHeader{};
		if (!ReadFile(hFile, &dosHeader, sizeof(dosHeader), &bytesRead, nullptr) ||
		    bytesRead != sizeof(dosHeader) || dosHeader.e_magic != IMAGE_DOS_SIGNATURE || dosHeader.e_lfanew <= 0)
		{
			return std::nullopt;
		}
		if (SetFilePointer(hFile, dosHeader.e_lfanew, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
		{
			return std::nullopt;
		}
		DWORD signature = 0;
		if (!ReadFile(hFile, &signature, sizeof(signature), &bytesRead, nullptr) ||
		    bytesRead != sizeof(signature) || signature != IMAGE_NT_SIGNATURE)
		{
			return std::nullopt;
		}
		IMAGE_FILE_HEADER fileHeader{};
		if (!ReadFile(hFile, &fileHeader, sizeof(fileHeader), &bytesRead, nullptr) ||
		    bytesRead != sizeof(fileHeader))
		{
			return std::nullopt;
		}
		switch (fileHeader.Machine)
		{
		case IMAGE_FILE_MACHINE_I386:
			return true;
		case IMAGE_FILE_MACHINE_AMD64:
			return false;
		default:
			return std::nullopt;
		}
	}

	std::wstring ansi_to_wide(std::string_view text)
	{
		if (text.empty())
		{
			return {};
		}
		const int len = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		if (len <= 0)
		{
			return {};
		}
		std::wstring result(static_cast<std::size_t>(len), L'\0');
		MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), result.data(), len);
		return result;
	}

	std::string wide_to_utf8(std::wstring_view text)
	{
		if (text.empty())
		{
			return {};
		}
		const int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		if (len <= 0)
		{
			return {};
		}
		std::string result(static_cast<std::size_t>(len), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), len, nullptr, nullptr);
		return result;
	}

	// 异常文本（std::exception::what()）统一按 UTF-8 处理，转成宽串写日志/弹窗
	std::wstring utf8_to_wide(std::string_view text)
	{
		if (text.empty())
		{
			return {};
		}
		const int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
		if (len <= 0)
		{
			return {};
		}
		std::wstring result(static_cast<std::size_t>(len), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), len);
		return result;
	}

	// 启动前预检环境数据目录（影子目录）：缺失则自动创建，用写探针验证可写，并检查剩余磁盘空间。
	// 目录被清理/磁盘写满/权限或杀软拦截时，被启动的应用往往在初始化阶段直接崩溃
	// （例如企业微信弹「Duilib 加载资源文件失败」后退出），这里提前拦截并返回可读原因，
	// 让问题以 eBox 的提示暴露。返回空表示通过。
	std::optional<std::wstring> precheck_env_data_dir(const biz::Env* env)
	{
		namespace fs = std::filesystem;
		const fs::path envDir{fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", env->getIndex())}};
		std::error_code ec;
		fs::create_directories(envDir, ec);
		if (!fs::is_directory(envDir, ec))
		{
			return std::format(L"环境数据目录不可用：{}（错误码 {}）", envDir.native(), ec.value());
		}
		// 写探针：目录可能被只读属性/权限/杀软锁定，导致应用写缓存和资源时失败
		const fs::path probePath{envDir / L".ebox_write_probe"};
		{
			std::ofstream probe{probePath, std::ios::binary | std::ios::trunc};
			probe << "probe";
			probe.close();
			if (!probe)
			{
				return std::format(L"环境数据目录不可写：{}\n可能是权限不足或杀毒软件拦截，请检查后重试。", envDir.native());
			}
		}
		fs::remove(probePath, ec);
		// 剩余空间不足时应用解压/写缓存极易失败
		ULARGE_INTEGER freeBytes{};
		if (GetDiskFreeSpaceExW(envDir.c_str(), &freeBytes, nullptr, nullptr) &&
		    freeBytes.QuadPart < 256ull * 1024 * 1024)
		{
			return std::format(L"磁盘剩余空间不足 256MB，请清理磁盘后再启动。\n环境目录：{}", envDir.native());
		}
		return std::nullopt;
	}

	// job 文件字段格式：uint32 字符数 + 该数量的 wchar_t（UTF-16LE），与 eBoxInject32 侧一致
	void append_job_field(std::vector<char>& blob, std::wstring_view text)
	{
		const auto charCount = static_cast<std::uint32_t>(text.size());
		const auto* countBytes = reinterpret_cast<const char*>(&charCount);
		blob.insert(blob.end(), countBytes, countBytes + sizeof(charCount));
		const auto* textBytes = reinterpret_cast<const char*>(text.data());
		blob.insert(blob.end(), textBytes, textBytes + text.size() * sizeof(wchar_t));
	}

	// 首次复用宿主机已有的浏览器内核（比特浏览器 BitBrowser）：
	// 内核下载后由子进程 7za 解压，解压落盘走的是真实 %APPDATA%\BitBrowser\Chrome-bin，
	// 而客户端在环境进程内用 existsSync 检查的是被重定向后的环境内路径，
	// 首次启动必然报 "Browser was not found at the configured executablePath"。
	// 这里在启动前把真实目录里已存在的内核复制进环境目录，消除首次失败；
	// 环境内已有内核（非空）时直接跳过，不重复拷贝。
	void reuse_host_bitbrowser_kernel(const biz::Env* env)
	{
		namespace fs = std::filesystem;
		try
		{
			PWSTR roaming = nullptr;
			if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming) != S_OK || !roaming)
			{
				return;
			}
			const fs::path roamingPath{roaming};
			CoTaskMemFree(roaming);
			// 去掉盘符得到重定向相对路径（与 MemoryDll 的重定向规则一致）
			const fs::path rel = roamingPath.relative_path();
			if (rel.empty())
			{
				return;
			}
			const fs::path src = roamingPath / fs::path{L"BitBrowser"} / fs::path{L"Chrome-bin"};
			std::error_code ec;
			if (!fs::is_directory(src, ec) || ec)
			{
				return;
			}
			const fs::path dst = fs::path{app().envDataRoot()} / fs::path{L"Env"} /
			                     fs::path{std::format(L"{}", env->getIndex())} / rel /
			                     fs::path{L"BitBrowser"} / fs::path{L"Chrome-bin"};
			if (fs::is_directory(dst, ec) && !ec)
			{
				std::error_code emptyEc;
				if (!fs::is_empty(dst, emptyEc) && !emptyEc)
				{
					return;
				}
			}
			fs::create_directories(dst, ec);
			std::error_code copyEc;
			fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::skip_existing, copyEc);
			if (copyEc)
			{
				// 半成品会让客户端"看起来有内核"却启动失败，清掉以便下次启动重试
				std::error_code removeEc;
				fs::remove_all(dst, removeEc);
				biz::env_logger().append(env->getIndex(), biz::EnvLogType::Error, biz::EnvLogStatus::Fail,
				                         L"复用宿主机浏览器内核失败",
				                         std::format(L"{} → {}（错误码 {}）", src.native(), dst.native(), copyEc.value()));
				return;
			}
			biz::env_logger().append(env->getIndex(), biz::EnvLogType::Info, biz::EnvLogStatus::Info,
			                         L"复用宿主机浏览器内核",
			                         std::format(L"已复制 {} → {}", src.native(), dst.native()));
		}
		catch (...)
		{
		}
	}

	std::wstring read_injector_log(const std::filesystem::path& logPath)
	{
		std::ifstream logFile{logPath, std::ios::binary};
		if (!logFile)
		{
			return {};
		}
		logFile.seekg(0, std::ios::end);
		const auto byteCount = static_cast<std::size_t>(logFile.tellg());
		logFile.seekg(0, std::ios::beg);
		std::wstring text(byteCount / sizeof(wchar_t), L'\0');
		if (!text.empty())
		{
			logFile.read(reinterpret_cast<char*>(text.data()),
			             static_cast<std::streamsize>(text.size() * sizeof(wchar_t)));
		}
		return text;
	}

	// 由 32 位注入器（eBoxInject32.exe）直接创建目标进程并注入。
	// 注入器与目标同位数，Detours 可就地注入，不会退化为 SysWOW64\rundll32.exe helper
	// （该 helper 在部分安全软件环境下被拦截，导致目标进程被 TerminateProcess）。
	// 启动参数经 job 文件传递，避免参数中的引号/换行破坏命令行解析。
	PROCESS_INFORMATION create_and_inject_by_helper32(const biz::Env* env, std::wstring_view exePath, std::wstring_view params)
	{
		namespace fs = std::filesystem;
		const fs::path injectorPath{env->ensureInjector32InDeviceAndReturnPath()};
		const fs::path jobPath = injectorPath.parent_path() /
			std::format(L"{}_{}.job", env->getFlagName(), GetCurrentProcessId());
		const fs::path logPath{jobPath.native() + L".log"};

		// Env 侧返回的是窄串（Detours 的 lpDllName 就是 ANSI），job 文件按宽串传递
		const std::wstring dllPath = ansi_to_wide(env->ensureDllInDeviceAndReturnPath(ArchBit::Bit32));
		const SystemVersionInfo& version = biz::get_core_data().version;
		const DWORD versionBits = (version.isWindows8Point1OrGreater ? VERSION_BIT_WIN8_1 : 0) |
			(version.isWindows8OrGreater ? VERSION_BIT_WIN8 : 0) |
			(version.isWindowsVistaOrGreater ? VERSION_BIT_VISTA : 0) |
			(version.is32BitSystem ? VERSION_BIT_32BIT_SYSTEM : 0);

		std::vector<char> blob;
		const wchar_t magic[4] = {L'E', L'B', L'I', L'1'};
		blob.insert(blob.end(), reinterpret_cast<const char*>(magic),
		            reinterpret_cast<const char*>(magic) + sizeof(magic));
		append_job_field(blob, dllPath);
		append_job_field(blob, exePath);
		append_job_field(blob, params);
		append_job_field(blob, fs::path{exePath}.parent_path().native());
		append_job_field(blob, app().envDataRoot());
		append_job_field(blob, std::format(L"{:x}", env->getFlag()));
		append_job_field(blob, std::format(L"{:x}", env->getIndex()));
		append_job_field(blob, std::format(L"{:x}", versionBits));

		{
			std::ofstream jobFile{jobPath, std::ios::binary | std::ios::trunc};
			jobFile.write(blob.data(), static_cast<std::streamsize>(blob.size()));
			jobFile.close();
			if (!jobFile)
			{
				fs::remove(jobPath);
				throw std::runtime_error("write eBoxInject32 job file failed");
			}
		}

		STARTUPINFOW startupInfo{sizeof(startupInfo)};
		PROCESS_INFORMATION procInfo{};
		std::wstring cmdLine = std::format(L"\"{}\" \"{}\"", injectorPath.native(), jobPath.native());
		if (!CreateProcessW(injectorPath.c_str(), cmdLine.data(), nullptr, nullptr, FALSE,
		                    CREATE_NO_WINDOW, nullptr, injectorPath.parent_path().c_str(), &startupInfo, &procInfo))
		{
			const DWORD err = GetLastError();
			fs::remove(jobPath);
			throw std::runtime_error(std::format("create eBoxInject32 process failed, error code: {}", err));
		}

		if (WaitForSingleObject(procInfo.hProcess, 15000) != WAIT_OBJECT_0)
		{
			// 注入器卡住：强制结束，按失败处理（其内部已在超时前写 log）
			TerminateProcess(procInfo.hProcess, ~0u);
		}
		DWORD exitCode = 0;
		GetExitCodeProcess(procInfo.hProcess, &exitCode);
		CloseHandle(procInfo.hThread);
		CloseHandle(procInfo.hProcess);

		const std::wstring detail = exitCode == 0 ? std::wstring{} : read_injector_log(logPath);
		fs::remove(jobPath);
		fs::remove(logPath);

		if (exitCode != 0)
		{
			// 注入器把失败原因（错误码/目标路径）写在 <job>.log，转写环境日志便于排查
			biz::env_logger().append(env->getIndex(), biz::EnvLogType::Error, biz::EnvLogStatus::Fail,
			                         L"32 位注入器启动失败",
			                         detail.empty() ? std::format(L"eBoxInject32 退出码 {}", exitCode) : detail);
			throw std::runtime_error(std::format("eBoxInject32 failed, exit code: {}", exitCode));
		}
		// 目标进程已由注入器 Resume，返回空 PROCESS_INFORMATION，调用方不应再 ResumeThread
		return PROCESS_INFORMATION{};
	}

	PROCESS_INFORMATION create_and_inject(const biz::Env* env, std::wstring_view exePath, std::wstring_view params)
	{
		// 64 位 eBox 启动 32 位目标时，Detours 无法就地注入（会退化为 rundll32 helper，
		// 在部分安全软件环境下被拦截），改由 32 位注入器与目标同位数直接创建并注入。
		if constexpr (IS_CURRENT_ARCH_64_BIT)
		{
			if (const std::optional<bool> is32 = is_32bit_pe(exePath); is32 && *is32)
			{
				return create_and_inject_by_helper32(env, exePath, params);
			}
		}

		PROCESS_INFORMATION procInfo = {nullptr};
		STARTUPINFOW startupInfo = {sizeof(startupInfo)};
		startupInfo.dwFlags = STARTF_USESHOWWINDOW;
		startupInfo.wShowWindow = SW_HIDE;

		namespace fs = std::filesystem;
		const fs::path cmdPath{fs::weakly_canonical(fs::path{sys_info::get_system_dir()} / fs::path{L"cmd.exe"})};
		std::wstring cmdLine = params.empty() ? std::format(LR"(/c start "" "{}")", exePath) : std::format(LR"(/c start "" "{}" {})", exePath, params);
		if (!DetourCreateProcessWithDllExW(cmdPath.c_str(), cmdLine.data(), nullptr, nullptr, 0,
		                                   CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED, nullptr,
		                                   std::filesystem::path{exePath}.parent_path().native().c_str(), &startupInfo, &procInfo,
		                                   env->ensureDllInDeviceAndReturnPath().c_str(), &::CreateProcessW))
		{
			throw std::runtime_error(std::format("CreateProcessW Failed, error code: {}", GetLastError()));
		}
		try
		{
			const std::wstring rootPath = app().envDataRoot();
			const std::uint32_t rootPathCount = static_cast<std::uint32_t>(rootPath.length());
			const std::uint32_t rootPathSize = rootPathCount * sizeof(wchar_t);
			const std::uint32_t paramsSize = sizeof(DetourInjectParams) + rootPathSize;
			std::vector<std::byte> buffer(paramsSize);
			DetourInjectParams* injectParams = reinterpret_cast<DetourInjectParams*>(buffer.data());
			injectParams->version = biz::get_core_data().version;
			injectParams->envFlag = env->getFlag();
			injectParams->envIndex = env->getIndex();
			injectParams->rootPathCount = rootPathCount;
			memcpy(injectParams->rootPath, rootPath.data(), rootPathSize);
			if (!DetourCopyPayloadToProcess(procInfo.hProcess, DETOUR_INJECT_PARAMS_GUID, injectParams, paramsSize))
			{
				throw std::runtime_error(std::format("copy payload failed, error code: {}", GetLastError()));
			}
		}
		catch (...)
		{
			TerminateProcess(procInfo.hProcess, 0);
			CloseHandle(procInfo.hThread);
			CloseHandle(procInfo.hProcess);
			throw;
		}
		return procInfo;
	}
}

namespace biz
{
	void Launcher::run(const std::shared_ptr<Env>& env, std::wstring_view exePath, std::wstring_view params /*= L""*/)
	{
		// UI 入口：启动成功后附带轮询兜底（慢电脑/注入失败时提示并可重试）
		if (m_bUiLaunchGuard.exchange(true, std::memory_order_acq_rel))
		{
			// 上一条启动流水线尚未走完（排队/创建环境/注入/resume 阶段），忽略本次连点，避免启动风暴
			return;
		}
		m_asyncScope.spawn(launchWithPoll(env, std::wstring{exePath}, std::wstring{params}));
	}

	void Launcher::runInNewEnv(std::wstring_view exePath, std::wstring_view params /*= L""*/)
	{
		if (m_bUiLaunchGuard.exchange(true, std::memory_order_acq_rel))
		{
			// 上一条启动流水线尚未走完（排队/创建环境/注入/resume 阶段），忽略本次连点，避免启动风暴
			return;
		}
		m_asyncScope.spawn(launchWithPoll(std::shared_ptr<Env>{}, std::wstring{exePath}, std::wstring{params}));
	}

	coro::LazyTask<void> Launcher::acquireLaunchSlot()
	{
		// 排队与等待都发生在独立执行器 m_execCtx 上，不阻塞 UI 线程
		co_await sched::transfer_to(m_execCtx);
		// 串行化：同一时刻只允许一条流水线创建子进程
		while (m_bLaunching.exchange(true, std::memory_order_acq_rel))
		{
			co_await sched::transfer_after(std::chrono::milliseconds{50}, m_execCtx);
		}
		// 错峰：与上一次启动起点保持最小间隔，让前一个实例先完成自身初始化
		if (m_lastLaunchStart.time_since_epoch().count() != 0)
		{
			const auto nextStart = m_lastLaunchStart + kLaunchSpacing;
			const auto now = std::chrono::steady_clock::now();
			if (now < nextStart)
			{
				co_await sched::transfer_after(nextStart - now, m_execCtx);
			}
		}
		m_lastLaunchStart = std::chrono::steady_clock::now();
		co_return;
	}

	void Launcher::releaseLaunchSlot()
	{
		m_bLaunching.store(false, std::memory_order_release);
	}

	coro::LazyTask<void> Launcher::coRun(std::shared_ptr<Env> env, std::wstring_view exePath, std::wstring_view params)
	{
		// RPC/CLI 批量入口同样走启动槽位：十几个实例排队错峰拉起，而不是同时初始化
		co_await acquireLaunchSlot();
		coro::SharedTask<void> sharedTask = coro::start_and_shared(launchInternal(env, std::wstring{exePath}, std::wstring{params}));
		m_asyncScope.spawn(sharedTask);
		try
		{
			co_await sharedTask;
		}
		catch (...)
		{
			releaseLaunchSlot();
			throw;
		}
		releaseLaunchSlot();
		co_return;
	}

	coro::LazyTask<void> Launcher::launch(const std::shared_ptr<Env>& env, std::wstring_view exePath, std::wstring_view params) const
	{
		try
		{
			co_await launchInternal(env, std::wstring{exePath}, std::wstring{params});
		}
		catch (const std::exception& e)
		{
			show_utf8_error_message(std::format("启动进程失败：{}", e.what()));
		}
		catch (...)
		{
			show_error_message(L"启动进程失败：发生未知错误");
		}
		co_return;
	}

	coro::LazyTask<void> Launcher::launchInternal(std::shared_ptr<Env> env, std::wstring exePath, std::wstring params) const
	{
		co_await sched::transfer_to(m_execCtx);

		// ===== 授权检查：到期后禁止启动环境 / 新建环境 =====
		if (!biz::license::canLaunch())
		{
			show_error_message(L"授权已到期，无法启动环境。请联系作者续期激活码。");
			co_return;
		}

		if (!env)
		{
			env = env_mgr().createEnv();
			// 新建环境
			env_logger().append(env->getIndex(), EnvLogType::Info, EnvLogStatus::Info,
			                    L"新建环境", std::format(L"环境{} 已创建", env->getIndex()));
		}
		// 记住该程序使用的环境，下次启动时优先复用（登录态/数据随环境持久化）
		env_mgr().setLastEnvForProc(exePath, env);
		// 首次启动的应用路径绑定为该环境的“启动”按钮入口（后续启动同一环境时固定使用它）
		if (env->getAppPath().empty())
		{
			env_mgr().setEnvAppPath(env, exePath);
		}
		env_logger().append(env->getIndex(), EnvLogType::Process, EnvLogStatus::Info,
		                    L"启动进程", exePath);
		try
		{
			// 启动前预检环境数据目录（影子目录）：缺失自动创建、写探针验证可写、检查磁盘空间。
			// 目录异常时提前拦截并提示，避免应用初始化阶段自行崩溃（如资源加载失败弹窗）。
			if (const std::optional<std::wstring> precheckError = precheck_env_data_dir(env.get()))
			{
				env_logger().append(env->getIndex(), EnvLogType::Error, EnvLogStatus::Fail,
				                    L"环境数据目录检查失败", *precheckError);
				throw std::runtime_error(wide_to_utf8(*precheckError));
			}
			// 启动前确保该环境注册表 hive 已加载（DLL 注入后优先使用 HKU\eBox_Env_<idx> 的虚拟注册表）。
			// 加载失败时 DLL 侧会自动回退为按进程加载，进程仍能正常启动，但不能静默降级：
			// 该模式下注册表变更不持久化（企业微信会要求重新扫码），必须以错误级别暴露给用户。
			// 具体失败原因（错误码、备份路径）由 Env::loadRegistryHive 写入环境日志。
			if (!env->loadRegistryHive())
			{
				env_logger().append(env->getIndex(), EnvLogType::Error, EnvLogStatus::Fail,
				                    L"注册表", L"环境注册表未加载，已回退为进程级虚拟注册表：本次运行的登录态等数据不会持久化（重启后需重新扫码）");
			}
			// 首次复用宿主机已下载好的浏览器内核（避免环境内首次解压失败导致开不了窗口）
			reuse_host_bitbrowser_kernel(env.get());
			const PROCESS_INFORMATION procInfo = create_and_inject(env.get(), exePath, params);
			// 32 位注入器路径下目标进程已由注入器自行 Resume，hThread/hProcess 为空
			if (procInfo.hThread != nullptr)
			{
				ResumeThread(procInfo.hThread);
				CloseHandle(procInfo.hThread);
			}
			if (procInfo.hProcess != nullptr)
			{
				CloseHandle(procInfo.hProcess);
			}
			env_logger().append(env->getIndex(), EnvLogType::Process, EnvLogStatus::Success,
			                    L"进程已启动", exePath);
		}
		catch (const std::exception& e)
		{
			// what() 为 UTF-8（与 show_utf8_error_message 一致），需按 UTF-8 转宽串，避免日志乱码
			env_logger().append(env->getIndex(), EnvLogType::Error, EnvLogStatus::Fail,
			                    L"启动进程失败", utf8_to_wide(e.what() ? e.what() : ""));
			throw;
		}
		co_return;
	}

	coro::LazyTask<void> Launcher::launchWithPoll(std::shared_ptr<Env> env, std::wstring exePath, std::wstring params)
	{
		// 明确先切到独立工作线程（m_execCtx），确保本协程第一行起就绝不在 UI 线程执行——
		// 即便 future 变更了 AsyncScope::spawn 的起始执行线程，也不会在 UI 线程运行任何
		// 可能阻塞的启动逻辑（如注册表 hive 加载、进程注入）。
		co_await sched::transfer_to(m_execCtx);
		// 错峰启动：与其它启动（UI 单击 / RPC 批量）排队串行，相邻启动保持最小间隔
		co_await acquireLaunchSlot();
		co_await launch(env, exePath, params);
		releaseLaunchSlot();
		// 启动阶段（创建环境/注入/resume）结束，放行下一次点击；
		// 轮询兜底阶段不阻塞其它启动，故在此释放。
		m_bUiLaunchGuard.store(false, std::memory_order_release);
		// 只对轮询部分启用取消：应用退出（Launcher 析构）时立即终止轮询，
		// 避免 join() 阻塞最多 60 秒、或超时弹窗挡住退出流程。
		co_await coro::co_with_cancellation(pollTargetProcess(std::move(env), std::move(exePath), std::move(params)), m_stopSource.get_token());
		co_return;
	}

	coro::LazyTask<void> Launcher::pollTargetProcess(std::shared_ptr<Env> env, std::wstring exePath, std::wstring params)
	{
		std::stop_token token = co_await coro::get_current_cancellation_token();
		namespace fs = std::filesystem;
		const std::wstring exeName = fs::path{exePath}.filename().native();
		if (exeName.empty())
		{
			co_return;
		}
		// 最多等待 60 秒，每秒检查一次目标 exe 是否已出现在系统中。
		// 只要进程出现过即视为启动成功（哪怕随后退出），不再打扰用户；
		// 慢电脑/老系统上应用进程创建本身较慢，也可能注入失败导致进程从未出现。
		bool bSeen = false;
		for (int i = 0; i < 60; ++i)
		{
			if (token.stop_requested())
			{
				// 应用正在退出，不再轮询
				co_return;
			}
			if (isProcessRunning(exeName))
			{
				bSeen = true;
				break;
			}
			co_await sched::transfer_after(std::chrono::seconds{1}, m_execCtx);
		}
		if (bSeen || token.stop_requested())
		{
			co_return;
		}
		// 超时未出现：切到 UI 线程弹窗，询问是否重试
		co_await sched::transfer_to(app().get_scheduler());
		if (token.stop_requested())
		{
			// 退出过程中超时，不弹窗打扰
			co_return;
		}
		const int ret = MessageBoxW(nullptr,
		                            std::format(L"「{}」未能正常启动。\n电脑配置较低或系统较旧时首次启动可能较慢，\n是否再试一次？", exePath).c_str(),
		                            MainApp::appName.data(), MB_YESNO | MB_ICONQUESTION | MB_TASKMODAL);
		if (ret == IDYES)
		{
			co_await launch(env, exePath, params);
		}
		co_return;
	}

	bool Launcher::isProcessRunning(std::wstring_view exeName)
	{
		const HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnapshot == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		bool bFound = false;
		PROCESSENTRY32W entry{sizeof(entry)};
		if (Process32FirstW(hSnapshot, &entry))
		{
			do
			{
				if (_wcsicmp(entry.szExeFile, std::wstring{exeName}.c_str()) == 0)
				{
					bFound = true;
					break;
				}
			} while (Process32NextW(hSnapshot, &entry));
		}
		CloseHandle(hSnapshot);
		return bFound;
	}
}
