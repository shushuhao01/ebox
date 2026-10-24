module;
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Rstrtmgr.lib")
// Restart Manager：迁移时定位"谁占用着这个文件"，用于结束后自动继续复制
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <restartmanager.h>
#include <tlhelp32.h>
module MainApp;

import std;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import UI.MainWindow;
import EssentialData;
import Biz.Core;
import biz.License;

namespace
{
	void clear_quit_msg() noexcept
	{
		MSG msg;
		PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
	}

	// 进程存续期间持有单实例互斥句柄（防止再次启动的 eBox 抢占 RPC 端点）
	HANDLE g_singleInstanceMutex{nullptr};

	// 见下方实现：等待 --wait-another-end 指定的旧实例退出（须在单实例互斥检查前调用）
	void wait_previous_instance_end(std::wstring_view cmdLine);

	// 见下方实现：执行上次挂起的环境数据目录迁移（须在 MainApp 构造前调用）
	void run_pending_data_dir_migration();
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ [[maybe_unused]] HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	try
	{
		// 退出/重启接力：新实例必须等旧实例完全退出后再抢单实例互斥，
		// 否则会误报"eBox 已在运行"，且旧实例仍持有 Env\data\eBox hive 句柄导致迁移失败。
		wait_previous_instance_end(lpCmdLine);

		// 单实例保护：多个 eBox 并存会弹"eBox已经运行"框、争抢唯一 RPC 端点，导致环境进程 login 卡顿。
		// 全局命名空间(Global\)需要 SeCreateGlobalPrivilege（默认仅管理员/服务拥有），
		// 标准用户创建会失败；此时退化为会话级名称(Local\)，同一会话内仍可互斥。
		{
			HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"Global\\eBox_SingleInstance");
			if (!hMutex)
			{
				hMutex = CreateMutexW(nullptr, FALSE, L"eBox_SingleInstance");
			}
			if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS)
			{
				MessageBoxW(nullptr, L"eBox 已在运行！\n请勿重复打开。", MainApp::appName.data(), MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
				CloseHandle(hMutex);
				return 0;
			}
			g_singleInstanceMutex = hMutex;
		}

		biz::init_system_version_info();

		// 一键切换数据目录：上次运行挂起的迁移必须在 MainApp 构造（加载 Env\data\eBox hive）之前完成
		run_pending_data_dir_migration();

		MainApp app{hInstance, lpCmdLine, nCmdShow};
		g_app = &app;

		biz::init_resource_info();
		biz::Core biz;

		ui::MainWindow mainWnd;
		ui::g_main_wnd = &mainWnd;

		mainWnd.show(app.cmdShow());

		// 联网授权：启动后台心跳线程（作废/过期/强制下线即时生效）。
		// 纯离线激活码不受影响（服务器未登记，心跳返回 offline 后自动停止）。
		biz::license::startHeartbeatLoop();

		// 系统公告实时推送：启动 SSE 长连接监听线程（与心跳独立，心跳周期下发公告作为兜底）。
		biz::license::startNoticeStream();

		app.runMessageLoop();

		// 通知心跳线程尽快结束（不 join）：若其正阻塞在同步 HTTP 请求，
		// 等待会拖慢退出。真正退出由函数末尾的 ExitProcess 兜底（跳过静态析构）。
		biz::license::requestStopHeartbeat();
		// 同理通知公告推送线程尽快结束（不 join）。
		biz::license::requestStopNoticeStream();
	}
	catch (const std::exception& e)
	{
		// 由于app的析构会触发PostQuitMessage
		// 所以需要在 MessageBox 前清除WM_QUIT消息，否则MessageBox窗口会立即销毁并返回
		clear_quit_msg();
		std::string_view msg = e.what();
		if (!msg.empty())
		{
			MessageBoxA(nullptr, msg.data(), MainApp::appNameA.data(), MB_OK | MB_ICONERROR | MB_TASKMODAL);
		}
	}
	catch (...)
	{
		clear_quit_msg();
		MessageBoxW(nullptr, L"Unknown error", MainApp::appName.data(), MB_OK | MB_ICONERROR | MB_TASKMODAL);
	}
	// 栈对象（MainWindow/Core/MainApp 及全部 UI 树）已完成析构，环境 hive 已保存、
	// RPC 服务器已关闭。静态存储期对象（如心跳 jthread）的析构会在 main 返回后
	// join 阻塞（心跳 HTTP 请求挂起时最多可达数十秒，表现为"退出残留进程"），
	// 因此直接 ExitProcess 跳过静态析构，保证进程立即、干净退出。
	ExitProcess(0);
}

namespace
{
	// ---- 环境数据目录切换：注册表读写（HKCU\Software\eBox）----
	constexpr wchar_t DATA_DIR_REG_KEY[] = L"Software\\eBox";
	constexpr wchar_t REG_VALUE_DATA_DIR[] = L"DataDir";
	constexpr wchar_t REG_VALUE_PENDING_FROM[] = L"PendingDataDirFrom";
	constexpr wchar_t REG_VALUE_PENDING_TO[] = L"PendingDataDirTo";

