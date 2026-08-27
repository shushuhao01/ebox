export module Env:EnvManager;

import std;
import :Envrironment;

namespace biz
{
	export class EnvManager
	{
	public:
		EnvManager();
		void loadEnvFrom(std::uint32_t index, std::uint64_t flag, std::wstring_view flagName, std::wstring_view name, std::wstring_view appPath);
		std::shared_ptr<Env> createEnv();
		std::shared_ptr<Env> findEnvByFlagNoExcept(std::uint64_t flag) const;
		std::shared_ptr<Env> findEnvByFlag(std::uint64_t flag) const;
		std::size_t getEnvCount() const;

		// 绑定/查询环境对应的应用路径（环境卡片“启动”按钮使用）
		void setEnvAppPath(std::shared_ptr<Env> env, std::wstring_view appPath);

		// 记录/查询某个程序上次使用的环境，用于“启动时优先复用上次的环境”
		void setLastEnvForProc(std::wstring_view procFullPath, const std::shared_ptr<Env>& env);
		std::shared_ptr<Env> getLastEnvForProc(std::wstring_view procFullPath) const;

		// 重命名环境（内存 + 持久化）
		bool renameEnv(std::shared_ptr<Env> env, std::wstring_view newName);

		void deleteEnv(std::shared_ptr<Env> env);

		bool containsProcessIdExclude(std::uint32_t pid, std::uint64_t excludeEnvFlag) const;
		std::vector<DWORD> getAllProcessIdsExclude(std::uint64_t excludeEnvFlag) const;

		bool containsToplevelWindowExclude(void* hWnd, std::uint64_t excludeEnvFlag) const;
		std::vector<void*> getAllToplevelWindows() const;
		std::vector<void*> getAllToplevelWindowsExclude(std::uint64_t excludeEnvFlag) const;

	public:
		std::vector<std::shared_ptr<Env>> getAllEnv() const;

		enum class EChangeType:std::uint8_t
		{
			Create,
			Delete
		};

		using EnvChangeNotify = std::function<void(EChangeType, const std::shared_ptr<Env>&)>;
		void setEnvChangeNotify(EnvChangeNotify envChangeNotify);

	private:
		struct EnvFlagInfo
		{
			std::uint64_t flag;
			std::wstring flagName;
		};

		EnvFlagInfo ensureCreateNewEnvFlag(std::uint32_t index) const;

		void addEnv(const std::shared_ptr<Env>& env);
		void removeEnv(std::uint64_t flag);

	private:
		std::atomic<std::uint32_t> m_currentIndex{0};
		mutable std::shared_mutex m_mutex;
		std::unordered_map<std::uint64_t, std::shared_ptr<Env>> m_flagToEnv;
		EnvChangeNotify m_envChangeNotify;
	};
}
