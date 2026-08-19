export module FileRedirect;

import "sys_defs.h";
import std;

namespace biz
{
	export class FileRedirect
	{
	public:
		FileRedirect();
		~FileRedirect();

		// 请求把源机文件拷贝到环境内重定向路径。
		// 注意：本方法【不阻塞】调用方——拷贝由内部有界工作池异步执行。
		// 原因：此前在 RPC 线程内同步拷贝（含重试+DB 伴随文件）会让 ncalrpc 端点被
		// 多个环境的拷贝请求长期占用，客户端线程因 ncalrpc 无调用超时（RPC_C_OPT_CALL_TIMEOUT
		// 对 local RPC 无效）会无限阻塞，表现为 eBox/WXWork 双双未响应。
		// 客户端侧改为发起后按需有界轮询重定向目标是否落盘（见 Hook-Ntdll 的 wait_redirect_ready）。
		void requestCreateRedirectFile(const std::wstring& originalFile, const std::wstring& redirectFile);

	private:
		void workerLoop();
		void finishTask(const std::wstring& redirectFile);

	private:
		static constexpr std::size_t MaxQueueSize = 1024;
		static constexpr std::size_t WorkerCount = 4;

		struct Task
		{
			std::wstring originalFile;
			std::wstring redirectFile;
		};

		std::mutex m_mutex; // 保护 m_tasks 去重表
		std::unordered_map<std::wstring, std::uint8_t> m_tasks;

		std::mutex m_queueMutex;
		std::condition_variable m_queueCv;
		std::deque<Task> m_queue;
		bool m_stop{false};
		std::vector<std::thread> m_workers;
	};
}