	std::wstring read_reg_value(const wchar_t* name)
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, DATA_DIR_REG_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		{
			return {};
		}
		wchar_t buf[1024]{};
		DWORD bufSize = sizeof(buf);
		const LSTATUS st = RegQueryValueExW(hKey, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &bufSize);
		RegCloseKey(hKey);
		return st == ERROR_SUCCESS ? std::wstring{buf} : std::wstring{};
	}

	bool write_reg_value(const wchar_t* name, const std::wstring& value)
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, DATA_DIR_REG_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
		{
			return false;
		}
		const LSTATUS st = RegSetValueExW(hKey, name, 0, REG_SZ,
		                                  reinterpret_cast<const BYTE*>(value.c_str()),
		                                  static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
		RegCloseKey(hKey);
		return st == ERROR_SUCCESS;
	}

	void delete_reg_value(const wchar_t* name)
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, DATA_DIR_REG_KEY, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
		{
			RegDeleteValueW(hKey, name);
			RegCloseKey(hKey);
		}
	}

	// 在单实例互斥检查之前等待「上一个 eBox 实例」退出。
	// 重启/切换数据目录时新实例由旧实例拉起，若先抢互斥会误报"eBox 已在运行"，
	// 且旧实例尚未释放 Env\data\eBox hive（RegLoadAppKeyW 独占），迁移会失败。
	void wait_previous_instance_end(std::wstring_view cmdLine)
	{
		constexpr std::wstring_view flag{L"--wait-another-end="};
		const std::size_t pos = cmdLine.find(flag);
		if (pos == std::wstring_view::npos)
		{
			return;
		}
		std::wstring_view value = cmdLine.substr(pos + flag.size());
		if (const std::size_t end = value.find_first_of(L" \t\""); end != std::wstring_view::npos)
		{
			value = value.substr(0, end);
		}
		DWORD pid = 0;
		try
		{
			pid = static_cast<DWORD>(std::stoul(std::wstring{value}));
		}
		catch (...)
		{
			return;
		}
		if (pid == 0 || pid == GetCurrentProcessId())
		{
			return;
		}

		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
		if (!hProcess)
		{
			return;
		}
		// 只等待"确实是同一个 exe"的进程，避免 PID 复用导致误等无关进程
		wchar_t selfPath[4096]{};
		const DWORD selfLen = GetModuleFileNameW(nullptr, selfPath, static_cast<DWORD>(std::size(selfPath)));
		wchar_t otherPath[4096]{};
		DWORD otherLen = static_cast<DWORD>(std::size(otherPath));
		if (selfLen > 0 &&
			QueryFullProcessImageNameW(hProcess, 0, otherPath, &otherLen) &&
			_wcsicmp(selfPath, otherPath) == 0)
		{
			WaitForSingleObject(hProcess, INFINITE);
		}
		CloseHandle(hProcess);
	}

	// ---- 启动期数据迁移 ----
	struct CopyTask
	{
		std::filesystem::path src;
		std::filesystem::path dst;
		std::uint64_t size{0};
	};

	constexpr unsigned int kMaxCopyThreads = 8;

	// 单个文件连续这么久没有任何字节进展就判定为"卡死"（多为杀软过滤驱动阻塞写入），
	// 由看门狗中断阻塞中的 I/O，再走"结束占用进程 → 重试"的恢复流程。
	constexpr ULONGLONG kCopyStallMs = 20000;

	// 每个复制线程独立的状态：进度按字节累加，卡住原因只写进自己的槽位，
	// 避免"某线程正在等待解锁"的提示被其它线程的复制成功清掉。
	struct CopySlot
	{
		std::atomic<std::uint64_t> transferred{0}; // 当前文件已传输字节（复制回调写入）
		std::atomic<std::uint64_t> attempt{0};     // 尝试序号（看门狗据此判断是否换了新尝试）
		std::atomic<bool> active{false};
		std::atomic<bool> cancel{false}; // 看门狗置位后复制回调返回 PROGRESS_CANCEL
		std::wstring file;               // 正在复制的文件（textMutex 保护）
		std::wstring waiting;            // 卡住待解锁的文件（textMutex 保护）
		std::wstring closing;            // 正在结束的占用程序（textMutex 保护）
		bool closingBlocked{false};      // true=占用者是系统关键进程，无法结束，只能等用户手动关闭
	};

	struct MigrationState
	{
		std::atomic<std::uint64_t> completed{0}; // 已复制完成的文件字节
		std::atomic<std::uint64_t> total{0};
		std::atomic<int> status{0}; // 0=进行中 1=成功 2=失败
		std::atomic<int> phase{0};  // 0=复制 1=校验 2=清理原目录 3=清理未完成的复制
		std::atomic<bool> stop{false};
		std::atomic<int> skippedCount{0}; // 复制时源文件已消失、按"无数据可复制"跳过的文件数
		// 删除原目录的进度（phase=2）：先统计文件数/字节数，删除时按文件逐个推进
		std::atomic<std::uint64_t> delTotalFiles{0};
		std::atomic<std::uint64_t> delTotalBytes{0};
		std::atomic<std::uint64_t> delFiles{0};
		std::atomic<std::uint64_t> delBytes{0};
		std::atomic<bool> delScanning{false}; // 正在统计待删除的文件（此期间进度条停在 0）
		std::atomic<bool> delKilling{false};  // 文件删不掉，正在结束占用它的程序
		std::wstring delFile;                 // 正在删除的文件（textMutex 保护）
		std::wstring error; // 仅 worker 线程写、join 之后主线程读（写时持 textMutex）
		std::mutex textMutex; // 保护 error 与各槽位的文本字段（worker 写、UI 线程读）
		std::array<CopySlot, kMaxCopyThreads> slots{};
		std::wstring sourceDir; // 源数据根目录（判定自有环境进程用，迁移开始前一次性写入）
		std::wstring shownText; // UI 线程侧缓存，避免重复刷新进度框文本
		std::wstring targetDir; // 目标目录（UI 线程侧，进度框文本用）
		int uiTick{0};          // UI 线程侧计数器，限制文本刷新频率
		int uiPhase{-1};        // UI 线程侧已显示的阶段
		bool oldDeleteFailed{false}; // 原目录未能完全删除
		std::wstring oldDeleteError; // 第一个删除失败的文件及原因（仅 worker 写、join 后主线程读）

		// 进度 = 已完成文件字节 + 各线程正在传输的字节。
		// 单个大文件复制期间也在持续推进，不会出现"卡在 99.x% 不动"。
		std::uint64_t done() const
		{
			std::uint64_t value = completed.load(std::memory_order_relaxed);
			for (const auto& slot : slots)
			{
				value += slot.transferred.load(std::memory_order_relaxed);
			}
			const std::uint64_t cap = total.load(std::memory_order_relaxed);
			return cap > 0 && value > cap ? cap : value;
		}
	};

	// std::error_code::message() 返回窄字符串，转成宽字符串后再拼进提示
	std::wstring narrow_to_wide(const std::string& s)
	{
		if (s.empty())
		{
			return {};
		}
		const int len = ::MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		if (len <= 0)
		{
			return {};
		}
		std::wstring out(static_cast<std::size_t>(len), L'\0');
		::MultiByteToWideChar(CP_ACP, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
		return out;
	}

	std::wstring win_error_text(DWORD err)
	{
		wchar_t* buf = nullptr;
		const DWORD len = ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
		                                       FORMAT_MESSAGE_IGNORE_INSERTS,
		                                   nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		                                   reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
		std::wstring text = (len > 0 && buf != nullptr) ? std::wstring{buf, len} : std::format(L"错误码 {}", err);
		if (buf != nullptr)
		{
			::LocalFree(buf);
		}
		while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
		{
			text.pop_back();
		}
		return text;
	}

	std::wstring format_size(std::uint64_t bytes)
	{
		const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
		if (mb >= 1024.0)
		{
			return std::format(L"{:.2f} GB", mb / 1024.0);
		}
		if (bytes < 1024ull * 1024ull)
		{
			return std::format(L"{} KB", (bytes + 1023ull) / 1024ull);
		}
		return std::format(L"{:.0f} MB", mb);
	}

	// 进度框里只显示文件名（完整路径太长，撑破对话框）
	std::wstring file_display_name(const std::wstring& path)
	{
		const auto slash = path.find_last_of(L"\\/");
		return slash == std::wstring::npos ? path : path.substr(slash + 1);
	}

	// 一次性枚举源目录生成复制计划（同时在目标侧建好子目录）。
	// 相对路径直接由字符串前缀截取，避免逐项 fs::relative 反复解析；总字节数顺带累加，
	// 省掉复制前对源目录的整盘扫描。
	bool build_copy_plan(const std::filesystem::path& fromRoot, const std::filesystem::path& toRoot,
	                     std::vector<CopyTask>& plan, MigrationState& state)
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		if (!fs::is_directory(fromRoot, ec))
		{
			return true; // Env/bin 可能本来就不存在
		}
		std::wstring fromPrefix = fromRoot.native();
		if (!fromPrefix.empty() && fromPrefix.back() != L'\\' && fromPrefix.back() != L'/')
		{
			fromPrefix.push_back(L'\\');
		}

		fs::recursive_directory_iterator it{fromRoot, fs::directory_options::skip_permission_denied, ec};
		const fs::recursive_directory_iterator end{};
		while (!ec && it != end)
		{
			const fs::path src = it->path();
			std::wstring rel = src.native();
			if (rel.size() > fromPrefix.size() && _wcsnicmp(rel.c_str(), fromPrefix.c_str(), fromPrefix.size()) == 0)
			{
				rel = rel.substr(fromPrefix.size());
			}
			else
			{
				std::error_code relEc;
				rel = fs::relative(src, fromRoot, relEc).native();
				if (relEc)
				{
					state.error = std::format(L"解析相对路径失败：{}", src.native());
					return false;
				}
			}
			const fs::path dst = toRoot / rel;

			std::error_code typeEc;
			if (it->is_directory(typeEc))
			{
				std::error_code mkEc;
				fs::create_directories(dst, mkEc);
				if (mkEc)
				{
					state.error = std::format(L"无法创建目标目录：{}\n{}", dst.native(), narrow_to_wide(mkEc.message()));
					return false;
				}
			}
			else if (it->is_regular_file(typeEc))
			{
				std::error_code sizeEc;
				const std::uint64_t size = it->file_size(sizeEc);
				if (sizeEc)
				{
					continue; // 读不到大小（临时文件已消失等）直接跳过
				}
				plan.push_back(CopyTask{src, dst, size});
				state.total += size;
			}
			it.increment(ec);
		}
		if (ec)
		{
			state.error = std::format(L"读取源目录失败：{}\n{}", fromRoot.native(), narrow_to_wide(ec.message()));
			return false;
		}
		return true;
	}

	struct FileLocker
	{
		DWORD pid{0};
		std::wstring imagePath; // 可能为空（权限不足/进程已退出）
	};

	// child 是否位于 dir 之内（大小写不敏感，按目录分隔符边界判断）
	bool path_within(const std::wstring& child, const std::wstring& dir)
	{
		if (child.empty() || dir.empty() || child.size() < dir.size())
		{
			return false;
		}
		if (_wcsnicmp(child.c_str(), dir.c_str(), dir.size()) != 0)
		{
			return false;
		}
		if (child.size() == dir.size())
		{
			return true;
		}
		const wchar_t next = child[dir.size()];
		return next == L'\\' || next == L'/';
	}

	// 用 Restart Manager 查出"谁正占用着这个文件"
	std::vector<FileLocker> find_file_lockers(const std::wstring& filePath)
	{
		std::vector<FileLocker> result;
		DWORD sessionHandle = 0;
		wchar_t sessionKey[CCH_RM_SESSION_KEY + 1]{};
		if (RmStartSession(&sessionHandle, 0, sessionKey) != ERROR_SUCCESS)
		{
			return result;
		}
		const wchar_t* resources[] = {filePath.c_str()};
		if (RmRegisterResources(sessionHandle, 1, resources, 0, nullptr, 0, nullptr) == ERROR_SUCCESS)
		{
			UINT needed = 0;
			UINT count = 0;
			DWORD reason = RmRebootReasonNone;
			DWORD rc = RmGetList(sessionHandle, &needed, &count, nullptr, &reason);
			if (rc == ERROR_MORE_DATA && needed > 0)
			{
				std::vector<RM_PROCESS_INFO> infos(needed);
				count = needed;
				rc = RmGetList(sessionHandle, &needed, &count, infos.data(), &reason);
				if (rc == ERROR_SUCCESS)
				{
					for (UINT i = 0; i < count; ++i)
					{
						FileLocker locker;
						locker.pid = infos[i].Process.dwProcessId;
						if (locker.pid == 0)
						{
							continue;
						}
						HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, locker.pid);
						if (hProcess)
						{
							wchar_t path[MAX_PATH * 2]{};
							DWORD size = static_cast<DWORD>(std::size(path));
							if (QueryFullProcessImageNameW(hProcess, 0, path, &size) && size > 0)
							{
								locker.imagePath.assign(path, size);
							}
							CloseHandle(hProcess);
						}
						result.push_back(std::move(locker));
					}
				}
			}
		}
		RmEndSession(sessionHandle);
		return result;
	}

	struct CloseWindowCtx
	{
		DWORD pid{0};
		int windows{0};
	};

	BOOL CALLBACK post_close_to_pid_cb(HWND hwnd, LPARAM lp)
	{
		auto* ctx = reinterpret_cast<CloseWindowCtx*>(lp);
		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (pid == ctx->pid)
		{
			PostMessageW(hwnd, WM_CLOSE, 0, 0);
			++ctx->windows;
		}
		return TRUE;
	}

	// 系统关键进程：即使持有文件也不能结束，否则会导致桌面/系统异常
	bool is_protected_process(const std::wstring& imagePath)
	{
		const auto slash = imagePath.find_last_of(L"\\/");
		const std::wstring name = slash == std::wstring::npos ? imagePath : imagePath.substr(slash + 1);
		static constexpr const wchar_t* kProtected[] = {
			L"system", L"system idle process", L"registry", L"memory compression", L"smss.exe", L"csrss.exe",
			L"wininit.exe", L"winlogon.exe", L"services.exe", L"lsass.exe", L"svchost.exe", L"dwm.exe",
			L"fontdrvhost.exe", L"explorer.exe", L"sihost.exe", L"taskhostw.exe", L"ctfmon.exe",
		};
		for (const wchar_t* protectedName : kProtected)
		{
			if (_wcsicmp(name.c_str(), protectedName) == 0)
			{
				return true;
			}
		}
		return false;
	}

	// 结束一个进程：先请求关闭窗口，宽限期内没退出就强制结束
	bool end_process(DWORD pid)
	{
		HANDLE hProcess =
			OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		if (!hProcess)
		{
			return false;
		}
		CloseWindowCtx ctx{pid, 0};
		EnumWindows(post_close_to_pid_cb, reinterpret_cast<LPARAM>(&ctx));
		bool ended = ctx.windows > 0 && WaitForSingleObject(hProcess, 600) == WAIT_OBJECT_0;
		if (!ended)
		{
			ended = TerminateProcess(hProcess, 0) != FALSE;
			WaitForSingleObject(hProcess, 1500);
		}
		CloseHandle(hProcess);
		return ended;
	}

	// 精确找出占用该文件的程序并强制结束；系统关键进程只提示不结束。
	// 占用是迁移失败的主要根因，因此这里以"结束"为默认动作，只有系统关键进程例外。
	// slot 非空时把"被谁占用/正在结束谁"写进该线程自己的槽位，供进度框实时展示。
	void kill_lockers(const std::wstring& filePath, MigrationState& state, CopySlot* slot)
	{
		const DWORD selfPid = GetCurrentProcessId();
		for (const FileLocker& locker : find_file_lockers(filePath))
		{
			if (locker.pid == selfPid || locker.pid <= 4)
			{
				continue;
			}
			const std::wstring display =
				locker.imagePath.empty() ? std::format(L"PID {}", locker.pid) : locker.imagePath;
			if (!locker.imagePath.empty() && is_protected_process(locker.imagePath))
			{
				if (slot != nullptr)
				{
					std::lock_guard lock{state.textMutex};
					slot->closingBlocked = true;
					slot->closing = display;
				}
				continue;
			}
			if (slot != nullptr)
			{
				std::lock_guard lock{state.textMutex};
				slot->closingBlocked = false;
				slot->closing = display;
			}
			end_process(locker.pid);
		}
	}

	// 强制结束映像位于 dir 内的全部进程（Env 下的环境子进程），彻底释放文件句柄。
	// 环境进程是 cmd.exe /c start 拉起的孤儿进程，不随 eBox 退出而退出，必须主动清理。
	void terminate_processes_under(const std::wstring& dir, MigrationState& state, CopySlot* slot)
	{
		if (dir.empty())
		{
			return;
		}
		const DWORD selfPid = GetCurrentProcessId();
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot == INVALID_HANDLE_VALUE)
		{
			return;
		}
		PROCESSENTRY32W entry{sizeof(entry)};
		if (Process32FirstW(snapshot, &entry))
		{
			do
			{
				if (entry.th32ProcessID == selfPid || entry.th32ProcessID <= 4)
				{
					continue;
				}
				HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
				if (!hProcess)
				{
					continue;
				}
				wchar_t imagePath[MAX_PATH * 2]{};
				DWORD size = static_cast<DWORD>(std::size(imagePath));
				const bool got = QueryFullProcessImageNameW(hProcess, 0, imagePath, &size) != FALSE;
				CloseHandle(hProcess);
				if (!got || !path_within(std::wstring{imagePath, size}, dir) ||
					is_protected_process(std::wstring{imagePath, size}))
				{
					continue;
				}
				if (slot != nullptr)
				{
					std::lock_guard lock{state.textMutex};
					slot->closingBlocked = false;
					slot->closing =
						std::format(L"{}（PID {}）", std::wstring{imagePath, size}, entry.th32ProcessID);
				}
				end_process(entry.th32ProcessID);
			} while (Process32NextW(snapshot, &entry));
		}
		CloseHandle(snapshot);
	}

	// CopyFileEx 的进度回调：把"当前文件已传输字节"写进本线程槽位，
	// 界面进度 = 已完成文件字节 + 各线程正在传输的字节，因此单个大文件复制期间也在推进。
	struct CopyProgressCtx
	{
		CopySlot* slot{nullptr};
	};

	DWORD CALLBACK copy_progress_routine(LARGE_INTEGER, LARGE_INTEGER totalTransferred, LARGE_INTEGER,
	                                     LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID data)
	{
		auto* ctx = static_cast<CopyProgressCtx*>(data);
		ctx->slot->transferred.store(static_cast<std::uint64_t>(totalTransferred.QuadPart),
		                             std::memory_order_relaxed);
		// 看门狗判定卡死后置位 cancel：主动中止这次复制，进入"结束占用进程 + 重试"流程
		return ctx->slot->cancel.load(std::memory_order_relaxed) ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
	}

	// 单文件复制：遇到共享冲突（环境子进程/杀软占用）时先重试，再强制结束占用程序后继续。
	// 被占用文件与处理中的进程写进本线程槽位，进度框实时向用户说明。
	bool copy_file_with_retry(const CopyTask& task, MigrationState& state, CopySlot& slot)
	{
		constexpr int maxAttempts = 40;      // 40 * 250ms ≈ 10 秒（不含结束占用进程的等待）
		constexpr int maxStallCancels = 3;   // 同一文件连续卡死 3 次就放弃，避免无限循环
		int stallCancels = 0;
		for (int attempt = 0;; ++attempt)
		{
			if (state.stop.load(std::memory_order_relaxed))
			{
				return false;
			}
			{
				std::lock_guard lock{state.textMutex};
				slot.file = task.src.native();
			}
			slot.transferred.store(0, std::memory_order_relaxed);
			slot.cancel.store(false, std::memory_order_relaxed);
			slot.attempt.fetch_add(1, std::memory_order_relaxed);

			CopyProgressCtx ctx{&slot};
			if (::CopyFileExW(task.src.c_str(), task.dst.c_str(), copy_progress_routine, &ctx, nullptr, 0))
			{
				// 先补记本文件字节、再清空本槽位的传输量，避免汇总时重复计数
				state.completed.fetch_add(task.size, std::memory_order_relaxed);
				slot.transferred.store(0, std::memory_order_relaxed);
				std::lock_guard lock{state.textMutex};
				slot.file.clear();
				slot.waiting.clear();
				slot.closing.clear();
				slot.closingBlocked = false;
				return true;
			}
			const DWORD err = ::GetLastError();
			// 看门狗中断的复制：按"卡死"处理，立即结束占用进程后重试
			const bool stalled = slot.cancel.load(std::memory_order_relaxed);
			// "路径/文件不存在"不是占用错误：可能是源文件在迁移过程中被其它程序删掉
			// （另一个实例的清理、杀软隔离），也可能是目标父目录被清理掉了。两种情况分别处理。
			if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
			{
				std::error_code srcEc;
				if (!std::filesystem::exists(task.src, srcEc))
				{
					// 源文件已消失：没有数据可复制，跳过（不计失败），并补记字节让进度继续推进
					state.completed.fetch_add(task.size, std::memory_order_relaxed);
					state.skippedCount.fetch_add(1, std::memory_order_relaxed);
					{
						std::lock_guard lock{state.textMutex};
						slot.file.clear();
						slot.waiting.clear();
					}
					return true;
				}
				// 源还在 → 是目标侧父目录缺失：重建后立即重试
				std::error_code mkEc;
				std::filesystem::create_directories(task.dst.parent_path(), mkEc);
				if (!mkEc)
				{
					continue;
				}
			}
			const bool retryable = err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION ||
			                       err == ERROR_ACCESS_DENIED || err == ERROR_USER_MAPPED_FILE ||
			                       err == ERROR_REQUEST_ABORTED || err == ERROR_OPERATION_ABORTED;
			if (stalled && ++stallCancels > maxStallCancels)
			{
				std::lock_guard lock{state.textMutex};
				state.error = std::format(
					L"复制文件失败：\n源文件：{}\n目标文件：{}\n{}\n\n"
					L"该文件长时间没有写入进展（常见原因：安全软件的实时防护\n"
					L"拦截了写入，或该文件被系统级进程独占）。\neBox 已多次强制结束能结束的占用进程。\n"
					L"请关闭安全软件等系统级程序后，重新打开 eBox 再切换磁盘。\n\n已保留原目录。",
					task.src.native(), task.dst.native(), win_error_text(err));
				return false;
			}
			if (!retryable || attempt + 1 >= maxAttempts)
			{
				std::lock_guard lock{state.textMutex};
				state.error = std::format(
					L"复制文件失败：\n源文件：{}\n目标文件：{}\n{}\n\n{}\n\n"
					L"请关闭安全软件等系统级程序后，重新打开 eBox 再切换磁盘。\n\n已保留原目录。",
					task.src.native(), task.dst.native(), win_error_text(err),
					retryable
						? L"该文件被其他程序长期占用。\neBox 已强制结束能结束的占用进程，但仍无法复制。"
						: L"这不是占用问题：系统拒绝读写该路径（目录不存在、权限不足或磁盘异常）。");
				return false;
			}
			{
				std::lock_guard lock{state.textMutex};
				if (slot.waiting.empty())
				{
					slot.waiting = task.src.native();
				}
			}
			// 卡死或多次失败：先精确结束占用该文件的程序，再清扫数据目录下的环境子进程
			if (stalled || attempt == 2 || attempt == 10 || attempt == 20)
			{
				kill_lockers(task.src.native(), state, &slot);
				terminate_processes_under(state.sourceDir, state, &slot);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}
	}

	void copy_plan(const std::vector<CopyTask>& plan, MigrationState& state)
	{
		if (plan.empty())
		{
			return;
		}
		// 文件复制是 I/O 密集：适度并行可明显加快"大量小文件"的迁移
		unsigned int threads = std::thread::hardware_concurrency();
		if (threads == 0)
		{
			threads = 4;
		}
		threads = std::clamp(threads, 2u, kMaxCopyThreads);
		if (plan.size() < 64)
		{
			threads = 1; // 文件很少时并行只有磁盘抖动，没有收益
		}

		std::atomic<std::size_t> next{0};
		std::atomic<bool> copying{true};
		// 各复制线程的线程句柄：卡死时交给看门狗 CancelSynchronousIo 中断阻塞中的 I/O
		std::array<std::atomic<HANDLE>, kMaxCopyThreads> handles{};

		std::vector<std::thread> pool;
		pool.reserve(threads);
		for (unsigned int index = 0; index < threads; ++index)
		{
			pool.emplace_back(
				[&plan, &state, &next, &handles, index]()
				{
					CopySlot& slot = state.slots[index];
					HANDLE raw = nullptr;
					if (::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(),
					                      &raw, THREAD_TERMINATE, FALSE, 0))
					{
						handles[index].store(raw);
					}
					slot.active.store(true, std::memory_order_relaxed);
					for (;;)
					{
						if (state.stop.load(std::memory_order_relaxed))
						{
							break;
						}
						const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
						if (i >= plan.size())
						{
							break;
						}
						if (!copy_file_with_retry(plan[i], state, slot))
						{
							state.stop = true;
							break;
						}
					}
					slot.active.store(false, std::memory_order_relaxed);
					std::lock_guard lock{state.textMutex};
					slot.file.clear();
					slot.waiting.clear();
					slot.closing.clear();
					slot.closingBlocked = false;
				});
		}

		// 看门狗：CopyFileEx 被过滤驱动（安全软件）阻塞时自身不会返回，重试循环也进不去，
		// 表现为"进度一直不动"。这里在"长时间没有任何字节进展"时中断该线程阻塞中的 I/O，
		// 让它带着错误返回，再交给重试逻辑去结束占用进程。
		std::thread watchdog{[&state, &copying, &handles, threads]()
		{
			std::array<std::uint64_t, kMaxCopyThreads> lastAttempt{};
			std::array<std::uint64_t, kMaxCopyThreads> lastBytes{};
			std::array<ULONGLONG, kMaxCopyThreads> lastChange{};
			while (copying.load(std::memory_order_relaxed))
			{
				::Sleep(1000);
				const ULONGLONG now = ::GetTickCount64();
				for (unsigned int i = 0; i < threads; ++i)
				{
					CopySlot& slot = state.slots[i];
					if (!slot.active.load(std::memory_order_relaxed))
					{
						lastAttempt[i] = 0;
						continue;
					}
					const std::uint64_t attempt = slot.attempt.load(std::memory_order_relaxed);
					const std::uint64_t bytes = slot.transferred.load(std::memory_order_relaxed);
					if (attempt != lastAttempt[i] || bytes != lastBytes[i])
					{
						lastAttempt[i] = attempt;
						lastBytes[i] = bytes;
						lastChange[i] = now;
						continue;
					}
					if (now - lastChange[i] < kCopyStallMs ||
						slot.cancel.exchange(true, std::memory_order_relaxed))
					{
						continue;
					}
					if (HANDLE h = handles[i].load(); h != nullptr)
					{
						::CancelSynchronousIo(h);
					}
				}
			}
		}};

		for (auto& t : pool)
		{
			t.join();
		}
		copying.store(false, std::memory_order_relaxed);
		watchdog.join();
		for (auto& h : handles)
		{
			if (HANDLE raw = h.exchange(nullptr); raw != nullptr)
			{
				::CloseHandle(raw);
			}
		}
	}

	// 校验：按计划逐项比对目标文件大小（不递归枚举，速度快）。
	// 源文件在迁移过程中已消失的项没有数据可校验，直接跳过（复制时已计数并会在完成提示里说明）。
	bool verify_copy_plan(const std::vector<CopyTask>& plan, std::wstring& error)
	{
		for (const auto& task : plan)
		{
			std::error_code ec;
			const std::uint64_t size = std::filesystem::file_size(task.dst, ec);
			if (!ec && size == task.size)
			{
				continue;
			}
			std::error_code srcEc;
			if (!std::filesystem::exists(task.src, srcEc) && !srcEc)
			{
				continue; // 源已消失：复制时已按"无数据可复制"跳过
			}
			error = std::format(L"数据目录迁移失败：校验未通过，文件未完整复制。\n{}\n\n已保留原目录。",
			                    task.dst.native());
			return false;
		}
		return true;
	}

	// 统计目录下待删除的文件数与总字节：删除阶段据此显示百分比进度
	void count_delete_tree(const std::filesystem::path& root, std::uint64_t& files, std::uint64_t& bytes)
	{
		std::error_code ec;
		const std::filesystem::recursive_directory_iterator end{};
		for (std::filesystem::recursive_directory_iterator it{
		         root, std::filesystem::directory_options::skip_permission_denied, ec};
		     it != end; it.increment(ec))
		{
			std::error_code entryEc;
			if (!it->is_regular_file(entryEc))
			{
				continue;
			}
			const std::uintmax_t size = it->file_size(entryEc);
			if (entryEc)
			{
				continue;
			}
			++files;
			bytes += size;
		}
	}

	// 删除单个文件：删不掉时按"被占用"处理——用 Restart Manager 查出占用该文件的程序
	// 并强制结束，然后重试；Restart Manager 查不到（常被安全软件拦截）时，再把原数据
	// 目录下的环境子进程清一遍（句柄通常被它们持有），最后再试。
	// 返回 true 表示文件已不存在（删除成功或本来就没有）。
	bool delete_file_forcing(const std::filesystem::path& path, MigrationState& state,
	                         const std::wstring& rootDir)
	{
		std::error_code rmEc;
		if (std::filesystem::remove(path, rmEc))
		{
			return true;
		}
		std::error_code existsEc;
		if (!std::filesystem::exists(path, existsEc))
		{
			return true; // 已被其它程序删掉
		}
		state.delKilling.store(true);
		kill_lockers(path.native(), state, nullptr);
		for (int i = 0; i < 10; ++i)
		{
			Sleep(200);
			if (std::filesystem::remove(path, rmEc))
			{
				state.delKilling.store(false);
				return true;
			}
			if (!std::filesystem::exists(path, existsEc))
			{
				state.delKilling.store(false);
				return true;
			}
		}
		terminate_processes_under(rootDir, state, nullptr);
		for (int i = 0; i < 15; ++i)
		{
			Sleep(200);
			if (std::filesystem::remove(path, rmEc))
			{
				state.delKilling.store(false);
				return true;
			}
			if (!std::filesystem::exists(path, existsEc))
			{
				state.delKilling.store(false);
				return true;
			}
		}
		state.delKilling.store(false);
		return false;
	}

	// 逐个删除目录内容，每删一个文件就推进进度并记录"正在删除"的文件名。
	// 先取出本层全部条目再删除：Windows 在枚举目录的同时删除文件可能漏枚举，
	// 会导致部分文件残留、原目录删不干净。
	// 删不掉的（通常是被占用）记进 failed，由调用方在最后统一重试一次。
	void delete_tree_reporting(const std::filesystem::path& dir, MigrationState& state,
	                           std::vector<std::filesystem::path>& failed, std::wstring& firstError,
	                           const std::wstring& rootDir)
	{
		std::vector<std::filesystem::path> entries;
		{
			std::error_code ec;
			const std::filesystem::directory_iterator end{};
			for (std::filesystem::directory_iterator it{
			         dir, std::filesystem::directory_options::skip_permission_denied, ec};
			     it != end && !ec; it.increment(ec))
			{
				entries.push_back(it->path());
			}
		}

		for (const auto& path : entries)
		{
			std::error_code typeEc;
			// 不跟随目录符号链接，避免把链接目标的内容删掉
			if (!std::filesystem::is_symlink(path, typeEc) &&
			    std::filesystem::is_directory(path, typeEc))
			{
				delete_tree_reporting(path, state, failed, firstError, rootDir);
				continue;
			}
			std::uint64_t size = 0;
			std::error_code sizeEc;
			const std::uintmax_t rawSize = std::filesystem::file_size(path, sizeEc);
			if (!sizeEc)
			{
				size = rawSize;
			}
			{
				std::lock_guard lock{state.textMutex};
				state.delFile = path.native();
			}
			if (!delete_file_forcing(path, state, rootDir))
			{
				if (firstError.empty())
				{
					std::error_code rmEc;
					std::filesystem::remove(path, rmEc);
					firstError = std::format(L"{}（{}）", path.native(), win_error_text(rmEc.value()));
				}
				failed.push_back(path);
				continue;
			}
			state.delFiles.fetch_add(1, std::memory_order_relaxed);
			state.delBytes.fetch_add(size, std::memory_order_relaxed);
		}
		// 内容已删空时连目录一起删（非空/被占用时失败，不影响后续）
		std::error_code dirEc;
		std::filesystem::remove(dir, dirEc);
	}

	HRESULT CALLBACK migration_dlg_callback(HWND hwnd, UINT msg, WPARAM, LPARAM, LONG_PTR lpRefData)
	{
		auto* state = reinterpret_cast<MigrationState*>(lpRefData);
		if (msg == TDN_CREATED)
		{
			SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 1000));
			SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_POS, 0, 0);
			SendMessageW(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR, FALSE, 0);
		}
		else if (msg == TDN_BUTTON_CLICKED)
		{
			// 迁移未完成时不允许关闭进度框：否则 worker 在后台继续复制、界面消失，
			// 用户会以为程序卡死；迁移必然结束（单文件重试有上限）。
			return state->status.load() == 0 ? S_FALSE : S_OK;
		}
		else if (msg == TDN_TIMER)
		{
			if (state->status.load() != 0)
			{
				// 迁移结束：关闭进度框（结果在 join 后统一处理）
				PostMessageW(hwnd, WM_CLOSE, 0, 0);
				return S_OK;
			}

			// 复制之后的校验、删除旧数据都没有字节进度：校验/清理切成"不确定进度"；
			// 删除原目录可提前统计文件数，用确定进度并显示正在删除的文件。
			const int phase = state->phase.load();
			if (phase != 0)
			{
				const bool phaseChanged = state->uiPhase != phase;
				if (phaseChanged)
				{
					state->uiPhase = phase;
					SendMessageW(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR, phase == 2 ? FALSE : TRUE, 0);
					state->shownText.clear();
				}
				if (phase == 2)
				{
					const std::uint64_t delTotal = state->delTotalBytes.load();
					const std::uint64_t delDone = state->delBytes.load();
					const std::uint64_t delFiles = state->delFiles.load();
					const std::uint64_t delTotalFiles = state->delTotalFiles.load();
					// 优先按字节推进；全是 0 字节文件时退化为按文件数推进
					int pos = 0;
					if (delTotal > 0)
					{
						pos = static_cast<int>(std::min<std::uint64_t>(1000, delDone * 1000 / delTotal));
					}
					else if (delTotalFiles > 0)
					{
						pos = static_cast<int>(std::min<std::uint64_t>(1000, delFiles * 1000 / delTotalFiles));
					}
					SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_POS, 0, pos);
					if (++state->uiTick % 3 != 0)
					{
						return S_OK;
					}
					std::wstring delFile;
					if (state->textMutex.try_lock())
					{
						delFile = state->delFile;
						state->textMutex.unlock();
					}
					std::wstring text;
					if (state->delScanning.load())
					{
						text = L"迁移完成，正在统计原目录中待删除的旧数据…";
					}
					else
					{
						text = std::format(L"迁移完成，正在删除原目录中的旧数据：{} / {}（{} / {} 个文件）",
						                   format_size(delDone), format_size(delTotal), delFiles, delTotalFiles);
						if (!delFile.empty())
						{
							text += std::format(L"\n\n正在删除：{}", file_display_name(delFile));
							if (state->delKilling.load())
							{
								text += L"\n该文件被占用，正在强制结束占用它的程序后继续删除…";
							}
						}
						text += std::format(L"\n\n原目录：{}", state->sourceDir);
					}
					if (text != state->shownText)
					{
						state->shownText = text;
						SendMessageW(hwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT,
						             reinterpret_cast<LPARAM>(text.c_str()));
					}
					return S_OK;
				}
				if (phaseChanged)
				{
					const std::wstring text = phase == 1 ? L"文件已复制完成，正在校验数据，请稍候…"
					                                      : L"迁移未完成，正在清理目标目录中的临时文件…";
					state->shownText = text;
					SendMessageW(hwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT,
					             reinterpret_cast<LPARAM>(text.c_str()));
				}
				return S_OK;
			}

			const std::uint64_t total = state->total.load();
			const std::uint64_t done = state->done();
			const int pos = total > 0 ? static_cast<int>(done * 1000 / total) : 0;
			SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_POS, 0, pos);

			// 进度条每 200ms 更新一次，说明文本每秒刷新一次：优先显示"卡在哪个文件、
			// 在结束哪个程序"，让用户知道为什么慢，而不是只看到数字不动。
			if (++state->uiTick % 5 != 0)
			{
				return S_OK;
			}
			std::wstring waiting;
			std::wstring closing;
			std::wstring copying;
			bool closingBlocked = false;
			int shownFiles = 0;
			if (state->textMutex.try_lock())
			{
				for (const auto& slot : state->slots)
				{
					if (closing.empty() && !slot.closing.empty())
					{
						closing = slot.closing;
						closingBlocked = slot.closingBlocked;
					}
					if (waiting.empty() && !slot.waiting.empty())
					{
						waiting = slot.waiting;
					}
					if (!slot.file.empty() && shownFiles < 3)
					{
						++shownFiles;
						if (!copying.empty())
						{
							copying += L"、";
						}
						copying += file_display_name(slot.file);
					}
				}
				state->textMutex.unlock();
			}
			std::wstring text;
			if (!closing.empty())
			{
				text = closingBlocked
					? std::format(L"文件被系统关键进程占用，无法自动结束，请手动关闭后会自动继续…\n{}\n\n{}",
					              closing, file_display_name(waiting))
					: std::format(L"正在强制结束占用文件的程序，结束后自动继续迁移…\n{}\n\n{}", closing,
					              file_display_name(waiting));
			}
			else if (!waiting.empty())
			{
				text = std::format(L"文件被占用，正在自动结束占用程序后继续…\n{}\n\n已复制 {} / {}",
				                   waiting, format_size(done), format_size(total));
			}
			else
			{
				text = std::format(L"已复制 {} / {}", format_size(done), format_size(total));
				if (!copying.empty())
				{
					text += std::format(L"\n\n正在复制：{}", copying);
				}
				text += std::format(L"\n\n目标目录：{}", state->targetDir);
				text += L"\n\n迁移期间请勿关闭电脑或强制结束进程。";
			}
			if (text != state->shownText)
			{
				state->shownText = text;
				SendMessageW(hwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(text.c_str()));
			}
		}
		return S_OK;
	}

	// 上次运行遗留的「数据目录迁移」挂起项：在加载环境 hive 之前完成搬迁。
	// 必须在 MainApp 构造之前执行（Env-Reg 会常驻持有 Env\data\eBox hive 句柄，运行期无法移动）。
	void run_pending_data_dir_migration()
	{
		namespace fs = std::filesystem;
		const std::wstring fromStr = read_reg_value(REG_VALUE_PENDING_FROM);
		const std::wstring toStr = read_reg_value(REG_VALUE_PENDING_TO);
		if (fromStr.empty() || toStr.empty())
		{
			if (!fromStr.empty() || !toStr.empty())
			{
				delete_reg_value(REG_VALUE_PENDING_FROM);
				delete_reg_value(REG_VALUE_PENDING_TO);
			}
			return;
		}

		std::error_code ec;
		const fs::path from = fs::weakly_canonical(fs::path{fromStr}, ec);
		const fs::path to = fs::weakly_canonical(fs::path{toStr}, ec);
		// cleanupPartial=true 时清掉目标侧已复制的一半数据：否则下次切换会因
		// "目标目录已存在 Env" 被拒绝，用户再也切换不回去。
		const auto abort = [&](const std::wstring& msg, bool cleanupPartial)
		{
			delete_reg_value(REG_VALUE_PENDING_FROM);
			delete_reg_value(REG_VALUE_PENDING_TO);
			if (cleanupPartial && !to.empty())
			{
				std::error_code rmEc;
				fs::remove_all(to / L"Env", rmEc);
				fs::remove_all(to / L"bin", rmEc);
			}
			MessageBoxW(nullptr, msg.c_str(), MainApp::appName.data(), MB_OK | MB_ICONERROR | MB_TASKMODAL);
		};

		if (from.empty() || to.empty())
		{
			abort(L"数据目录迁移失败：路径无效，已保留原目录。", false);
			return;
		}

		fs::create_directories(to, ec);
		if (ec)
		{
			abort(std::format(L"数据目录迁移失败：无法创建目标目录。\n{}\n\n已保留原目录。", to.native()), false);
			return;
		}

		MigrationState state;
		state.targetDir = to.native();
		state.sourceDir = from.native();

		// 迁移前先结束数据目录下的环境子进程：它们是 cmd /c start 拉起的孤儿进程，
		// 不随 eBox 退出而退出，常驻持有 Env 内的 hive/数据库文件，是迁移失败的主要根因。
		terminate_processes_under(state.sourceDir, state, nullptr);

		// 生成复制计划：一次枚举拿到全部文件与总大小（顺带在目标侧建目录）
		std::vector<CopyTask> plan;
		if (!build_copy_plan(from / L"Env", to / L"Env", plan, state) ||
			!build_copy_plan(from / L"bin", to / L"bin", plan, state))
		{
			abort(state.error.empty() ? std::wstring{L"数据目录迁移失败，已保留原目录。"} : state.error, false);
			return;
		}

		// 源目录是残骸（Env/bin 都不存在或全不可读）时，计划为空。此时若继续，
		// 会把 DataDir 指向空目录并删掉原目录 —— 等于丢数据。直接取消。
		if (plan.empty())
		{
			abort(L"数据目录迁移失败：源目录中没有可迁移的数据，已取消并保留原目录。", false);
			return;
		}

		// 大文件排前面：如果最后只剩一个大文件，即便有字节级进度，尾部也会拖很久；
		// 反过来让尾部变成大量小文件，收尾会很快。
		std::sort(plan.begin(), plan.end(),
		          [](const CopyTask& a, const CopyTask& b) { return a.size > b.size; });

		// 空间校验：目标盘剩余空间必须放得下
		if (!to.root_path().empty())
		{
			ULARGE_INTEGER freeBytes{};
			if (GetDiskFreeSpaceExW(to.root_path().c_str(), &freeBytes, nullptr, nullptr) &&
				freeBytes.QuadPart < state.total.load())
			{
				abort(std::format(L"数据目录迁移失败：目标磁盘剩余空间不足。\n需要：{}，可用：{}\n\n已保留原目录。",
				                  format_size(state.total.load()), format_size(freeBytes.QuadPart)),
				      true);
				return;
			}
		}

		// 复制失败时清掉目标侧已复制的一半数据：否则下次切换会因"目标目录已存在 Env"被拒绝。
		// 这一步也在 worker 里做（phase=3），几十 GB 的删除同样有进度提示，不会静默卡住。
		const auto fail_migration = [&state, &to](std::wstring msg)
		{
			state.phase = 3;
			std::error_code rmEc;
			fs::remove_all(to / L"Env", rmEc);
			fs::remove_all(to / L"bin", rmEc);
			{
				std::lock_guard lock{state.textMutex};
				state.error = std::move(msg);
			}
			state.status = 2;
		};

		// 复制、校验、删除旧数据全部在同一个 worker 里完成：进度框只有 worker 真正结束才关闭，
		// 避免"框已消失、后台还在删几十 GB 旧数据"的假死感。
		std::jthread worker{[&state, &plan, to, from, &fail_migration]
		{
			copy_plan(plan, state);
			if (state.stop.load())
			{
				fail_migration(state.error);
				return;
			}
			state.phase = 1; // 校验数据
			std::wstring verifyError;
			if (!verify_copy_plan(plan, verifyError))
			{
				fail_migration(std::move(verifyError));
				return;
			}
			// 复制完成才切换 DataDir（写失败同样视为迁移失败，避免"指向空目录"）
			if (!write_reg_value(REG_VALUE_DATA_DIR, to.native()))
			{
				fail_migration(L"数据目录迁移失败：写入配置失败，已保留原目录。");
				return;
			}
			// 防误删提示文件
			const fs::path tipFile = to / L"请勿删除此文件夹.txt";
			std::error_code tipEc;
			if (!fs::exists(tipFile, tipEc))
			{
				std::ofstream ofs{tipFile, std::ios::binary | std::ios::trunc};
				if (ofs)
				{
					ofs << "\xEF\xBB\xBF"; // UTF-8 BOM
					ofs << "【重要】此文件夹是 eBox 多开环境的数据目录（存放 QYWX/WX 等应用的环境数据）。\n"
					       "请勿删除、移动或重命名本文件夹，否则所有环境（登录状态/聊天记录/配置）将全部丢失！\n";
				}
			}
			// 迁移成功后立刻删除原目录数据。这一步同样可能很慢（几十 GB、几万个文件），
			// 所以先统计文件数/字节数，再逐文件删除并实时推进进度（与复制阶段一致的体验）。
			state.phase = 2;
			state.delScanning = true;
			{
				std::lock_guard lock{state.textMutex};
				state.delFile.clear();
			}
			std::uint64_t delTotalFiles = 0;
			std::uint64_t delTotalBytes = 0;
			count_delete_tree(from / L"Env", delTotalFiles, delTotalBytes);
			count_delete_tree(from / L"bin", delTotalFiles, delTotalBytes);
			state.delTotalFiles.store(delTotalFiles);
			state.delTotalBytes.store(delTotalBytes);
			state.delScanning = false;

			std::vector<fs::path> failedFiles;
			std::wstring firstDeleteError;
			const std::wstring rootDir = from.native();
			delete_tree_reporting(from / L"Env", state, failedFiles, firstDeleteError, rootDir);
			delete_tree_reporting(from / L"bin", state, failedFiles, firstDeleteError, rootDir);
			if (!failedFiles.empty())
			{
				// 逐项再试：delete_file_forcing 会查出占用者并强制结束进程后重试
				std::vector<fs::path> stillFailed;
				for (const auto& path : failedFiles)
				{
					{
						std::lock_guard lock{state.textMutex};
						state.delFile = path.native();
					}
					if (delete_file_forcing(path, state, rootDir))
					{
						state.delFiles.fetch_add(1, std::memory_order_relaxed);
					}
					else
					{
						stillFailed.push_back(path);
					}
				}
				failedFiles.swap(stillFailed);
			}
			{
				std::error_code remainEc;
				const bool remains =
					fs::exists(from / L"Env", remainEc) || fs::exists(from / L"bin", remainEc);
				if (!failedFiles.empty() || remains)
				{
					// 还删不掉说明仍有环境子进程占着（句柄/Restart Manager 查不到的情况）：
					// 强制结束原目录下的进程，等待句柄释放后整棵目录再删一遍
					terminate_processes_under(rootDir, state, nullptr);
					Sleep(300);
					std::error_code cleanupEc;
					fs::remove_all(from / L"Env", cleanupEc);
					fs::remove_all(from / L"bin", cleanupEc);
				}
			}
			// 原数据根目录若已空则一并删掉（先移走迁移时生成的提示文件，remove 只删空目录）
			{
				std::error_code tipEc;
				fs::remove(from / L"请勿删除此文件夹.txt", tipEc);
			}
			if (from.has_parent_path() && from != from.root_path())
			{
				std::error_code rootEc;
				fs::remove(from, rootEc);
			}
			// 以实际残留判定是否删干净：remove 失败的文件（被占用等）和残留目录都算未删干净
			std::error_code checkEc;
			state.oldDeleteFailed = !failedFiles.empty() || fs::exists(from / L"Env", checkEc) ||
			                        fs::exists(from / L"bin", checkEc);
			state.oldDeleteError = firstDeleteError;
			state.status = 1;
		}};

		TASKDIALOGCONFIG cfg{sizeof(TASKDIALOGCONFIG)};
		cfg.hwndParent = nullptr;
		// 不允许关闭进度框：迁移期间关闭窗口会让 worker 在后台继续跑而界面消失，
		// 表现为"启动卡住无提示"。迁移必然会结束（单文件重试有上限）。
		cfg.dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER;
		cfg.pszWindowTitle = MainApp::appName.data();
		cfg.pszMainIcon = TD_INFORMATION_ICON;
		cfg.pszMainInstruction = L"正在迁移环境数据，请稍候…";
		const std::wstring content = std::format(L"从：{}\n到：{}\n\n迁移期间请勿关闭电脑或强制结束进程。", fromStr, toStr);
		cfg.pszContent = content.c_str();
		cfg.pfCallback = migration_dlg_callback;
		cfg.lpCallbackData = reinterpret_cast<LONG_PTR>(&state);
		TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr);

		worker.join();

		if (state.status.load() != 1)
		{
			// 目标侧的半成品已由 worker 清掉，这里不用再删一遍
			abort(state.error.empty() ? std::wstring{L"数据目录迁移失败，已保留原目录。"} : state.error, false);
			return;
		}

		// 成功：清挂起项（旧数据已在 worker 里删除）
		delete_reg_value(REG_VALUE_PENDING_FROM);
		delete_reg_value(REG_VALUE_PENDING_TO);
		const bool oldKept = state.oldDeleteFailed;

		std::wstring done = std::format(L"环境数据已迁移完成，数据目录已切换为：\n{}", to.native());
		const int skipped = state.skippedCount.load();
		if (skipped > 0)
		{
			done += std::format(
				L"\n\n有 {} 个文件在迁移过程中已从原目录消失（可能被其它程序删除），已跳过。", skipped);
		}
		if (oldKept)
		{
			done += L"\n\n原目录中的部分文件未能删除（可能仍被占用），可稍后手动删除。";
			if (!state.oldDeleteError.empty())
			{
				done += std::format(L"\n例如：{}", state.oldDeleteError);
			}
		}
		else
		{
			done += L"\n\n原目录中的数据已删除。";
		}
		MessageBoxW(nullptr, done.c_str(), MainApp::appName.data(), MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
	}
}

