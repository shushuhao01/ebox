module;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

export module Scheduler:EventLoop;

import std;
import "sys_defs.h";
import :TimedQueue;

namespace sched
{
	// UI 卡死 watchdog 的前置声明（实现位于本模块末尾）：供 EventLoopForWinUi::run 调用
	export void hang_watchdog_kick();
	export void hang_watchdog_mark(const wchar_t* op);
	export void hang_watchdog_ensure_started();

	template <typename DerivedT>
	class EventLoopBase
	{
	public:
		using Task = std::move_only_function<void()>;
		using TaskList = std::list<Task>;

		void addTask(Task task)
		{
			{
				std::lock_guard guard(m_queueLock);
				m_timeQueue.addTask(std::move(task));
			}
			static_cast<DerivedT*>(this)->notify();
		}

		void addTimer(std::chrono::steady_clock::time_point expireTime, Task task)
		{
			{
				std::lock_guard guard(m_queueLock);
				m_timeQueue.addTimer(expireTime, std::move(task));
			}
			static_cast<DerivedT*>(this)->notify();
		}

		void addTimer(std::chrono::steady_clock::time_point expireTime, Task task, std::stop_token cancellationToken)
		{
			{
				std::lock_guard guard(m_queueLock);
				m_timeQueue.addTimerWithCancellation(expireTime, std::move(task), std::move(cancellationToken));
			}
			static_cast<DerivedT*>(this)->notify();
		}

		template <typename Rep, typename Period>
		void addTimer(std::chrono::duration<Rep, Period> duration, Task task)
		{
			{
				std::lock_guard guard(m_queueLock);
				m_timeQueue.addTimer(duration, std::move(task));
			}
			static_cast<DerivedT*>(this)->notify();
		}

		template <typename Rep, typename Period>
		void addTimer(std::chrono::duration<Rep, Period> duration, Task task, std::stop_token cancellationToken)
		{
			{
				std::lock_guard guard(m_queueLock);
				m_timeQueue.addTimerWithCancellation(duration, std::move(task), std::move(cancellationToken));
			}
			static_cast<DerivedT*>(this)->notify();
		}

		template <typename Rep, typename Period>
		void addPeriodicTimer(std::chrono::duration<Rep, Period> duration, Task task)
		{
			{
				auto periodicTimer = [this, duration, task = std::move(task)]() mutable
				{
					task();
					addPeriodicTimer(duration, std::move(task));
				};
				std::lock_guard guard(m_queueLock);
				m_timeQueue.addTimer(duration, std::move(periodicTimer));
			}
			static_cast<DerivedT*>(this)->notify();
		}

		template <typename Rep, typename Period>
		void addPeriodicTimer(std::chrono::duration<Rep, Period> duration, Task task, std::stop_token cancellationToken)
		{
			{
				auto periodicTimer = [this, duration, task = std::move(task), token = cancellationToken]() mutable
				{
					// 能触发就说明刚刚判断过stop_requested,这里没必要再次判断
					task();
					// 执行完任务后需要再次判断
					if (!token.stop_requested())
					{
						addPeriodicTimer(duration, std::move(task), std::move(token));
					}
				};
				std::lock_guard guard(m_queueLock);
				m_timeQueue.addTimerWithCancellation(duration, std::move(periodicTimer), std::move(cancellationToken));
			}
			static_cast<DerivedT*>(this)->notify();
		}

	protected:
		std::mutex m_queueLock{};
		MultiTimingWheel m_timeQueue{};
	};

	export class EventLoopForWinUi : public EventLoopBase<EventLoopForWinUi>
	{
	public:
		EventLoopForWinUi()
		{
			m_hNotifyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (!m_hNotifyEvent)
			{
				throw std::runtime_error("CreateEventW failed");
			}
		}

		~EventLoopForWinUi()
		{
			CloseHandle(m_hNotifyEvent);
		}

