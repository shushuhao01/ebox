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
			// eBox 退出前保存并卸载所有已加载的环境注册表 hive，避免注册表数据丢失。
			// RegSaveKeyW / RegUnLoadKeyW 是不可取消的内核同步操作，耗时随 hive 脏页
			// 增长（多环境时逐个串行可达数十秒，表现为退出长时间无响应）。
			// 各环境 hive 相互独立，按环境并行处理，总耗时从"累加"降为"最慢的一个"。
			// 任务持有 shared_ptr，保存期间环境被删除也不会访问已释放对象。
			const std::vector<std::shared_ptr<Env>> envs = m_envManager.getAllEnv();
			std::vector<std::thread> workers;
			workers.reserve(envs.size());
			for (const std::shared_ptr<Env>& env : envs)
			{
				workers.emplace_back([env]
				{
					env->saveRegistryHive();
					env->unloadRegistryHive();
				});
			}
			for (std::thread& worker : workers)
			{
				worker.join();
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
