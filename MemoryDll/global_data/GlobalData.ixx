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
			static Data instance;
			return instance;
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