namespace
{
	// ---- 设置中心「检查与修复」----
	// 背景：杀毒软件拦截写入时，下面的所有落盘动作都会「静默失败」（源码里都是
	// 不检查返回值的 write_embedded_file），表现为环境再也启动不了、只能重装。
	// 这里用「写入探针 + 内嵌资源大小比对」把写不进去的事实显性化，并引导/自动放行。
	constexpr int REPAIR_BUTTON_ID = 100;

	struct RepairIssue
	{
		std::wstring title;
		std::wstring detail;
		bool repaired{false};
		std::wstring note;
	};

	struct RepairReport
	{
		std::vector<RepairIssue> issues;
		std::vector<std::wstring> blockedPaths; // 写入探针/写文件失败的路径（多为安全软件拦截）
		std::size_t envCount{0};
		std::wstring dataRoot;
	};

	// 目录写入探针：临时文件 + DELETE_ON_CLOSE，失败即说明该目录"写不进去"
	bool can_write_to_dir(const std::filesystem::path& dir, DWORD& error)
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		if (!fs::is_directory(dir, ec) && !ec)
		{
			fs::create_directories(dir, ec);
			if (ec)
			{
				error = static_cast<DWORD>(ec.value());
				return false;
			}
		}
		const fs::path probe = dir / std::format(L".ebox_write_test_{}.tmp", GetCurrentProcessId());
		const HANDLE hFile = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		                                 FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			return false;
		}
		const char data[4]{'t', 'e', 's', 't'};
		DWORD written = 0;
		const bool ok = WriteFile(hFile, data, sizeof(data), &written, nullptr) != FALSE &&
		                written == sizeof(data);
		if (!ok)
		{
			error = GetLastError();
		}
		CloseHandle(hFile); // FILE_FLAG_DELETE_ON_CLOSE：关闭即删除
		return ok;
	}

	// 把内嵌资源写入目标文件（先写临时文件再整体替换，避免半截文件）
	bool write_resource_file(const std::filesystem::path& target, const biz::DllResourceInfo& res, DWORD& error)
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		fs::create_directories(target.parent_path(), ec);
		const fs::path temp = std::format(L"{}.{}.repair", target.native(), GetCurrentProcessId());
		const HANDLE hFile = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		                                 FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile == INVALID_HANDLE_VALUE)
		{
			error = GetLastError();
			return false;
		}
		const char* cursor = res.address;
		unsigned int remaining = res.size;
		bool ok = true;
		while (remaining > 0)
		{
			const DWORD chunk = remaining > (1024u * 1024u) ? (1024u * 1024u) : remaining;
			DWORD written = 0;
			if (!WriteFile(hFile, cursor, chunk, &written, nullptr) || written != chunk)
			{
				ok = false;
				break;
			}
			cursor += chunk;
			remaining -= chunk;
		}
		if (ok)
		{
			FlushFileBuffers(hFile);
		}
		else
		{
			error = GetLastError();
		}
		CloseHandle(hFile);
		if (!ok)
		{
			DeleteFileW(temp.c_str());
			return false;
		}
		// 历史残留可能带只读属性，先清掉再替换
		SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
		if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			error = GetLastError();
			DeleteFileW(temp.c_str());
			return false;
		}
		return true;
	}

	// repair=false 只扫描列问题；repair=true 顺带修复
	void run_repair_checks(bool repair, RepairReport& report)
	{
		namespace fs = std::filesystem;
		report = RepairReport{};
		const fs::path root{app().envDataRoot()};
		report.dataRoot = root.native();

		DWORD probeError = 0;
		if (!can_write_to_dir(root, probeError))
		{
			RepairIssue issue;
			issue.title = L"数据目录无法写入";
			issue.detail = std::format(L"{}\n原因：{}（错误码 {}）", root.native(),
			                          win_error_text(probeError), probeError);
			issue.note = L"通常是安全软件拦截写入，需要把该目录加入信任区";
			report.blockedPaths.push_back(root.native());
			report.issues.push_back(std::move(issue));
			return; // 根目录写不进去，后续检查没有意义
		}

		const fs::path envDir = root / L"Env";
		const fs::path binDir = root / L"bin";
		const auto is_blocked = [&report](const std::wstring& path)
		{
			return std::find(report.blockedPaths.begin(), report.blockedPaths.end(), path) != report.blockedPaths.end();
		};
		for (const fs::path* dir : {&envDir, &binDir})
		{
			DWORD err = 0;
			if (can_write_to_dir(*dir, err))
			{
				continue;
			}
			RepairIssue issue;
			issue.title = L"目录无法写入";
			issue.detail = std::format(L"{}\n原因：{}（错误码 {}）", dir->native(), win_error_text(err), err);
			issue.note = L"通常是安全软件拦截写入，需要把该目录加入信任区";
			report.blockedPaths.push_back(dir->native());
			report.issues.push_back(std::move(issue));
		}
		const bool binWritable = !is_blocked(binDir.native());
		const bool envWritable = !is_blocked(envDir.native());

		// 单个组件文件的自检/修复
		const auto check_component = [&](const fs::path& path, const biz::DllResourceInfo& res,
		                                 const std::wstring& label, bool inUse)
		{
			std::error_code ec;
			const bool exists = fs::exists(path, ec) && !ec;
			const std::uint64_t size = exists ? fs::file_size(path, ec) : 0;
			if (exists && !ec && size == res.size)
			{
				return;
			}
			RepairIssue issue;
			issue.title = std::format(L"{}{}", label, exists ? L"已损坏" : L"缺失");
			issue.detail = std::format(L"{}\n当前大小：{}，应为：{}", path.native(),
			                          format_size(size), format_size(res.size));
			if (inUse)
			{
				issue.note = L"该环境正在运行，文件被占用已跳过；请退出该环境后重新检查";
			}
			else if (!binWritable)
			{
				issue.note = L"所在目录无法写入，需要把该目录加入安全软件信任区";
			}
			else if (repair)
			{
				DWORD writeError = 0;
				if (write_resource_file(path, res, writeError))
				{
					issue.repaired = true;
				}
				else
				{
					issue.note = std::format(L"写入失败：{}（错误码 {}），可能被安全软件拦截",
					                        win_error_text(writeError), writeError);
					report.blockedPaths.push_back(path.parent_path().native());
				}
			}
			report.issues.push_back(std::move(issue));
		};

		const biz::CoreData& core = biz::get_core_data();
		if constexpr (IS_CURRENT_ARCH_64_BIT)
		{
			// 64 位 eBox 启动 32 位目标时依赖它，缺了 32 位应用就起不来
			check_component(binDir / L"eBoxInject32.exe", core.exe32, L"32 位注入器", false);
		}

		const auto envList = biz::env_mgr().getAllEnv();
		report.envCount = envList.size();
		for (const auto& env : envList)
		{
			const std::wstring flag{env->getFlagName()};
			const std::wstring name{env->getName()};
			const bool inUse = env->getAllProcessesCount() > 0 || env->isRegistryHiveLoaded();
			check_component(binDir / std::format(L"{}_32.bin", flag), core.dll32,
			                std::format(L"环境「{}」的 32 位组件", name), inUse);
			check_component(binDir / std::format(L"{}_64.bin", flag), core.dll64,
			                std::format(L"环境「{}」的 64 位组件", name), inUse);

			if (!envWritable || inUse)
			{
				continue;
			}
			const fs::path hive = fs::path{app().envDataRoot()} / L"Env" /
				std::format(L"{}", env->getIndex()) / flag;
			std::error_code hEc;
			const bool hiveExists = fs::exists(hive, hEc) && !hEc;
			const std::uint64_t hiveSize = hiveExists ? fs::file_size(hive, hEc) : 0;
			if (hiveExists && !hEc && hiveSize > 0)
			{
				continue;
			}
			RepairIssue issue;
			issue.title = std::format(L"环境「{}」的注册表数据无效", name);
			issue.detail = hiveExists
				? std::format(L"{}\n文件大小为 0，无法加载（该环境会启动失败）", hive.native())
				: std::format(L"缺少文件：{}", hive.native());
			if (repair)
			{
				// RegSaveKeyW 不允许覆盖已存在的文件，先删掉再重建
				std::error_code rmEc;
				fs::remove(hive, rmEc);
				if (env->ensureRegistryHiveFile())
				{
					issue.repaired = true;
				}
				else
				{
					issue.note = L"重建失败，可能被安全软件拦截";
				}
			}
			report.issues.push_back(std::move(issue));
		}

		// eBox 自身的配置 hive：缺失属正常（下次启动自动创建），仅 0 字节算问题
		if (envWritable)
		{
			const fs::path appHive = envDir / L"data" / fs::path{MainApp::appName};
			std::error_code aEc;
			if (fs::exists(appHive, aEc) && !aEc && fs::file_size(appHive, aEc) == 0)
			{
				RepairIssue issue;
				issue.title = L"eBox 配置数据文件为空";
				issue.detail = std::format(L"{}\n环境列表与配置可能无法保存。", appHive.native());
				issue.note = L"重启 eBox 后会自动重建；若重启后仍为空，请把数据目录加入安全软件信任区";
				report.issues.push_back(std::move(issue));
			}
		}

		// 上次切换数据目录残留的挂起项（启动期已处理完，运行期出现必为残留）
		for (const wchar_t* valueName : {REG_VALUE_PENDING_FROM, REG_VALUE_PENDING_TO})
		{
			const std::wstring value = read_reg_value(valueName);
			if (value.empty())
			{
				continue;
			}
			RepairIssue issue;
			issue.title = L"残留的目录切换标记";
			issue.detail = std::format(L"{} = {}", std::wstring{valueName}, value);
			if (repair)
			{
				delete_reg_value(valueName);
				issue.repaired = true;
			}
			report.issues.push_back(std::move(issue));
		}
	}

	std::wstring build_repair_details(const RepairReport& report)
	{
		std::wstring text;
		for (std::size_t i = 0; i < report.issues.size(); ++i)
		{
			const RepairIssue& issue = report.issues[i];
			text += std::format(L"{}. {}", i + 1, issue.title);
			text += issue.repaired ? std::wstring{L"（已修复）\n"} : std::wstring{L"\n"};
			if (!issue.detail.empty())
			{
				text += issue.detail;
				text += L"\n";
			}
			if (!issue.note.empty())
			{
				text += issue.note;
				text += L"\n";
			}
			text += L"\n";
		}
		return text;
	}

	// 把 eBox 的数据/程序目录加入 Windows Defender 排除项（需管理员权限）
	bool add_defender_exclusions()
	{
		namespace fs = std::filesystem;
		const auto quote = [](const std::wstring& s)
		{
			std::wstring escaped;
			for (const wchar_t ch : s)
			{
				escaped.push_back(ch);
				if (ch == L'\'')
				{
					escaped.push_back(L'\'');
				}
			}
			return std::format(L"'{}'", escaped);
		};

		const fs::path root{app().envDataRoot()};
		std::vector<std::wstring> dirs;
		dirs.push_back(root.native());
		dirs.push_back((root / L"Env").native());
		dirs.push_back((root / L"bin").native());
		dirs.push_back(std::wstring{app().exeDir()});
		std::vector<std::wstring> exes;
		exes.push_back(std::wstring{app().exeFullName()});
		exes.push_back((root / L"bin" / L"eBoxInject32.exe").native());

		std::wstring script = L"$ErrorActionPreference='Stop';Add-MpPreference -ExclusionPath ";
		for (std::size_t i = 0; i < dirs.size(); ++i)
		{
			if (i > 0)
			{
				script += L",";
			}
			script += quote(dirs[i]);
		}
		script += L";Add-MpPreference -ExclusionProcess ";
		for (std::size_t i = 0; i < exes.size(); ++i)
		{
			if (i > 0)
			{
				script += L",";
			}
			script += quote(exes[i]);
		}
		script += L";exit 0";

		const std::wstring params = std::format(
			L"-NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy Bypass -Command \"{}\"",
			script);
		SHELLEXECUTEINFOW siw{sizeof(siw)};
		siw.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
		siw.lpVerb = L"runas";
		siw.lpFile = L"powershell.exe";
		siw.lpParameters = params.c_str();
		siw.nShow = SW_HIDE;
		if (!ShellExecuteExW(&siw) || siw.hProcess == nullptr)
		{
			return false; // 用户取消了 UAC 或系统无 PowerShell
		}
		WaitForSingleObject(siw.hProcess, 60 * 1000);
		DWORD exitCode = 1;
		GetExitCodeProcess(siw.hProcess, &exitCode);
		CloseHandle(siw.hProcess);
		return exitCode == 0;
	}

	// 告知用户写入被拦截，并尝试自动放行。返回 true 表示已尝试加入信任区
	bool offer_security_allowance(HWND owner, const std::vector<std::wstring>& blockedPaths)
	{
		std::wstring list;
		for (const std::wstring& path : blockedPaths)
		{
			list += L"    ";
			list += path;
			list += L"\n";
		}
		const std::wstring content = std::format(
			L"以下位置无法写入，通常是杀毒软件 / 安全管家拦截了写入：\n{}\n"
			L"不放行的话运行组件写不进去，多开将无法启动。", list);
		constexpr wchar_t expanded[] =
			L"「自动加入信任区」会弹出管理员权限（UAC）提示，并执行 Windows Defender 排除命令：\n"
			L"  Add-MpPreference -ExclusionPath / -ExclusionProcess\n\n"
			L"若使用的是其他安全软件（360、火绒、腾讯电脑管家等），请在它的「信任区 / 白名单 / 排除项」中\n"
			L"添加上述目录，并放行进程 eBox.exe 与 bin\\eBoxInject32.exe，然后重新点击「检查与修复」。";

		TASKDIALOGCONFIG cfg{sizeof(TASKDIALOGCONFIG)};
		cfg.hwndParent = owner;
		cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_EXPANDED_BY_DEFAULT;
		cfg.pszWindowTitle = MainApp::appName.data();
		cfg.pszMainIcon = TD_SHIELD_ICON;
		cfg.pszMainInstruction = L"检测到写入被安全软件拦截";
		cfg.pszContent = content.c_str();
		cfg.pszExpandedInformation = expanded;
		TASKDIALOG_BUTTON buttons[2]{};
		buttons[0].nButtonID = IDYES;
		buttons[0].pszButtonText = L"自动加入 Windows Defender 信任区";
		buttons[1].nButtonID = IDCANCEL;
		buttons[1].pszButtonText = L"手动处理";
		cfg.pButtons = buttons;
		cfg.cButtons = 2;
		cfg.nDefaultButton = IDYES;
		int pressed = IDCANCEL;
		TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr);
		if (pressed != IDYES)
		{
			return false;
		}
		if (add_defender_exclusions())
		{
			MessageBoxW(owner, L"已加入 Windows Defender 信任区，将自动重新修复。",
			            MainApp::appName.data(), MB_OK | MB_ICONINFORMATION);
			return true;
		}
		MessageBoxW(owner,
		            L"自动加入信任区失败（可能取消了管理员授权，或系统未启用 Windows Defender）。\n\n"
		            L"请手动在安全软件的信任区 / 白名单中添加 eBox 数据目录，并放行 eBox.exe、eBoxInject32.exe。",
		            MainApp::appName.data(), MB_OK | MB_ICONWARNING);
		return false;
	}
}

