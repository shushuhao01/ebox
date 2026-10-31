// ReSharper disable CppUseRangeAlgorithm
module;
// #define _CRT_SECURE_NO_WARNINGS
// #include <cstdio>
module GlobalData;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

namespace
{
	// 已确认存在的目录缓存：环境数据目录创建后不会消失，避免每次文件访问都做
	// fs::create_directories（它会沿路径每一级做 stat 系统调用）。WXWork 高频文件
	// 访问（切换聊天/加载图片/属性查询）时这是 UI 线程的显著开销来源。
	// new + 永不析构：与 hook_cache 同一约定，避免短命中转进程（cmd）退出竞态。
	class DirExistsCache
	{
	public:
		bool contains(std::wstring_view dir) const
		{
			AcquireSRWLockShared(&m_lock);
			const bool hit = m_dirs.find(std::wstring{dir}) != m_dirs.end();
			ReleaseSRWLockShared(&m_lock);
			return hit;
		}

		void insert(std::wstring_view dir)
		{
			AcquireSRWLockExclusive(&m_lock);
			if (m_dirs.size() >= MaxEntries)
			{
				m_dirs.clear(); // 防无界增长；超限重建缓存，不影响正确性
			}
			m_dirs.emplace(dir);
			ReleaseSRWLockExclusive(&m_lock);
		}

	private:
		static constexpr std::size_t MaxEntries = 2048;
		mutable SRWLOCK m_lock{SRWLOCK_INIT};
		std::unordered_set<std::wstring> m_dirs;
	};

	DirExistsCache* g_dirExistsCachePtr = nullptr;

	DirExistsCache& dir_exists_cache()
	{
		// 反射注入子进程下 magic static 的 _Init_thread_header 初始化机制不可靠
		// （实测 WXWorkWeb 崩溃），与 hook_cache 同一约定：InterlockedCompareExchangePointer
		// 一次性发布，无 CRT 依赖、反射注入安全、线程安全；永不析构由操作系统回收。
		if (g_dirExistsCachePtr == nullptr)
		{
			DirExistsCache* fresh = new DirExistsCache;
			if (::InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&g_dirExistsCachePtr),
			                                        fresh, nullptr) != nullptr)
			{
				delete fresh; // 其他线程抢先发布，释放本线程临时对象
			}
		}
		return *g_dirExistsCachePtr;
	}

	// void InitConsole()
	// {
	// 	if (!AllocConsole())
	// 	{
	// 		return;
	// 	}
	//
	// 	freopen("CONOUT$", "w", stdout);
	// }

	BOOL get_process_elevation(TOKEN_ELEVATION_TYPE* pElevationType, BOOL* pIsAdmin)
	{
		HANDLE hToken{nullptr};
		DWORD dwSize;

		// Get current process token
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
		{
			return FALSE;
		}

		BOOL bResult = FALSE;
		// Retrieve elevation type information 
		if (GetTokenInformation(hToken, TokenElevationType,
		                        pElevationType, sizeof(TOKEN_ELEVATION_TYPE), &dwSize))
		{
			// Create the SID corresponding to the Administrators group
			byte adminSid[SECURITY_MAX_SID_SIZE]{};
			dwSize = sizeof(adminSid);
			CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, &adminSid, &dwSize);

			if (*pElevationType == TokenElevationTypeLimited)
			{
				// Get handle to linked token (will have one if we are lua)
				HANDLE hUnfilteredToken{nullptr};
				if (GetTokenInformation(hToken, TokenLinkedToken,
				                        &hUnfilteredToken, sizeof(HANDLE), &dwSize))
				{
					// Check if this original token contains admin SID
					if (CheckTokenMembership(hUnfilteredToken, &adminSid, pIsAdmin))
					{
						bResult = TRUE;
					}
					CloseHandle(hUnfilteredToken);
				}
			}
			else
			{
				*pIsAdmin = IsUserAnAdmin();
				bResult = TRUE;
			}
		}
		CloseHandle(hToken);
		return bResult;
	}
}

