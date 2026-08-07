module;
#include "biz/rpc/RpcServer.h"
export module Biz.Core;

import std;
export import Env;
export import Launcher;
export import WndEnumerator;
export import FileRedirect;
export import EnvLog;

namespace biz
{
	inline FileRedirect* g_file_redirect{nullptr};
	inline EnvManager* g_env_mgr{nullptr};
	inline Launcher* g_launcher{nullptr};
	inline WndEnumerator* g_wnd_enumerator{nullptr};
	inline std::unique_ptr<rpc::Server> g_rpc_server{nullptr};

	export class Core
	{
	public:
		Core()
		{
			g_file_redirect = &m_fileRedirect;
			g_env_mgr = &m_envManager;
			g_launcher = &m_launcher;
			g_wnd_enumerator = &m_wndEnumerator;
			g_env_logger = &m_envLogger;
			g_rpc_server = std::make_unique<rpc::Server>();
			// 启动时从磁盘恢复最近 24 小时日志（离线后历史日志仍可查看）
			m_envLogger.loadFromDisk();
		}

		~Core()
		{
			// 2Box 退出前保存并卸载所有已加载的环境注册表 hive，避免注册表数据丢失
			for (const std::shared_ptr<Env>& env : m_envManager.getAllEnv())
			{
				env->saveRegistryHive();
				env->unloadRegistryHive();
			}
			g_rpc_server.reset();
		}

	private:
		FileRedirect m_fileRedirect;
		EnvManager m_envManager;
		Launcher m_launcher;
		WndEnumerator m_wndEnumerator;
		EnvLogger m_envLogger;
	};

	export FileRedirect& file_redirect()
	{
		return *g_file_redirect;
	}

	export EnvManager& env_mgr()
	{
		return *g_env_mgr;
	}

	export Launcher& launcher()
	{
		return *g_launcher;
	}

	export WndEnumerator& wnd_enumerator()
	{
		return *g_wnd_enumerator;
	}

	export void shutdown_rpc_server()
	{
		g_rpc_server.reset();
	}
}