int run_self_check_and_repair(HWND owner)
{
	RepairReport scan;
	run_repair_checks(false, scan);
	if (scan.issues.empty())
	{
		MessageBoxW(owner,
		            std::format(L"自检完成，未发现问题。\n\n已检查数据目录：{}\n共 {} 个环境的运行组件与注册表数据。",
		                        scan.dataRoot, scan.envCount).c_str(),
		            MainApp::appName.data(), MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
		return 0;
	}

	// 先列出问题，由用户决定是否修复
	{
		const std::wstring instruction = std::format(L"发现 {} 个问题，是否立即修复？", scan.issues.size());
		const std::wstring details = build_repair_details(scan);
		TASKDIALOGCONFIG cfg{sizeof(TASKDIALOGCONFIG)};
		cfg.hwndParent = owner;
		cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_EXPANDED_BY_DEFAULT;
		cfg.pszWindowTitle = MainApp::appName.data();
		cfg.pszMainIcon = TD_WARNING_ICON;
		cfg.pszMainInstruction = instruction.c_str();
		cfg.pszContent = L"自检发现运行组件或环境数据异常，多数是被杀毒软件误删 / 拦截写入导致。\n"
		                 L"修复会重写缺失或损坏的组件文件，并重建无效的环境注册表数据。";
		cfg.pszExpandedInformation = details.c_str();
		TASKDIALOG_BUTTON buttons[2]{};
		buttons[0].nButtonID = REPAIR_BUTTON_ID;
		buttons[0].pszButtonText = L"立即修复";
		buttons[1].nButtonID = IDCANCEL;
		buttons[1].pszButtonText = L"暂不修复";
		cfg.pButtons = buttons;
		cfg.cButtons = 2;
		cfg.nDefaultButton = REPAIR_BUTTON_ID;
		int pressed = IDCANCEL;
		TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr);
		if (pressed != REPAIR_BUTTON_ID)
		{
			return 2;
		}
	}

	bool defenderAllowed = false;
	if (!scan.blockedPaths.empty())
	{
		defenderAllowed = offer_security_allowance(owner, scan.blockedPaths);
	}

	const auto count_repaired = [](const RepairReport& report)
	{
		std::size_t count = 0;
		for (const RepairIssue& issue : report.issues)
		{
			if (issue.repaired)
			{
				++count;
			}
		}
		return count;
	};

	RepairReport repaired;
	run_repair_checks(true, repaired);
	const std::size_t fixedCount = count_repaired(repaired);

	RepairReport after;
	run_repair_checks(false, after);
	if (after.issues.empty())
	{
		MessageBoxW(owner, std::format(L"修复完成：已解决 {} 个问题。", fixedCount).c_str(),
		            MainApp::appName.data(), MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
		return 1;
	}

	// 仍有问题且此前未放行：再给一次自动放行机会（放行后立刻重试一轮）
	if (!defenderAllowed && !after.blockedPaths.empty() &&
		offer_security_allowance(owner, after.blockedPaths))
	{
		run_repair_checks(true, repaired);
		run_repair_checks(false, after);
		if (after.issues.empty())
		{
			MessageBoxW(owner, std::format(L"修复完成：已解决 {} 个问题。", count_repaired(repaired)).c_str(),
			            MainApp::appName.data(), MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
			return 1;
		}
	}

	const std::wstring remainingInstruction = std::format(L"仍有 {} 个问题未能自动修复", after.issues.size());
	const std::wstring remainingDetails = build_repair_details(after);
	TASKDIALOGCONFIG cfg{sizeof(TASKDIALOGCONFIG)};
	cfg.hwndParent = owner;
	cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_EXPANDED_BY_DEFAULT;
	cfg.pszWindowTitle = MainApp::appName.data();
	cfg.pszMainIcon = TD_ERROR_ICON;
	cfg.pszMainInstruction = remainingInstruction.c_str();
	cfg.pszContent = L"请按下方说明处理后，再点击「检查与修复」重试。\n"
	                 L"若提示被杀毒软件拦截，请在其信任区 / 白名单中放行 eBox 数据目录与 eBox.exe、eBoxInject32.exe。";
	cfg.pszExpandedInformation = remainingDetails.c_str();
	cfg.dwCommonButtons = TDCBF_CLOSE_BUTTON;
	TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr);
	return 2;
}

bool request_data_dir_change(const std::wstring& targetDir)
{
	try
	{
		const std::wstring from{app().envDataRoot()};
		if (targetDir.empty() || _wcsicmp(from.c_str(), targetDir.c_str()) == 0)
		{
			return false;
		}
		if (!write_reg_value(REG_VALUE_PENDING_FROM, from) ||
			!write_reg_value(REG_VALUE_PENDING_TO, targetDir))
		{
			delete_reg_value(REG_VALUE_PENDING_FROM);
			delete_reg_value(REG_VALUE_PENDING_TO);
			return false;
		}

		// 用默认动词拉起新实例（保持同一用户与完整性级别，避免 HKCU/权限错位）
		// 新实例会先等待本进程退出，再在启动期完成迁移。
		SHELLEXECUTEINFOW siw{sizeof(siw)};
		siw.lpFile = app().exeFullName().data();
		const std::wstring params = std::format(LR"(--wait-another-end={})", GetCurrentProcessId());
		siw.lpParameters = params.c_str();
		siw.nShow = SW_SHOWNORMAL;
		if (!ShellExecuteExW(&siw))
		{
			delete_reg_value(REG_VALUE_PENDING_FROM);
			delete_reg_value(REG_VALUE_PENDING_TO);
			return false;
		}
		app().exit();
		return true;
	}
	catch (...)
	{
		delete_reg_value(REG_VALUE_PENDING_FROM);
		delete_reg_value(REG_VALUE_PENDING_TO);
		return false;
	}
}

namespace
{
	HRESULT CALLBACK task_dialog_callback(_In_ HWND hwnd, _In_ UINT msg, _In_ WPARAM wParam, _In_ LPARAM lParam, _In_ LONG_PTR lpRefData)
	{
		if (msg == TDN_CREATED)
		{
			SendMessageW(hwnd, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, IDYES, 1);
		}
		else if (msg == TDN_BUTTON_CLICKED)
		{
			if (wParam == IDYES)
			{
				SHELLEXECUTEINFOW siw{sizeof(siw)};
				siw.lpVerb = L"runas";
				siw.lpFile = app().exeFullName().data();
				std::wstring params = std::format(LR"(--wait-another-end={})", GetCurrentProcessId());
				siw.lpParameters = params.c_str();
				siw.nShow = SW_SHOWNORMAL;
				ShellExecuteExW(&siw);
				app().exit();
			}
		}
		return S_OK;
	}
}

void show_require_elevation_message(std::wstring_view requester, std::wstring_view path)
{
	static std::atomic hasShown{false};
	if (hasShown.exchange(true))
	{
		return;
	}
	app().get_scheduler().addTask([requester = std::wstring{requester}, path = std::wstring{path}]()
	{
		TASKDIALOGCONFIG cfg{sizeof(TASKDIALOGCONFIG)};
		cfg.hwndParent = ui::main_wnd().nativeHandle();
		cfg.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
		cfg.pszWindowTitle = MainApp::appName.data();
		cfg.pszMainIcon = TD_SHIELD_ICON;
		cfg.pszMainInstruction = L"环境中有进程试图以管理员身份运行子进程";
		std::wstring content = std::format(L"进程：{}\n试图以管理员身份运行：{}\n\n"
		                                   L"{}在权限受限情况下会始终阻止环境中的进程以管理员身份创建子进程，这可能会导致环境中的进程功能或行为不正常。您可以选择：",
		                                   requester, path, MainApp::appName.data());
		cfg.pszContent = content.c_str();
		cfg.dwCommonButtons = TDCBF_CANCEL_BUTTON;
		std::wstring btnText1 = std::format(L"以管理员身份重新运行{}\n"
		                                    L"注意！重启{}会导致强制关闭所有环境中的所有进程！建议手动关闭所有进程以免丢失数据",
		                                    MainApp::appName.data(), MainApp::appName.data());
		std::wstring btnText2 = std::format(L"必要时手动以管理员身份运行{}\n"
		                                    L"选择此项并继续观察环境中该进程的表现，如您发现任何异常，可以手动以管理员身份重新运行{}",
		                                    MainApp::appName.data(), MainApp::appName.data());
		TASKDIALOG_BUTTON btnArray[2]{};
		btnArray[0].nButtonID = IDYES;
		btnArray[0].pszButtonText = btnText1.c_str();
		btnArray[1].nButtonID = IDCANCEL;
		btnArray[1].pszButtonText = btnText2.c_str();
		cfg.pButtons = btnArray;
		cfg.cButtons = sizeof(btnArray) / sizeof(TASKDIALOG_BUTTON);
		cfg.nDefaultButton = IDCANCEL;
		cfg.pfCallback = task_dialog_callback;

		TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr);
	});
}