namespace global
{
	void Data::initialize(SystemVersionInfo versionInfo, std::uint64_t envFlag, unsigned long envIndex, std::wstring_view rootPath)
	{
		m_sysVersion = versionInfo;
		m_envFlag = envFlag;
		m_envIndex = envIndex;
		m_envFlagName = std::format(L"{:016X}", envFlag);
		m_envFlagNameA = std::format("{:016X}", envFlag);
		m_rootPath = rootPath;

		initializePrivilegesAbout();
		initializeRegistry();
		initializeSelfPath();
		initializeDllFullPath();
		initializeKnownFolderPath();
		initializeMisc();

		// std::wcout.imbue(std::locale(""));
		// InitConsole();
	}

	static constexpr std::wstring_view PREFIX_TO_CHECK(LR"(\??\)");

	// 大小写不敏感子串匹配（避免对整条路径做 tolower 复制+逐字符转换）
	bool contains_icase(std::wstring_view haystack, std::wstring_view needle)
	{
		if (needle.empty())
		{
			return true;
		}
		if (needle.length() > haystack.length())
		{
			return false;
		}
		const std::size_t lastStart = haystack.length() - needle.length();
		for (std::size_t i = 0; i <= lastStart; ++i)
		{
			if (_wcsnicmp(haystack.data() + i, needle.data(), needle.length()) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool Data::isInKnownFolderPath(std::wstring_view path) const
	{
		if (m_knownFolders.empty())
		{
			return false;
		}

		if (!path.starts_with(PREFIX_TO_CHECK))
		{
			return false;
		}

		const std::wstring_view pathToCheck = path.substr(PREFIX_TO_CHECK.length());

		// 快速排除：含系统组件/自身数据目录关键字时绝不重定向（与原逻辑等价，大小写不敏感）
		if (contains_icase(pathToCheck, L"microsoft")
			|| contains_icase(pathToCheck, L"nvidia")
			|| contains_icase(pathToCheck, L"amd")
			|| contains_icase(pathToCheck, LR"(\ebox\env\)")
			|| contains_icase(pathToCheck, LR"(\2box\env\)"))  // 兼容老版本路径
		{
			return false;
		}

		// 只对已知文件夹长度部分做大小写不敏感比较，避免整路径复制+tolower 的开销
		for (const std::wstring& knownFolder : m_knownFolders)
		{
			if (knownFolder.length() > pathToCheck.length())
			{
				continue;
			}
			if (_wcsnicmp(pathToCheck.data(), knownFolder.c_str(), knownFolder.length()) == 0)
			{
				return true;
			}
		}
		return false;
	}

	bool Data::is_key_not_in_app_cache(std::wstring_view keyName) const
	{
		if (m_negKeySet.empty())
		{
			return false;
		}
		std::shared_lock lock(m_negKeyMutex);
		return m_negKeySet.find(std::wstring{keyName}) != m_negKeySet.end();
	}

	void Data::mark_key_not_in_app_cache(std::wstring_view keyName) const
	{
		std::unique_lock lock(m_negKeyMutex);
		if (m_negKeySet.size() >= 256)
		{
			m_negKeySet.clear(); // 简单防无界增长；超限重建缓存，不影响正确性
		}
		m_negKeySet.emplace(keyName);
	}

	void Data::clear_key_not_in_app_cache(std::wstring_view keyName) const
	{
		if (m_negKeySet.empty())
		{
			return;
		}
		std::unique_lock lock(m_negKeyMutex);
		m_negKeySet.erase(std::wstring{keyName});
	}

	std::optional<std::wstring> Data::getRedirectPath(std::wstring_view knownFolderPath) const
	{
		static constexpr std::wstring_view driverMarker(LR"(:\)");

		// 快速路径：缓存命中（源路径 → 重定向目标，环境运行期稳定，避免每次做 weakly_canonical 文件系统 I/O）
		if (!m_redirectCache.empty())
		{
			std::shared_lock lock(m_redirectCacheMutex);
			const auto it = m_redirectCache.find(std::wstring{knownFolderPath});
			if (it != m_redirectCache.end())
			{
				return it->second;
			}
		}

		std::optional<std::wstring> result;
		try
		{
			namespace fs = std::filesystem;
			if (const size_t driverPos = knownFolderPath.find(driverMarker); driverPos != std::wstring_view::npos)
			{
				const fs::path indexPath{std::format(L"{}", m_envIndex)};
				const fs::path relativePath{knownFolderPath.substr(driverPos + driverMarker.length())};
				// 缓存 miss 时免 I/O：相对路径不含 "." / ".." 特殊段时直接拼接，
				// 避免每次 miss 都做 weakly_canonical 文件系统探测。切换企业主体瞬间
				// 大量新路径 miss（缓存上限已提高），I/O 累积会拖慢调用线程 → 卡顿/未响应；
				// 仅当确含特殊段时才退化为 weakly_canonical 消除 "." / ".."。
				fs::path redirectPath;
				bool needCanonical = false;
				for (const auto& seg : relativePath)
				{
					if (seg == L"." || seg == L"..")
					{
						needCanonical = true;
						break;
					}
				}
				if (needCanonical)
				{
					redirectPath = fs::weakly_canonical(fs::path{m_rootPath} / fs::path{L"Env"} / indexPath / relativePath);
				}
				else
				{
					redirectPath = fs::path{m_rootPath} / fs::path{L"Env"} / indexPath / relativePath;
				}
				result = std::format(L"{}{}", PREFIX_TO_CHECK, redirectPath.native());
			}
		}
		catch (...)
		{
		}

		if (result)
		{
			std::unique_lock lock(m_redirectCacheMutex);
			// 上限提高到 4096：切换主体瞬间大量新路径入缓存，512 会立刻被刷爆后整表清空，
			// 老路径（正常运行高频访问）随之全部 miss 并重复做路径解析；4096 足以覆盖
			// 一次切换产生的全部新路径，避免频繁整表重建。
			if (m_redirectCache.size() >= 4096)
			{
				m_redirectCache.clear();
			}
			m_redirectCache.emplace(std::wstring{knownFolderPath}, *result);
		}
		return result;
	}

	std::optional<std::wstring> redirect_executable_path(std::wstring_view path)
	{
		// 仅处理"已知文件夹（%APPDATA% 等）内的可执行文件"：
		// 这类 exe 在环境内真实存在，但 CreateProcess 用逻辑路径打开映像时找不到。
		if (path.empty())
		{
			return std::nullopt;
		}
		const Data& data = Data::get();
		const std::wstring ntPath = std::format(L"{}{}", PREFIX_TO_CHECK, path);
		if (!data.isInKnownFolderPath(ntPath))
		{
			return std::nullopt;
		}
		const std::optional<std::wstring> redirectPath = data.getRedirectPath(ntPath);
		if (!redirectPath)
		{
			return std::nullopt;
		}
		std::wstring_view target = *redirectPath;
		if (target.starts_with(PREFIX_TO_CHECK))
		{
			target = target.substr(PREFIX_TO_CHECK.length());
		}
		std::error_code ec;
		if (!std::filesystem::is_regular_file(std::wstring{target}, ec) || ec)
		{
			return std::nullopt;
		}
		return std::wstring{target};
	}

	std::optional<std::wstring> redirect_executable_command_line(std::wstring_view cmdLine)
	{
		// 解析命令行首 token（exe 路径，可能带引号），命中重定向时重建整条命令行。
		std::wstring_view rest = cmdLine;
		while (!rest.empty() && (rest.front() == L' ' || rest.front() == L'\t'))
		{
			rest.remove_prefix(1);
		}
		if (rest.empty())
		{
			return std::nullopt;
		}
		std::wstring_view exe;
		std::wstring_view tail;
		if (rest.front() == L'"')
		{
			const std::size_t end = rest.find(L'"', 1);
			if (end == std::wstring_view::npos)
			{
				return std::nullopt;
			}
			exe = rest.substr(1, end - 1);
			tail = rest.substr(end + 1);
		}
		else
		{
			const std::size_t end = rest.find_first_of(L" \t");
			exe = end == std::wstring_view::npos ? rest : rest.substr(0, end);
			tail = end == std::wstring_view::npos ? std::wstring_view{} : rest.substr(end);
		}
		if (exe.empty())
		{
			return std::nullopt;
		}
		const std::optional<std::wstring> redirectPath = redirect_executable_path(exe);
		if (!redirectPath)
		{
			return std::nullopt;
		}
		return std::format(L"\"{}\"{}", *redirectPath, tail);
	}

	namespace
	{
		// 临时诊断输出：直接 Win32 追加写，避免依赖流/静态对象（反射注入下安全）。
		void write_trace_line(std::wstring_view text)
		{
			const std::wstring logPath = std::format(L"{}\\logs\\shell-redirect.log", global::Data::get().rootPath());
			const HANDLE h = ::CreateFileW(logPath.c_str(), FILE_APPEND_DATA,
			                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
			                               FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h == INVALID_HANDLE_VALUE)
			{
				return;
			}
			DWORD written = 0;
			::WriteFile(h, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)), &written, nullptr);
			::CloseHandle(h);
		}

		std::optional<std::wstring> redirect_known_folder_path_impl(std::wstring_view path)
		{
		// 用于把"表面路径"交给外部程序（典型：explorer.exe 打开/定位文件）之前做翻译。
		// explorer.exe 不在被注入的重定向进程范围内，交给它的路径必须是环境内真实路径，
		// 否则会弹"位置不可用"。
		//
		// 保守规则（避免误改共享/公有目录）：
		//   1. 仅绝对路径 "X:\..."；
		//   2. 仅已知文件夹（%APPDATA% / Documents 等）；
		//   3. 环境内对应路径存在 → 才替换。
		// 注意：这里不能再检查"宿主侧是否存在"——本函数运行在被注入进程内，
		// NtQueryAttributesFile/NtCreateFile 已被重定向，exists() 看到的就是环境视图，
		// 必然为真，会把所有路径都误判为"宿主可见"而放弃改写（实测"打开文件夹"因此失效）。
		if (path.empty())
		{
			return std::nullopt;
		}

		std::wstring cleaned{path};
		while (!cleaned.empty() && (cleaned.back() == L'\0' || cleaned.back() == L' ' || cleaned.back() == L'\t'))
		{
			cleaned.pop_back();
		}
		std::size_t head = 0;
		while (head < cleaned.size() && (cleaned[head] == L' ' || cleaned[head] == L'\t'))
		{
			++head;
		}
		if (head > 0)
		{
			cleaned.erase(0, head);
		}
		if (cleaned.size() >= 2 && cleaned.front() == L'"' && cleaned.back() == L'"')
		{
			cleaned = cleaned.substr(1, cleaned.size() - 2);
		}
		if (cleaned.empty())
		{
			return std::nullopt;
		}

		// 统一去掉 \\?\ 或 \??\ 前缀
		if (cleaned.starts_with(LR"(\\?\)") || cleaned.starts_with(LR"(\??\)"))
		{
			cleaned.erase(0, 4);
		}

		const Data& data = Data::get();
		const std::wstring_view rootPath = data.rootPath();
		if (!rootPath.empty() && cleaned.length() >= rootPath.length()
			&& _wcsnicmp(cleaned.c_str(), rootPath.data(), rootPath.length()) == 0)
		{
			return std::nullopt; // 已经是环境内路径，避免重复/递归改写
		}

		if (cleaned.length() < 3 || cleaned[1] != L':' || cleaned[2] != L'\\')
		{
			return std::nullopt; // 非绝对路径（含 UNC）不处理
		}

		const std::wstring ntPath = std::format(L"{}{}", PREFIX_TO_CHECK, cleaned);
		if (!data.isInKnownFolderPath(ntPath))
		{
			return std::nullopt;
		}

		const std::optional<std::wstring> redirectPath = data.getRedirectPath(ntPath);
		if (!redirectPath)
		{
			return std::nullopt;
		}
		std::wstring_view target = *redirectPath;
		if (target.starts_with(PREFIX_TO_CHECK))
		{
			target = target.substr(PREFIX_TO_CHECK.length());
		}
		std::wstring targetPath{target};
		std::error_code ec;
		if (!std::filesystem::exists(targetPath, ec) || ec)
		{
			return std::nullopt;
		}
		return targetPath;
		}
	}

	namespace
	{
		// 诊断开关：默认关闭，避免 shell-redirect.log 在用户机器上无限增长。
		// 需要排查时设置环境变量 EBOX_SHELL_TRACE=1 再启动环境。
		// 用 InterlockedXXX 一次性判定，无 CRT 静态初始化依赖（反射注入下安全）。
		bool shell_trace_enabled()
		{
			constexpr DWORD kBufSize = 16;
			static volatile LONG state = 0; // 0=未判定 1=关闭 2=开启
			LONG current = ::InterlockedCompareExchange(&state, 0, 0);
			if (current == 0)
			{
				wchar_t buf[kBufSize]{};
				const DWORD len = ::GetEnvironmentVariableW(L"EBOX_SHELL_TRACE", buf, kBufSize);
				const bool on = len > 0 && len < kBufSize && buf[0] == L'1';
				current = on ? 2 : 1;
				::InterlockedExchange(&state, current);
			}
			return current == 2;
		}
	}

	void debug_trace(std::wstring_view text)
	{
		if (!shell_trace_enabled())
		{
			return;
		}
		write_trace_line(std::format(L"[{}] {}\r\n", ::GetCurrentProcessId(), text));
	}

	std::optional<std::wstring> redirect_known_folder_path(std::wstring_view path)
	{
		const std::optional<std::wstring> result = redirect_known_folder_path_impl(path);
		debug_trace(std::format(L"knownFolder: [{}] => {}", path, result ? *result : L"<nullopt>"));
		return result;
	}

	std::optional<std::wstring> redirect_paths_in_command_line(std::wstring_view cmdLine)
	{
		// 命令行可能是 explorer.exe /select,"D:\..." 或 explorer.exe "D:\..."，
		// 逐个识别 "X:\..." 片段并尝试重定向。
		if (cmdLine.empty())
		{
			return std::nullopt;
		}
		const auto is_boundary = [](wchar_t c)
		{
			return c == L' ' || c == L'\t' || c == L'"' || c == L'\'' || c == L','
				|| c == L'=' || c == L';' || c == L'\r' || c == L'\n';
		};
		const auto is_drive_letter = [](wchar_t c)
		{
			return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
		};

		std::wstring result;
		result.reserve(cmdLine.size());
		bool changed = false;
		std::size_t i = 0;
		while (i < cmdLine.size())
		{
			const bool atBoundary = i == 0 || is_boundary(cmdLine[i - 1]);
			if (atBoundary && i + 2 < cmdLine.size() && is_drive_letter(cmdLine[i])
				&& cmdLine[i + 1] == L':' && cmdLine[i + 2] == L'\\')
			{
				std::size_t end = i + 3;
				while (end < cmdLine.size() && !is_boundary(cmdLine[end]))
				{
					++end;
				}
				const std::wstring_view token = cmdLine.substr(i, end - i);
				if (const std::optional<std::wstring> redirected = redirect_known_folder_path(token))
				{
					result.append(*redirected);
					changed = true;
				}
				else
				{
					result.append(token);
				}
				i = end;
				continue;
			}
			result.push_back(cmdLine[i]);
			++i;
		}
		if (!changed)
		{
			return std::nullopt;
		}
		return result;
	}

	void Data::initializePrivilegesAbout()
	{
		TOKEN_ELEVATION_TYPE elevationType = TokenElevationTypeDefault;
		BOOL bIsAdmin = FALSE;
		if (get_process_elevation(&elevationType, &bIsAdmin))
		{
			if (elevationType != TokenElevationTypeLimited)
			{
				m_bIsNonLimitedAdmin = bIsAdmin ? true : false;
			}
		}
	}

	void Data::initializeRegistry()
	{
		// 环境虚拟注册表有两种共享存储：
		//  1. eBox 提权运行：RegLoadKeyW 挂到 HKU\eBox_Env_<index> 的 hive（数据在环境目录）
		//  2. eBox 普通用户运行：HKCU\Software\eBox\EnvRegistry\<index>（Windows 自动落盘）
		// 选择顺序必须与 eBox 侧 Env::loadRegistryHive 一致：已存在的用户注册表存储优先，
		// 否则同一环境会在两套存储之间来回切换，用户每次都要重新扫码。
		m_appKey = RegKey{
			[&]()-> HKEY
			{
				const std::wstring userStore = std::format(L"Software\\eBox\\EnvRegistry\\{}", m_envIndex);
				HKEY hUserStore = nullptr;
				if (RegOpenKeyExW(HKEY_CURRENT_USER, userStore.c_str(), 0, KEY_ALL_ACCESS, &hUserStore) == ERROR_SUCCESS)
				{
					return hUserStore;
				}
				const std::wstring subKey = std::format(L"eBox_Env_{}", m_envIndex);
				HKEY hKey = nullptr;
				if (RegOpenKeyExW(HKEY_USERS, subKey.c_str(), 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS)
				{
					return hKey;
				}
				// 两种共享存储都不存在（eBox 侧加载失败，处于临时模式）：不回退到任何存储，
				// hook 里对 appKey 的写入/查询会优雅失败（仅走真实注册表），与 eBox 的判断保持一致。
				// 注意：这里绝不自行创建用户注册表存储，否则会把"本可恢复 hive 的环境"永久切到用户存储。
				return nullptr;
			}
		};
	}

	void Data::initializeSelfPath()
	{
		constexpr DWORD pathLength = std::numeric_limits<short>::max();
		m_selfFullPath.resize(pathLength);
		DWORD resultSize = GetModuleFileNameW(nullptr, m_selfFullPath.data(), pathLength);
		m_selfFullPath.resize(resultSize);
		m_selfFullPath = std::wstring(m_selfFullPath);
		m_selfFileName = std::filesystem::path{m_selfFullPath}.filename().native();
		m_bIsCmd = _wcsicmp(m_selfFileName.c_str(), L"cmd.exe") == 0;
	}

	void Data::initializeDllFullPath()
	{
		namespace fs = std::filesystem;
		// 同时记录 32/64 两个位宽的注入 DLL 路径。启动父进程（cmd）时用与自身
		// 架构一致的那个；hook 到 CreateProcessW 给目标应用注入时，按目标进程
		// 实际位数选择对应位宽的 DLL，避免 32 位应用被注入 64 位 DLL 而加载失败。
		const fs::path path64{fs::weakly_canonical(fs::path{m_rootPath} / fs::path{L"bin"} / fs::path{std::format(L"{}_64.bin", m_envFlagName)})};
		const fs::path path32{fs::weakly_canonical(fs::path{m_rootPath} / fs::path{L"bin"} / fs::path{std::format(L"{}_32.bin", m_envFlagName)})};
		m_dllFullPath64 = path64.string();
		m_dllFullPath32 = path32.string();
		if constexpr (CURRENT_ARCH_BIT == ArchBit::Bit64)
		{
			m_dllFullPath = m_dllFullPath64;
		}
		else
		{
			m_dllFullPath = m_dllFullPath32;
		}
	}

	void Data::initializeKnownFolderPath()
	{
		// 注意：QYWX(以及其他 Chromium/CEF 内核应用)会把登录态/用户数据写到
		// “我的文档\WXWork”(user-data-dir)，因此 FOLDERID_Documents 必须一并重定向，
		// 否则登录态不会随环境持久化，且多开实例会共享同一份 Web 层数据，存在风控风险。
		static const std::array rfidArray = {FOLDERID_LocalAppData, FOLDERID_LocalAppDataLow, FOLDERID_RoamingAppData, FOLDERID_SavedGames, FOLDERID_ProgramData, FOLDERID_Documents};

		for (size_t i = 0; i < rfidArray.size(); ++i)
		{
			const KNOWNFOLDERID& rfid = rfidArray[i];
			wchar_t* out;
			if (S_OK == SHGetKnownFolderPath(rfid, 0, nullptr, &out))
			{
				std::wstring_view sv{out};
				std::transform(sv.begin(), sv.end(), out, std::towlower);
				if (std::optional<std::wstring> redirectPath = getRedirectPath(sv))
				{
					if (ensure_dir_exists(redirectPath.value(), true))
					{
						m_knownFolders.push_back(std::format(L"{}\\", sv));
					}
				}
				CoTaskMemFree(out);
			}
		}
	}

	void Data::initializeMisc()
	{
		m_inputSyncMsgId = RegisterWindowMessageW(L"eBox_WM_INPUT_SYNC");
		if (!m_inputSyncMsgId)
		{
			m_inputSyncMsgId = 9527;
		}
	}

	bool ensure_dir_exists(std::wstring_view fullName, bool bIsDir)
	{
		try
		{
			namespace fs = std::filesystem;
			const fs::path path{fullName};
			const fs::path dirPath = bIsDir ? path : path.parent_path();
			// 目录存在性缓存：目录创建后不会消失，避免每次文件访问都做
			// fs::create_directories（沿路径每一级 stat 系统调用）。WXWork 高频
			// 文件访问（切换聊天/加载图片/属性查询）时这是 UI 线程显著开销。
			if (dir_exists_cache().contains(dirPath.native()))
			{
				return true;
			}
			fs::create_directories(dirPath);
			dir_exists_cache().insert(dirPath.native());
			return true;
		}
		catch (...)
		{
		}
		return false;
	}
}
