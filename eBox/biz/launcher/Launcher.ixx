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
	};
}
