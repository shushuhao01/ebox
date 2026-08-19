// ReSharper disable CppUnusedIncludeDirective
#pragma once
#ifndef _HOOK_CACHE_H_
#define _HOOK_CACHE_H_

// 多开隔离查询的进程级异步 TTL 缓存。
//
// 背景: 注入 DLL 的窗口/进程隔离 hook 原本"每次 API 调用都同步 RPC"到 eBox 宿主进程,
// 20 个环境 x 每环境 10~20 个进程同时高频调用时, 唯一的 RPC 端点成为瓶颈,
// 输入法/应用 UI 线程被 ncalrpc 往返阻塞, 表现为打字卡顿、窗口无响应。
// 第一版改为"TTL 内首次同步拉取"后, 慢路径仍会阻塞调用线程, 且多环境并发时
// 拉取失败会保守返回 true, 导致本环境自己的窗口被过滤(扫码窗口不显示)。
//
// 本版彻底异步化:
//   - 查询永远只读本地集合(共享锁, 无任何 RPC / 阻塞);
//   - 由进程内一个懒启动的后台线程每 200ms 检查 TTL 并刷新全量集合;
//   - 首次集合未就绪时返回 false(放行), 避免误杀本环境窗口, 250ms 内刷新完成。
// 隔离语义与原来一致(过滤"其他环境"的窗口/进程), 仅引入极短的缓存延迟。
//
// 兼容性: 仅使用 Win7+ 可用 API(GetTickCount64 / SRWLOCK / CreateThread)。

#ifndef NOMINMAX
#define NOMINMAX  // 防 windows.h 的 min/max 宏经 header unit 泄漏，破坏 std::min/std::max
#endif
#include <windows.h>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include <thread>

namespace hook_cache
{
	// 窗口集合缓存 TTL(毫秒): 其他环境窗口增删后最多约 700ms 内生效
	inline constexpr ULONGLONG kWndTtlMs = 500;
	// 进程集合缓存 TTL(毫秒): 其他环境进程增删后最多约 1200ms 内生效
	inline constexpr ULONGLONG kProcTtlMs = 1000;
	// 后台刷新周期(毫秒)
	inline constexpr ULONGLONG kRefreshIntervalMs = 200;

	using WndFetcher  = bool (*)(std::unordered_set<HWND>& out);
	using ProcFetcher = bool (*)(std::unordered_set<ULONG_PTR>& out);

	// 内部状态(命名以下划线开头, 勿在模块外直接使用)
	// 容器改为"new 分配 + 永不析构"(见 wnd_set/proc_set)：注入 DLL 会运行在
	// 短命中转进程(如 cmd)与反射注入子进程(WXWorkWeb/crashpad)中；若容器是普通
	// 静态对象, 进程退出时 CRT 静态析构会与后台刷新线程(detach, 可能阻塞在
	// ncalrpc 上, 该 RPC 无调用超时)并发, 导致 use-after-free(实测 cmd.exe 崩溃:
	// 0xc0000005, 崩溃点在 unordered_set 桶定位)。永不析构后由操作系统在进程
	// 退出时回收, 消除该竞态。
	// 注意：不能用函数内 static(magic static)——反射注入下多线程首次访问时 MSVC
	// 的 _Init_thread_header 初始化机制不可靠, 实测 WXWorkWeb 运行时崩溃(同样是
	// unordered_set 桶定位)。改为 InterlockedCompareExchangePointer 一次性发布,
	// 无 CRT 依赖、反射注入安全、线程安全。
	inline SRWLOCK g_wndLock = SRWLOCK_INIT;
	inline SRWLOCK g_procLock = SRWLOCK_INIT;
	inline std::atomic<ULONGLONG> g_wndLoadedAt{0};
	inline std::atomic<ULONGLONG> g_procLoadedAt{0};
	inline WndFetcher g_wndFetcher = nullptr;
	inline ProcFetcher g_procFetcher = nullptr;
	inline std::unordered_set<HWND>* g_wndSetPtr = nullptr;
	inline std::unordered_set<ULONG_PTR>* g_procSetPtr = nullptr;

