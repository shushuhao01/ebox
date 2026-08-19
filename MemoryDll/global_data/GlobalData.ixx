export module GlobalData;

import "sys_defs.h";
import std;

namespace global
{
	export class RegKey
	{
	public:
		RegKey() = default;

		explicit RegKey(auto creator)
		{
			m_key = creator();
		}

		~RegKey()
		{
			if (m_key)
			{
				RegCloseKey(m_key);
			}
		}

		RegKey(const RegKey&) = delete;
		RegKey& operator=(const RegKey&) = delete;

		RegKey(RegKey&& that) noexcept : m_key(std::exchange(that.m_key, nullptr))
		{
		}

		RegKey& operator=(RegKey&& that) noexcept
		{
			std::swap(m_key, that.m_key);
			return *this;
		}

		operator HKEY() const { return m_key; }

	private:
		HKEY m_key{nullptr};
	};

	export class Data
	{
	public:
		static Data& get()
		{
			// 反射注入子进程下 magic static 的 _Init_thread_header 初始化机制不可靠
			// （实测 WXWorkWeb/crashpad 运行时崩溃，见 hook_cache 注释），且 Data 含
			// unordered 容器/共享互斥量，若构造未完成即被并发访问会以野桶指针 c0000005
			// 崩溃。改为 InterlockedCompareExchangePointer 一次性发布 + 永不析构：
			// 无 CRT 依赖、反射注入安全、线程安全，由操作系统在进程退出时回收。
			static Data* s_instance = nullptr; // 常量初始化，无 guard
			if (s_instance == nullptr)
			{
				Data* fresh = new Data;
				if (::InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&s_instance),
				                                        fresh, nullptr) != nullptr)
				{
					delete fresh; // 其他线程抢先发布，释放本线程临时对象
				}
			}
			return *s_instance;
		}

	public:
		void initialize(SystemVersionInfo versionInfo, std::uint64_t envFlag, unsigned long envIndex, std::wstring_view rootPath);

	public:
		SystemVersionInfo sysVersion() const { return m_sysVersion; }
		std::uint64_t envFlag() const { return m_envFlag; }
		std::uint32_t envIndex() const { return m_envIndex; }
		bool isNonLimitedAdmin() const { return m_bIsNonLimitedAdmin; }
		bool isCmd() const { return m_bIsCmd; }
		std::wstring_view envFlagName() const { return m_envFlagName; }
		std::string_view envFlagNameA() const { return m_envFlagNameA; }
		std::wstring_view rootPath() const { return m_rootPath; }
		// 当前进程架构对应的注入 DLL 路径（兼容旧调用）
		std::string_view dllFullPath() const { return m_dllFullPath; }
		// 按目标进程位数选择注入 DLL：true=64 位进程，false=32 位进程
		std::string_view dllFullPathForArch(bool want64) const
		{
			return want64 ? std::string_view{m_dllFullPath64} : std::string_view{m_dllFullPath32};
		}
		HKEY appKey() const { return m_appKey; }
		std::uint32_t inputSyncMsgId() const { return m_inputSyncMsgId; }

		bool isInKnownFolderPath(std::wstring_view path) const;
		std::optional<std::wstring> getRedirectPath(std::wstring_view knownFolderPath) const;

		// 注册表虚拟化否定缓存：某 keyName 经探测不在 appKey 中（走真实注册表）。
		// 用于避免 NtQueryValueKey 每次查询都做一次失败的 RegOpenKeyExW(appKey) 系统调用。
		// 语义安全：appKey 是环境私有 hive，仅环境内进程经 NtSetValueKey hook 写入；
		// 写入时 clear_key_not_in_app_cache 使缓存失效，保证查询结果始终正确。
		bool is_key_not_in_app_cache(std::wstring_view keyName) const;
		void mark_key_not_in_app_cache(std::wstring_view keyName) const;
		void clear_key_not_in_app_cache(std::wstring_view keyName) const;

	private:
		Data() = default;

		void initializePrivilegesAbout();
		void initializeRegistry();
		void initializeSelfPath();
		void initializeDllFullPath();
		void initializeKnownFolderPath();
		void initializeMisc();

	private:
		SystemVersionInfo m_sysVersion;
		std::uint64_t m_envFlag{0};
		std::uint32_t m_envIndex{0};
		bool m_bIsNonLimitedAdmin{false};
		bool m_bIsCmd{false};
		std::wstring m_envFlagName;
		std::string m_envFlagNameA;
		std::string m_dllFullPath;   // 当前进程架构对应的注入 DLL 路径
		std::string m_dllFullPath32; // 32 位目标进程注入用（MemoryDll32.dll）
		std::string m_dllFullPath64; // 64 位目标进程注入用（MemoryDll64.dll）
		std::wstring m_rootPath;
		std::wstring m_selfFullPath;
		std::wstring m_selfFileName;
		RegKey m_appKey;
		std::vector<std::wstring> m_knownFolders;
		std::uint32_t m_inputSyncMsgId{0};

		// 源路径 → 重定向目标路径 缓存（运行期稳定，避免每次文件操作都做 weakly_canonical 真实文件系统 I/O）
		mutable std::shared_mutex m_redirectCacheMutex;
		mutable std::unordered_map<std::wstring, std::wstring> m_redirectCache;

		// 注册表虚拟化否定缓存（上限 256，超限整体清空重建）
		mutable std::shared_mutex m_negKeyMutex;
		mutable std::unordered_set<std::wstring> m_negKeySet;
	};

	export bool is_app_key_name(std::wstring_view fullName)
	{
		// 兼容两种环境虚拟注册表形态：
		//  1. eBox 加载到 HKU\eBox_Env_<idx> 的共享 hive
		//  2. 回退路径 RegLoadAppKeyW 的 \REGISTRY\A\{GUID} 私有 hive
		// 命中即跳过，避免 hook 对虚拟注册表自身操作造成递归。
		static constexpr std::wstring_view hkuPrefix(LR"(\REGISTRY\USER\eBox_Env_)");
		static constexpr std::wstring_view appKeyPrefix(LR"(\REGISTRY\A\{)");
		return fullName.starts_with(hkuPrefix) || fullName.starts_with(appKeyPrefix);
	}

	export std::wstring_view remove_leading_backslashes_sv(std::wstring_view sv)
	{
		const auto it = std::find_if_not(sv.begin(), sv.end(), [](wchar_t c) { return c == L'\\'; });
		if (it == sv.end())
		{
			return sv;
		}
		return sv.substr(std::distance(sv.begin(), it));
	}

	export bool ensure_dir_exists(std::wstring_view fullName, bool bIsDir);
}