		void run()
		{
			// 启动 UI 卡死 watchdog：独立线程监测 UI 心跳，停滞超时自动转储 .dmp/.txt
			// 供开发端定位卡死调用栈；仅在卡死时写一次文件，正常运行零开销、不干扰主应用。
			hang_watchdog_ensure_started();
			MSG msg{}; // 显式零初始化，避免首次 PeekMessageW 前读到未初始化成员（UB）
			TaskList tasks;
			// 只能通过WM_QUIT窗口消息来结束run
			while (true)
			{
				hang_watchdog_kick();
				handleTasks(tasks);

				// 优先处理完所有窗口消息
				while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
				{
					if (msg.message == WM_QUIT)
					{
						break;
					}
					TranslateMessage(&msg);
					DispatchMessageW(&msg);
				}

				for (auto it = tasks.begin(); it != tasks.end();)
				{
					if (auto& func = *it)
					{
						func();
					}
					it = tasks.erase(it);
				}

				if (msg.message == WM_QUIT)
				{
					return;
				}
			}
		}

		void finish() const noexcept
		{
			PostQuitMessage(0);
			SetEvent(m_hNotifyEvent);
		}

	private:
		friend EventLoopBase;

		void handleTasks(TaskList& out)
		{
			std::unique_lock lock(m_queueLock);

			DWORD dwMilliseconds = INFINITE;
			const auto [bHasTask, tp] = m_timeQueue.nextTaskTimePoint();
			if (bHasTask)
			{
				auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(tp - std::chrono::steady_clock::now()).count();
				if (delay < 0)
				{
					delay = 0;
				}
				dwMilliseconds = static_cast<DWORD>(delay);
			}

			lock.unlock();
			const DWORD result = MsgWaitForMultipleObjects(1, &m_hNotifyEvent, FALSE, dwMilliseconds,QS_ALLINPUT);
			lock.lock();

			if (result == WAIT_TIMEOUT)
			{
				m_timeQueue.advanceUntil(tp);
			}
			out = m_timeQueue.pull();
		}

		void notify() const
		{
			SetEvent(m_hNotifyEvent);
		}

	private:
		HANDLE m_hNotifyEvent;
	};

	export class EventLoop : public EventLoopBase<EventLoop>
	{
	public:
		void run()
		{
			TaskList tasks;
			bool bContinue = true;
			while (bContinue)
			{
				bContinue = handleTasks(tasks);

				for (auto it = tasks.begin(); it != tasks.end();)
				{
					if (auto& func = *it)
					{
						func();
					}
					it = tasks.erase(it);
				}
			}
		}

		bool handleTasks(TaskList& out)
		{
			std::lock_guard guard(m_queueLock);
			if (m_bStopped)
			{
				return false;
			}
			const auto [bHasTask, tp] = m_timeQueue.nextTaskTimePoint();
			if (bHasTask)
			{
				if (m_cv.wait_until(m_queueLock, tp) == std::cv_status::timeout)
				{
					m_timeQueue.advanceUntil(tp);
				}
			}
			else
			{
				m_cv.wait(m_queueLock);
			}
			out = m_timeQueue.pull();
			return !m_bStopped;
		}

		void finish()
		{
			std::lock_guard guard(m_queueLock);
			m_bStopped = true;
			m_cv.notify_all();
		}

	private:
		friend EventLoopBase;

		void notify()
		{
			m_cv.notify_one();
		}

	private:
		std::condition_variable_any m_cv{};
		bool m_bStopped = false;
	};

	// ========================= UI 卡死 watchdog =========================
	// 机制：UI 事件循环每迭代调用 hang_watchdog_kick() 使心跳自增；watchdog 独立线程
	// 周期检查心跳，若超过阈值未变化则判定 UI 线程卡死，写一份完整 minidump(.dmp) 与
	// 说明文本(.txt) 到 %LOCALAPPDATA%\eBox\dumps\。开发端可用同版本 PDB 对 .dmp 做
	// 符号化，精确定位卡死调用栈。仅卡死时写一次，正常运行零开销、不依赖 UI 线程。
	namespace hangdetail
	{
		std::atomic<unsigned long long> g_uiHeartbeat{0};
		std::atomic<const wchar_t*> g_lastOp{nullptr};
		std::atomic<unsigned long> g_lastOpHash{0};
		std::atomic<DWORD> g_uiThreadId{0};
		std::atomic<bool> g_dumped{false};
		std::optional<std::jthread> g_watchdog{};

		std::filesystem::path dumpDir()
		{
			std::filesystem::path dir;
			wchar_t buf[MAX_PATH]{};
			const DWORD len = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
			if (len > 0 && len < MAX_PATH)
			{
				dir = std::filesystem::path(buf) / L"eBox" / L"dumps";
			}
			else
			{
				dir = std::filesystem::temp_directory_path() / L"eBox" / L"dumps";
			}
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			return dir;
		}