	inline std::unordered_set<HWND>& wnd_set()
	{
		if (g_wndSetPtr == nullptr)
		{
			auto* fresh = new std::unordered_set<HWND>;
			if (::InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&g_wndSetPtr),
			                                        fresh, nullptr) != nullptr)
			{
				delete fresh; // 其他线程抢先发布，释放本线程临时对象
			}
		}
		return *g_wndSetPtr;
	}

	inline std::unordered_set<ULONG_PTR>& proc_set()
	{
		if (g_procSetPtr == nullptr)
		{
			auto* fresh = new std::unordered_set<ULONG_PTR>;
			if (::InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&g_procSetPtr),
			                                        fresh, nullptr) != nullptr)
			{
				delete fresh;
			}
		}
		return *g_procSetPtr;
	}

	// 由各 hook 模块在初始化时注册"全量拉取"回调(内部走 RPC, 仅在后台线程调用)
	inline void set_wnd_fetcher(WndFetcher f)  { g_wndFetcher = f; }
	inline void set_proc_fetcher(ProcFetcher f) { g_procFetcher = f; }

	// 后台刷新循环: 仅在 TTL 过期时拉取; 拉取失败保留旧数据(首查时短暂放行)
	inline void refresh_loop()
	{
		for (;;)
		{
			::Sleep(kRefreshIntervalMs);
			const ULONGLONG now = ::GetTickCount64();

			// 窗口集合
			if (g_wndLoadedAt.load(std::memory_order_acquire) == 0
				|| (now - g_wndLoadedAt.load(std::memory_order_relaxed)) >= kWndTtlMs)
			{
				if (WndFetcher fetcher = g_wndFetcher)
				{
					std::unordered_set<HWND> fresh;
					if (fetcher(fresh))
					{
						AcquireSRWLockExclusive(&g_wndLock);
						wnd_set().swap(fresh);
						g_wndLoadedAt.store(::GetTickCount64(), std::memory_order_release);
						ReleaseSRWLockExclusive(&g_wndLock);
					}
				}
			}

			// 进程集合
			if (g_procLoadedAt.load(std::memory_order_acquire) == 0
				|| (now - g_procLoadedAt.load(std::memory_order_relaxed)) >= kProcTtlMs)
			{
				if (ProcFetcher fetcher = g_procFetcher)
				{
					std::unordered_set<ULONG_PTR> fresh;
					if (fetcher(fresh))
					{
						AcquireSRWLockExclusive(&g_procLock);
						proc_set().swap(fresh);
						g_procLoadedAt.store(::GetTickCount64(), std::memory_order_release);
						ReleaseSRWLockExclusive(&g_procLock);
					}
				}
			}
		}
	}

	// 懒启动后台刷新线程(首次查询时触发, 此时不在 DllMain 上下文, 创建线程安全)
	inline void ensure_refresh_thread()
	{
		static std::once_flag s_once;
		std::call_once(s_once, []
		{
			HANDLE hThread = ::CreateThread(nullptr, 0, [](LPVOID) -> DWORD
			{
				refresh_loop();
				return 0;
			}, nullptr, 0, nullptr);
			if (hThread)
			{
				::CloseHandle(hThread); // detach: 进程退出时由系统回收
			}
		});
	}

	// 查询 hwnd 是否属于其他环境(只读本地集合, 永不阻塞)
	inline bool window_other(HWND hwnd)
	{
		ensure_refresh_thread();
		AcquireSRWLockShared(&g_wndLock);
		const bool hit = (wnd_set().find(hwnd) != wnd_set().end());
		ReleaseSRWLockShared(&g_wndLock);
		return hit;
	}

	// 查询 pid 是否属于其他环境(只读本地集合, 永不阻塞)
	inline bool process_other(ULONG_PTR pid)
	{
		ensure_refresh_thread();
		AcquireSRWLockShared(&g_procLock);
		const bool hit = (proc_set().find(pid) != proc_set().end());
		ReleaseSRWLockShared(&g_procLock);
		return hit;
	}
}
#endif
