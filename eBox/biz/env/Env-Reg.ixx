export module Env:Reg;

import "sys_defs.h";
import std;
import :Envrironment;

namespace biz
{
	class RegKey
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

	struct EnvInitializeData
	{
		std::uint32_t index;
		std::uint64_t flag;
		std::wstring_view flagName;
		std::wstring_view name;
		std::wstring_view appPath;
	};

	using EnvInitializeNotify = std::function<void(const EnvInitializeData&)>;

	export void initialize_env_reg(const EnvInitializeNotify& notify);
	export void add_env_to_reg(std::wstring_view flagName, const Env* env);
	export void delete_env_from_reg(std::wstring_view flagName);
	export void save_env_name_to_reg(std::wstring_view flagName, std::wstring_view name);
	export void save_env_app_path_to_reg(std::wstring_view flagName, std::wstring_view appPath);

	// 最近使用环境映射：procFullPath -> envFlag，用于“启动时优先复用上次的环境”
	export void save_proc_last_env(std::wstring_view procFullPath, std::uint64_t envFlag);
	export std::optional<std::uint64_t> load_proc_last_env(std::wstring_view procFullPath);
}