		std::wstring timestamp()
		{
			SYSTEMTIME st{};
			::GetLocalTime(&st);
			return std::format(L"{:04}{:02}{:02}-{:02}{:02}{:02}",
			                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		}

		std::wstring lastOpText()
		{
			const auto hash = g_lastOpHash.load(std::memory_order_relaxed);
			const auto* p = g_lastOp.load(std::memory_order_relaxed);
			// 文本快照无法跨线程安全读取 wchar_t*（可能在卡死时被并发修改），故用哈希+长度近似。
			// 这里仅给出操作类别提示；精确栈仍以 .dmp 为准。
			if (hash == 0)
			{
				return L"(无)";
			}
			if (p)
			{
				// 逐字符读，异常时截断到合理长度，尽力还原最近一次操作描述
				std::wstring s;
				for (int i = 0; i < 128 && p[i] != 0; ++i)
				{
					s.push_back(p[i]);
				}
				return s;
			}
			return L"(未知)";
		}

		void writeHangDump()
		{
			const std::wstring base = (dumpDir() / (L"eBox-hang-" + timestamp())).wstring();
			const std::wstring dmpPath = base + L".dmp";
			const std::wstring txtPath = base + L".txt";

			{
				std::wofstream out(txtPath, std::ios::binary);
				out << L"eBox UI 卡死转储信息\n";
				out << L"发生时间 : " << timestamp() << L"\n";
				out << L"进程PID  : " << ::GetCurrentProcessId() << L"\n";
				out << L"UI线程ID : " << g_uiThreadId.load(std::memory_order_relaxed) << L"\n";
				out << L"最后操作 : " << lastOpText() << L"\n";
				out << L"转储文件 : " << dmpPath << L"\n";
				out << L"\n";
				out << L"请把本 .txt 与同名的 .dmp 一起发给开发者，即可定位卡死位置。\n";
				out.close();
			}

			const HANDLE hFile = ::CreateFileW(dmpPath.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile != INVALID_HANDLE_VALUE)
			{
				::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), hFile, MiniDumpNormal, nullptr, nullptr, nullptr);
				::CloseHandle(hFile);
			}
		}

		void watchdogLoop(std::stop_token st)
		{
			using namespace std::chrono_literals;
			unsigned long long lastHeartbeat = g_uiHeartbeat.load(std::memory_order_relaxed);
			auto lastChange = std::chrono::steady_clock::now();

			while (!st.stop_requested())
			{
				std::this_thread::sleep_for(200ms);
				const auto now = std::chrono::steady_clock::now();
				const auto cur = g_uiHeartbeat.load(std::memory_order_relaxed);
				if (cur != lastHeartbeat)
				{
					lastHeartbeat = cur;
					lastChange = now;
					g_dumped.store(false, std::memory_order_relaxed); // 恢复跳动，允许再次取证
				}
				else if (now - lastChange > std::chrono::seconds(4))
				{
					if (!g_dumped.exchange(true, std::memory_order_relaxed))
					{
						writeHangDump();
					}
				}
			}
		}
	}

	export void hang_watchdog_kick()
	{
		hangdetail::g_uiHeartbeat.fetch_add(1, std::memory_order_relaxed);
	}

	export void hang_watchdog_mark(const wchar_t* op)
	{
		hangdetail::g_lastOp.store(op, std::memory_order_relaxed);
		// 记录哈希备查：若读取时指针已被并发释放，也可据此确认“确有一次操作被标记”
		hangdetail::g_lastOpHash.store(op ? (unsigned long)std::hash<std::wstring_view>{}(op) : 0,
		                              std::memory_order_relaxed);
	}

	export void hang_watchdog_ensure_started()
	{
		static std::once_flag flag;
		std::call_once(flag, []
		{
			// 此处运行在 UI 主循环线程上，记录其线程 ID 供转储说明使用
			hangdetail::g_uiThreadId.store(::GetCurrentThreadId(), std::memory_order_relaxed);
			hangdetail::g_watchdog.emplace([](std::stop_token st) { hangdetail::watchdogLoop(std::move(st)); });
		});
	}
}
