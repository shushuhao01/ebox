export module Launcher;

import std;
import Scheduler;
import Coroutine;
import Env;

namespace biz
{
	export class Launcher
	{
	public:
		~Launcher()
		{
			// 退出时先请求取消轮询协程，避免 m_asyncScope 析构时的 join() 阻塞最多 60 秒
			m_stopSource.request_stop();
		}

		void run(const std::shared_ptr<Env>& env, std::wstring_view exePath, std::wstring_view params = L"");
		void runInNewEnv(std::wstring_view exePath, std::wstring_view params = L"");

		coro::LazyTask<void> coRun(std::shared_ptr<Env> env, std::wstring_view exePath, std::wstring_view params);

	private:
		coro::LazyTask<void> launch(const std::shared_ptr<Env>& env, std::wstring_view exePath, std::wstring_view params) const;
		coro::LazyTask<void> launchInternal(std::shared_ptr<Env> env, std::wstring exePath, std::wstring params) const;
		// UI 入口：启动 + 轮询兜底（慢电脑/注入失败时提示并允许重试）
		coro::LazyTask<void> launchWithPoll(std::shared_ptr<Env> env, std::wstring exePath, std::wstring params);
		// 启动后轮询目标应用进程是否出现（约 60 秒），超时弹窗提示并可重试
		coro::LazyTask<void> pollTargetProcess(std::shared_ptr<Env> env, std::wstring exePath, std::wstring params);
		// 系统中是否存在指定 exe 文件名的进程
		static bool isProcessRunning(std::wstring_view exeName);

	private:
		sched::SingleThreadContext m_execCtx;
		coro::AsyncScope m_asyncScope;
		// 退出时取消正在进行的启动轮询（pollTargetProcess），防止析构 join() 阻塞。
		// 不能用 std::nostopstate：无共享状态时 request_stop() 是空操作，取消失效。
		std::stop_source m_stopSource{};
	};
}
