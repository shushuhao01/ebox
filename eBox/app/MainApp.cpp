module;
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Rstrtmgr.lib")
// Restart Manager：迁移时定位"谁占用着这个文件"，用于结束后自动继续复制
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <restartmanager.h>
#include <tlhelp32.h>
// BCrypt：迁移校验时计算 SHA-256（只用 CreateHash/HashData/FinishHash，Win7 可用）
#include <bcrypt.h>
// 崩溃兜底：MINIDUMP_* 结构定义（MiniDumpWriteDump 运行时从 Dbghelp.dll 动态取）
#include <dbghelp.h>
// SHGetKnownFolderPath：定位真实的“文档”目录，推导环境内 WXWork 数据目录位置
#include <shlobj.h>
#include <knownfolders.h>
#include <objbase.h>
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

	// 见下方实现：启动时静默把数据目录/程序目录加入 Defender 排除项（只成功执行一次）
	void ensure_defender_exclusions_async();

	// ===== 崩溃兜底 =====
	// 更新流程等 UI 回调一旦抛出未处理的结构化异常，系统会把它升级为
	// STATUS_FATAL_USER_CALLBACK_EXCEPTION(0xc000041d) 直接杀进程，现场只剩一个
	// 笼统的"停止工作"弹窗。这里在进程最外层兜住，落一份完整 minidump 便于定位。
	// 注意：过滤器运行在异常上下文，只做最小必要的文件操作，避免二次异常。
	LONG WINAPI ebox_unhandled_exception_filter(EXCEPTION_POINTERS* info)
	{
		wchar_t appData[MAX_PATH] = {};
		if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
		{
			const std::wstring root = std::wstring(appData) + L"\\eBox";
			const std::wstring crashDir = root + L"\\crash";
			CreateDirectoryW(root.c_str(), nullptr);
			CreateDirectoryW(crashDir.c_str(), nullptr);

			SYSTEMTIME st{};
			GetLocalTime(&st);
			const auto two_digit = [](WORD v) -> std::wstring
			{
				return (v < 10 ? L"0" : L"") + std::to_wstring(v);
			};
			const std::wstring dumpPath = crashDir + L"\\eBox_crash_" + std::to_wstring(st.wYear) +
			                              two_digit(st.wMonth) + two_digit(st.wDay) + L"_" +
			                              two_digit(st.wHour) + two_digit(st.wMinute) + two_digit(st.wSecond) +
			                              L"_p" + std::to_wstring(GetCurrentProcessId()) + L".dmp";

			// 动态加载：避免进程启动即绑定 dbghelp，也避免加载器锁内做多余导入
			HMODULE hDbgHelp = LoadLibraryW(L"Dbghelp.dll");
			if (hDbgHelp)
			{
				using PFN_MiniDumpWriteDump = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
				                                            PMINIDUMP_EXCEPTION_INFORMATION,
				                                            PMINIDUMP_USER_STREAM_INFORMATION,
				                                            PMINIDUMP_CALLBACK_INFORMATION);
				auto pMiniDumpWriteDump = reinterpret_cast<PFN_MiniDumpWriteDump>(
				    reinterpret_cast<void*>(GetProcAddress(hDbgHelp, "MiniDumpWriteDump")));
				if (pMiniDumpWriteDump)
				{
					const HANDLE hFile = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
					                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
					if (hFile != INVALID_HANDLE_VALUE)
					{
						MINIDUMP_EXCEPTION_INFORMATION mei{};
						mei.ThreadId = GetCurrentThreadId();
						mei.ExceptionPointers = info;
						mei.ClientPointers = FALSE;
						pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
						                   MiniDumpWithFullMemory, &mei, nullptr, nullptr);
						CloseHandle(hFile);
					}
				}
				FreeLibrary(hDbgHelp);
			}
		}
		// 已落盘：不再走 WER 上报/弹窗，直接让进程结束
		return EXCEPTION_EXECUTE_HANDLER;
	}

	// ===== 启动期权限检查（环境注册表 hive 的硬前提）=====
	// 环境 hive 依赖 RegLoadKeyW（需 SeRestorePrivilege）与 RegSaveKeyW（需 SeBackupPrivilege）。
	// 这两项权限只存在于已提权的管理员令牌中：未提权时 RegLoadKeyW 直接返回
	// 1314(ERROR_PRIVILEGE_NOT_HELD)，环境会退化为进程级临时注册表、数据不落盘，
	// 表现为"重启后需要重新扫码"。所以启动期先确认权限，缺失则以管理员身份重启自身。
	constexpr std::wstring_view ELEVATED_RELAUNCH_FLAG = L"--elevated-relaunch";

	// 当前令牌是否持有 hive 所需的全部权限（有权限但被禁用也算持有，后续可自行启用）
	bool holds_hive_privileges()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		{
			return false;
		}
		// PRIVILEGE_SET 的 Privilege 数组声明长度为 1，两项权限必须自行扩容，否则越界写入
		struct alignas(PRIVILEGE_SET) PrivilegeSetTwo
		{
			DWORD PrivilegeCount;
			DWORD Control;
			LUID_AND_ATTRIBUTES Privilege[2];
		};
		PrivilegeSetTwo required{};
		LUID restoreLuid{};
		LUID backupLuid{};
		bool held = false;
		if (LookupPrivilegeValueW(nullptr, SE_RESTORE_NAME, &restoreLuid) &&
		    LookupPrivilegeValueW(nullptr, SE_BACKUP_NAME, &backupLuid))
		{
			required.PrivilegeCount = 2;
			required.Control = PRIVILEGE_SET_ALL_NECESSARY;
			required.Privilege[0].Luid = restoreLuid;
			required.Privilege[1].Luid = backupLuid;
			BOOL result = FALSE;
			held = PrivilegeCheck(token, reinterpret_cast<PRIVILEGE_SET*>(&required), &result) && result;
		}
		CloseHandle(token);
		return held;
	}

	bool is_admin_member()
	{
		SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
		PSID adminSid = nullptr;
		if (!AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
		                              DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminSid))
		{
			return false;
		}
		BOOL isMember = FALSE;
		CheckTokenMembership(nullptr, adminSid, &isMember);
		FreeSid(adminSid);
		return isMember != FALSE;
	}

	// 当前用户能否"无凭据"提权：属于管理员组、只是被 UAC 过滤（Limited）。
	// 标准用户提权必须输入管理员账号，会让 eBox 以另一个用户身份运行（HKCU / 数据目录错位），
	// 因此这种情况不自动提权，只由环境日志提示需要管理员权限。
	bool can_elevate_silently()
	{
		HANDLE token = nullptr;
		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		{
			return false;
		}
		TOKEN_ELEVATION_TYPE type = TokenElevationTypeDefault;
		DWORD size = sizeof(type);
		const bool gotType = GetTokenInformation(token, TokenElevationType, &type, size, &size) != FALSE;
		CloseHandle(token);
		if (!gotType)
		{
			return false;
		}
		switch (type)
		{
		case TokenElevationTypeFull:
			return true;
		case TokenElevationTypeLimited:
			return true; // 用户属于管理员组，UAC 可无凭据提权
		default:
			// 未启用 UAC（令牌本身已是管理员）或标准用户
			return is_admin_member();
		}
	}

	// 是否已有环境在使用"用户注册表存储"（HKCU\Software\eBox\EnvRegistry\<index>）。
	// 有则说明用户已经（或只能）以普通权限运行过：提权对持久化已无任何作用，
	// 反而每次启动都会弹一次 UAC，因此不再自动提权。
	bool user_registry_store_present()
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\eBox\\EnvRegistry", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		{
			return false;
		}
		DWORD subKeyCount = 0;
		const bool hasSubKey =
			RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &subKeyCount,
			                 nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS
			&& subKeyCount > 0;
		RegCloseKey(hKey);
		return hasSubKey;
	}

	// 以管理员身份重启自身；返回 true 表示新实例已拉起，本进程应立即退出。
	bool relaunch_elevated(std::wstring_view cmdLine)
	{
		wchar_t exePath[MAX_PATH * 4]{};
		const DWORD capacity = static_cast<DWORD>(std::size(exePath));
		const DWORD len = GetModuleFileNameW(nullptr, exePath, capacity);
		if (len == 0 || len >= capacity)
		{
			return false;
		}
		// 带标记重启：万一提权后令牌仍缺权限（如组策略移除），不会无限重启自己
		std::wstring params{cmdLine};
		if (!params.empty())
		{
			params += L' ';
		}
		params += ELEVATED_RELAUNCH_FLAG;
		SHELLEXECUTEINFOW siw{sizeof(siw)};
		siw.fMask = SEE_MASK_NOASYNC; // 本进程马上退出，必须等启动完成
		siw.lpVerb = L"runas";
		siw.lpFile = exePath;
		siw.lpParameters = params.c_str();
		siw.nShow = SW_SHOWNORMAL;
		return ShellExecuteExW(&siw) != FALSE; // 用户取消 UAC 时返回 FALSE，继续以受限模式运行
	}
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ [[maybe_unused]] HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
	// 兜底：未处理异常时生成 minidump（%LOCALAPPDATA%\eBox\crash），避免只剩系统弹窗
	SetUnhandledExceptionFilter(&ebox_unhandled_exception_filter);

	try
	{
		// 环境注册表优先沿用 hive（提权可用时，数据仍留在环境目录）；普通权限下自动改用
		// HKCU 用户注册表存储，同样持久化、无需管理员。因此只有"从未用过用户注册表存储"
		// 且当前令牌缺 hive 权限时，才尝试提权重启；已切到用户注册表存储的环境不再弹 UAC。
		if (!holds_hive_privileges()
			&& !user_registry_store_present()
			&& !std::wstring_view{lpCmdLine}.contains(ELEVATED_RELAUNCH_FLAG)
			&& can_elevate_silently()
			&& relaunch_elevated(lpCmdLine))
		{
			return 0;
		}

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

		// 静默把数据目录/程序目录加入 Defender 排除项：每次启动都要写 DLL 并注入子进程，
		// 目录未排除时会触发实时扫描，明显拖慢启动（曾观测到 20 秒以上的空档）。
		ensure_defender_exclusions_async();

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
		// 校验（phase=1）进度：逐文件比对大小 + SHA-256
		std::atomic<std::uint64_t> verifyFiles{0};
		std::atomic<std::uint64_t> verifyTotalFiles{0};
		std::uint64_t verifiedFileCount{0}; // 校验通过的文件数（join 后主线程读，用于完成提示）
		std::uint64_t verifiedByteCount{0}; // 校验通过的字节数
		// true=上次迁移已复制并校验通过、只差删除原目录（启动期续做时置位）。
		// 此时绝不允许重新复制（源目录已被删掉一部分，重拷会用残缺数据覆盖好数据）。
		bool resumeDelete{false};
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

		// 严格枚举：不使用 skip_permission_denied —— 任何读不到的子树都必须让迁移失败，
		// 否则会"悄悄少复制一批文件"却仍判成功，切到目标目录后表现为数据缺失。
		fs::recursive_directory_iterator it{fromRoot, fs::directory_options::none, ec};
		const fs::recursive_directory_iterator end{};
		if (ec)
		{
			state.error = std::format(L"读取源目录失败：{}\n{}\n\n已保留原目录。", fromRoot.native(),
			                          narrow_to_wide(ec.message()));
			return false;
		}
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
					state.error = std::format(L"无法创建目标目录：{}\n{}\n\n已保留原目录。", dst.native(),
					                          narrow_to_wide(mkEc.message()));
					return false;
				}
			}
			else if (it->is_regular_file(typeEc))
			{
				std::error_code sizeEc;
				const std::uint64_t size = it->file_size(sizeEc);
				if (sizeEc)
				{
					// 读不到大小说明这个文件当前不可读（被独占/权限不足/枚举期间消失）。
					// 不能跳过：跳过就等于少复制一个文件，而校验也查不出来。
					state.error = std::format(L"读取源文件失败：{}\n{}\n\n"
					                          L"请关闭占用该文件的程序后重新切换磁盘。\n\n已保留原目录。",
					                          src.native(), narrow_to_wide(sizeEc.message()));
					return false;
				}
				plan.push_back(CopyTask{src, dst, size});
				state.total += size;
			}
			else if (typeEc)
			{
				state.error = std::format(L"读取源目录失败：{}\n{}\n\n已保留原目录。", src.native(),
				                          narrow_to_wide(typeEc.message()));
				return false;
			}
			it.increment(ec);
		}
		if (ec)
		{
			state.error = std::format(L"读取源目录失败：{}\n{}\n\n已保留原目录。", fromRoot.native(),
			                          narrow_to_wide(ec.message()));
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

	// 迁移前结束"占用源数据文件"的所有进程。
	// terminate_processes_under 只能结束映像位于源目录内的进程，而企业微信/微信的本体
	// （如 D:\RJ\WXWork\WXWork.exe）装在别处、却持有 Env 内数据库与 hive 的句柄：
	// 不结束它，SQLite（WAL 模式）会在复制期间继续写入，拷出来的是撕裂的快照。
	// 这里用 Restart Manager 反查占用者（每批 64 个文件一个会话）。
	void terminate_lockers_under_tree(const std::filesystem::path& root, MigrationState& state)
	{
		namespace fs = std::filesystem;
		constexpr std::size_t kMaxProbeFiles = 4096;
		constexpr std::size_t kMaxPriorityFiles = 1024;
		constexpr std::size_t kFilesPerSession = 64;

		static constexpr const wchar_t* kDataExts[] = {
			L".db",   L".db-wal", L".db-shm", L".db-journal", L".sqlite", L".sqlite3",
			L".ldb",  L".hive",   L".dat",    L".log",        L".idx",
		};
		const auto looks_like_data = [](const fs::path& path)
		{
			const std::wstring name = path.filename().native();
			for (const wchar_t* ext : kDataExts)
			{
				const std::size_t len = ::wcslen(ext);
				if (name.size() >= len && _wcsicmp(name.c_str() + (name.size() - len), ext) == 0)
				{
					return true;
				}
			}
			return false;
		};

		std::vector<std::wstring> files;
		files.reserve(kMaxProbeFiles);
		// 企业微信的数据文件（客户资料/会话库都在 WXWork\<corp>\Data）优先探测：
		// 多环境时数据目录内的文件可达数万个，只按枚举顺序取前 N 个会把后面环境的
		// 企微整个漏掉 —— 企微进程没被结束就会在复制期间继续写库，拷出撕裂的快照。
		std::vector<std::wstring> priority;
		priority.reserve(kMaxPriorityFiles);
		const auto is_wxwork_path = [](const std::wstring& path)
		{
			std::wstring lower = path;
			for (wchar_t& ch : lower)
			{
				if (ch >= L'A' && ch <= L'Z')
				{
					ch = static_cast<wchar_t>(ch - L'A' + L'a');
				}
			}
			return lower.find(L"\\wxwork\\") != std::wstring::npos;
		};
		std::error_code ec;
		const fs::recursive_directory_iterator end{};
		for (fs::recursive_directory_iterator it{root, fs::directory_options::skip_permission_denied, ec}; it != end;
		     it.increment(ec))
		{
			std::error_code typeEc;
			if (!it->is_regular_file(typeEc) || !looks_like_data(it->path()))
			{
				continue;
			}
			const std::wstring path = it->path().native();
			if (is_wxwork_path(path))
			{
				if (priority.size() < kMaxPriorityFiles)
				{
					priority.push_back(path);
				}
			}
			else if (files.size() < kMaxProbeFiles)
			{
				files.push_back(path);
			}
		}
		files.insert(files.end(), priority.begin(), priority.end());
		if (files.empty())
		{
			return;
		}

		const DWORD selfPid = GetCurrentProcessId();
		std::vector<DWORD> targets;
		const auto collect = [&](std::size_t begin, std::size_t count)
		{
			std::vector<const wchar_t*> resources;
			resources.reserve(count);
			for (std::size_t i = 0; i < count; ++i)
			{
				resources.push_back(files[begin + i].c_str());
			}
			DWORD sessionHandle = 0;
			wchar_t sessionKey[CCH_RM_SESSION_KEY + 1]{};
			if (RmStartSession(&sessionHandle, 0, sessionKey) != ERROR_SUCCESS)
			{
				return;
			}
			if (RmRegisterResources(sessionHandle, static_cast<UINT>(resources.size()), resources.data(), 0, nullptr,
			                        0, nullptr) == ERROR_SUCCESS)
			{
				UINT needed = 0;
				UINT got = 0;
				DWORD reason = RmRebootReasonNone;
				DWORD rc = RmGetList(sessionHandle, &needed, &got, nullptr, &reason);
				if (rc == ERROR_MORE_DATA && needed > 0)
				{
					std::vector<RM_PROCESS_INFO> infos(needed);
					got = needed;
					rc = RmGetList(sessionHandle, &needed, &got, infos.data(), &reason);
					if (rc == ERROR_SUCCESS)
					{
						for (UINT i = 0; i < got; ++i)
						{
							const DWORD pid = infos[i].Process.dwProcessId;
							if (pid == 0 || pid == selfPid || pid <= 4 ||
								std::find(targets.begin(), targets.end(), pid) != targets.end())
							{
								continue;
							}
							targets.push_back(pid);
						}
					}
				}
			}
			RmEndSession(sessionHandle);
		};
		for (std::size_t begin = 0; begin < files.size(); begin += kFilesPerSession)
		{
			collect(begin, std::min(kFilesPerSession, files.size() - begin));
		}

		for (const DWORD pid : targets)
		{
			HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			if (!hProcess)
			{
				continue;
			}
			wchar_t imagePath[MAX_PATH * 2]{};
			DWORD size = static_cast<DWORD>(std::size(imagePath));
			const bool got = QueryFullProcessImageNameW(hProcess, 0, imagePath, &size) != FALSE;
			CloseHandle(hProcess);
			// 系统关键进程（svchost/explorer 等）不能杀，只能寄希望于重试
			if (!got || is_protected_process(std::wstring{imagePath, size}))
			{
				continue;
			}
			{
				std::lock_guard lock{state.textMutex};
				// 借用第 0 个槽位展示（此时还没有复制线程在跑）
				state.slots[0].closing = std::format(L"{}（PID {}）", std::wstring{imagePath, size}, pid);
			}
			end_process(pid);
		}
		{
			std::lock_guard lock{state.textMutex};
			state.slots[0].closing.clear();
		}
		// 给被结束的进程一点时间释放句柄（它们可能在退出前做 WAL checkpoint）
		if (!targets.empty())
		{
			::Sleep(500);
		}
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

	// ---- 迁移校验 ----
	// 只比"计划里的大小"是不够的：计划阶段枚举不到的文件根本不在校验范围（旧实现会静默跳过），
	// 而 WAL 模式的数据库边写边拷时大小可能一致、内容却是撕裂的。因此这里做严格校验：
	// 重新枚举源目录，逐个文件比对目标侧的存在性、大小与 SHA-256，不通过就绝不切换。

	// 统一成小写、反斜杠分隔的路径键（Windows 路径不区分大小写）
	std::wstring path_key(std::wstring text)
	{
		for (auto& ch : text)
		{
			if (ch == L'/')
			{
				ch = L'\\';
			}
			else if (ch >= L'A' && ch <= L'Z')
			{
				ch = static_cast<wchar_t>(ch - L'A' + L'a');
			}
		}
		return text;
	}

	// 内容是"易变"的路径：日志/缓存/临时目录与临时文件。
	// 这些不是用户数据，且环境一启动就会被改写，逐字节比对必然误报。
	bool is_transient_path(const std::wstring& relativeKey)
	{
		static constexpr const wchar_t* kTransientDirs[] = {
			L"\\logs\\", L"\\log\\",         L"\\cache\\", L"\\caches\\", L"\\gpucache\\",
			L"\\code cache\\", L"\\cachestorage\\", L"\\crashpad\\", L"\\temp\\",
			L"\\tmp\\",  L"\\dumps\\",
		};
		std::wstring text = L"\\";
		text += relativeKey;
		for (const wchar_t* dir : kTransientDirs)
		{
			if (text.find(dir) != std::wstring::npos)
			{
				return true;
			}
		}
		static constexpr const wchar_t* kTransientExts[] = {L".tmp", L".dmp", L".etl", L".lock"};
		for (const wchar_t* ext : kTransientExts)
		{
			const std::size_t len = ::wcslen(ext);
			if (text.size() >= len && text.compare(text.size() - len, len, ext) == 0)
			{
				return true;
			}
		}
		return false;
	}

	// 计算文件的 SHA-256（BCrypt 三段式；不能用 BCryptHash —— Win7 没有该导出）
	bool sha256_file(const std::filesystem::path& path, std::array<unsigned char, 32>& digest)
	{
		HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ,
		                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		                            FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		BCRYPT_ALG_HANDLE algorithm = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		bool ok = false;
		do
		{
			if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
			{
				break;
			}
			if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0)
			{
				break;
			}
			std::array<unsigned char, 64 * 1024> buffer{};
			for (;;)
			{
				DWORD read = 0;
				if (!::ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
				{
					break;
				}
				if (read == 0)
				{
					ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
					break;
				}
				if (BCryptHashData(hash, buffer.data(), read, 0) < 0)
				{
					break;
				}
			}
		} while (false);
		if (hash != nullptr)
		{
			BCryptDestroyHash(hash);
		}
		if (algorithm != nullptr)
		{
			BCryptCloseAlgorithmProvider(algorithm, 0);
		}
		::CloseHandle(file);
		return ok;
	}

	// 严格枚举一棵目录树：相对路径(小写) → 大小。任何读不到的东西都算失败。
	bool scan_tree(const std::filesystem::path& root, std::unordered_map<std::wstring, std::uint64_t>& files,
	               std::uint64_t& bytes, std::wstring& error)
	{
		namespace fs = std::filesystem;
		files.clear();
		bytes = 0;
		std::error_code ec;
		if (!fs::is_directory(root, ec))
		{
			if (ec)
			{
				error = std::format(L"数据目录迁移校验失败：无法读取目录。\n{}\n{}\n\n已保留原目录。",
				                    root.native(), narrow_to_wide(ec.message()));
				return false;
			}
			return true; // 该子树不存在（Env/bin 可能本来就没有）
		}
		std::wstring prefix = root.native();
		if (prefix.empty() || (prefix.back() != L'\\' && prefix.back() != L'/'))
		{
			prefix.push_back(L'\\');
		}
		const fs::recursive_directory_iterator end{};
		for (fs::recursive_directory_iterator it{root, fs::directory_options::none, ec}; !ec && it != end;
		     it.increment(ec))
		{
			std::error_code typeEc;
			if (!it->is_regular_file(typeEc))
			{
				if (typeEc)
				{
					error = std::format(L"数据目录迁移校验失败：无法读取文件属性。\n{}\n{}\n\n已保留原目录。",
					                    it->path().native(), narrow_to_wide(typeEc.message()));
					return false;
				}
				continue; // 子目录
			}
			std::error_code sizeEc;
			const std::uintmax_t size = it->file_size(sizeEc);
			if (sizeEc)
			{
				error = std::format(L"数据目录迁移校验失败：无法读取文件大小。\n{}\n{}\n\n已保留原目录。",
				                    it->path().native(), narrow_to_wide(sizeEc.message()));
				return false;
			}
			std::wstring rel = it->path().native();
			if (rel.size() > prefix.size() &&
			    _wcsnicmp(rel.c_str(), prefix.c_str(), prefix.size()) == 0)
			{
				rel = rel.substr(prefix.size());
			}
			files[path_key(std::move(rel))] = static_cast<std::uint64_t>(size);
			bytes += static_cast<std::uint64_t>(size);
		}
		if (ec)
		{
			error = std::format(L"数据目录迁移校验失败：读取目录失败。\n{}\n{}\n\n已保留原目录。",
			                    root.native(), narrow_to_wide(ec.message()));
			return false;
		}
		return true;
	}

	// 逐文件校验 from/Env、from/bin 是否已完整落到 to 下（大小 + SHA-256）。
	// 目标侧多出的文件只统计、不判失败（例如目标根的提示文件，或易变文件）。
	bool verify_copy_trees(const std::filesystem::path& from, const std::filesystem::path& to,
	                       MigrationState& state, std::wstring& error)
	{
		namespace fs = std::filesystem;
		const std::array<std::pair<fs::path, fs::path>, 2> pairs{{
			{from / L"Env", to / L"Env"},
			{from / L"bin", to / L"bin"},
		}};

		// 先统计要校验的文件数，供进度框显示 "x / y"
		std::uint64_t totalFiles = 0;
		for (const auto& pair : pairs)
		{
			std::unordered_map<std::wstring, std::uint64_t> files;
			std::uint64_t bytes = 0;
			if (!scan_tree(pair.first, files, bytes, error))
			{
				return false;
			}
			for (const auto& entry : files)
			{
				if (!is_transient_path(entry.first))
				{
					++totalFiles;
				}
			}
		}
		state.verifyTotalFiles.store(totalFiles);
		state.verifyFiles.store(0);

		std::uint64_t verifiedFiles = 0;
		std::uint64_t verifiedBytes = 0;
		for (const auto& [srcRoot, dstRoot] : pairs)
		{
			std::unordered_map<std::wstring, std::uint64_t> srcFiles;
			std::unordered_map<std::wstring, std::uint64_t> dstFiles;
			std::uint64_t srcBytes = 0;
			std::uint64_t dstBytes = 0;
			if (!scan_tree(srcRoot, srcFiles, srcBytes, error) || !scan_tree(dstRoot, dstFiles, dstBytes, error))
			{
				return false;
			}
			for (const auto& [rel, size] : srcFiles)
			{
				if (is_transient_path(rel))
				{
					continue;
				}
				const auto found = dstFiles.find(rel);
				if (found == dstFiles.end())
				{
					error = std::format(L"数据目录迁移校验失败：目标目录缺少文件。\n源文件：{}\n目标目录：{}\n\n"
					                    L"已保留原目录，可重新切换磁盘再试。",
					                    (srcRoot / rel).native(), dstRoot.native());
					return false;
				}
				if (found->second != size)
				{
					error = std::format(L"数据目录迁移校验失败：文件大小不一致。\n源文件：{}\n"
					                    L"源大小：{} 字节\n目标大小：{} 字节\n\n已保留原目录。",
					                    (srcRoot / rel).native(), size, found->second);
					return false;
				}
				std::array<unsigned char, 32> srcDigest{};
				std::array<unsigned char, 32> dstDigest{};
				if (!sha256_file(srcRoot / rel, srcDigest))
				{
					error = std::format(L"数据目录迁移校验失败：无法读取源文件。\n{}\n"
					                    L"（可能仍有程序占用该文件）\n\n已保留原目录。",
					                    (srcRoot / rel).native());
					return false;
				}
				if (!sha256_file(dstRoot / rel, dstDigest))
				{
					error = std::format(L"数据目录迁移校验失败：无法读取目标文件。\n{}\n\n已保留原目录。",
					                    (dstRoot / rel).native());
					return false;
				}
				if (srcDigest != dstDigest)
				{
					error = std::format(L"数据目录迁移校验失败：文件内容不一致（SHA-256 不同）。\n"
					                    L"源文件：{}\n\n这通常是因为复制时该文件仍被程序写入。\n\n已保留原目录。",
					                    (srcRoot / rel).native());
					return false;
				}
				++verifiedFiles;
				verifiedBytes += size;
				state.verifyFiles.fetch_add(1, std::memory_order_relaxed);
			}
		}
		state.verifiedFileCount = verifiedFiles;
		state.verifiedByteCount = verifiedBytes;
		return true;
	}

	// ---- 迁移完成标记 ----
	// 写在目标目录根：记录"这次迁移已经逐文件校验通过、DataDir 已切换"。
	// 删除原目录阶段被中断（断电/强杀/蓝屏）后，下次启动据此只续做删除，
	// 绝不重新复制——此时源目录已被删掉一部分，重拷会用好数据覆盖成残缺数据。
	constexpr wchar_t MIGRATION_MARKER[] = L".ebox-migration-complete.txt";

	// ---- 迁移进行中标记 ----
	// 同样写在目标目录根：开始复制前落盘，记录"目标目录里的 Env/bin 是本次迁移写入的
	// 半成品"。复制阶段被中断（断电/强杀/蓝屏）后，目标目录会留下半套文件，下次启动
	// 必须清掉再重拷；但目标根下也可能本来就有用户自己的数据（挂起项陈旧、用户手工把
	// 旧数据拷回目标盘、目标目录被别的程序写入）。没有本标记就说明那些文件不是我们写的，
	// 此时清理会静默删掉真实数据，因此必须先确认来源一致。
	constexpr wchar_t MIGRATION_INCOMPLETE_MARKER[] = L".ebox-migration-incomplete.txt";

	struct MigrationMarker
	{
		std::wstring from;
		std::uint64_t files{0};
		std::uint64_t bytes{0};
	};

	std::uint64_t parse_count(const std::wstring& text)
	{
		std::uint64_t value = 0;
		for (const wchar_t ch : text)
		{
			if (ch < L'0' || ch > L'9')
			{
				return 0;
			}
			value = value * 10 + static_cast<std::uint64_t>(ch - L'0');
		}
		return value;
	}

	// 读取标记文件全文（UTF-16），失败返回空串
	std::wstring read_marker_text(const std::filesystem::path& path)
	{
		HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return {};
		}
		LARGE_INTEGER size{};
		std::wstring text;
		if (::GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 64 * 1024)
		{
			text.resize(static_cast<std::size_t>(size.QuadPart) / sizeof(wchar_t));
			DWORD read = 0;
			if (::ReadFile(file, text.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) && read > 0)
			{
				text.resize(read / sizeof(wchar_t));
			}
			else
			{
				text.clear();
			}
		}
		::CloseHandle(file);
		return text;
	}

	// 写标记文件（UTF-16、隐藏属性）；返回 false 表示落盘失败
	bool write_marker_text(const std::filesystem::path& path, const std::wstring& text)
	{
		HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
		                            FILE_ATTRIBUTE_HIDDEN, nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		const DWORD wanted = static_cast<DWORD>(text.size() * sizeof(wchar_t));
		DWORD written = 0;
		const bool ok = ::WriteFile(file, text.data(), wanted, &written, nullptr) != FALSE && written == wanted;
		::FlushFileBuffers(file);
		::CloseHandle(file);
		return ok;
	}

	bool write_migration_marker(const std::filesystem::path& to, const std::wstring& from, std::uint64_t files,
	                            std::uint64_t bytes)
	{
		const std::wstring text =
			std::format(L"version=1\r\nfrom={}\r\nfiles={}\r\nbytes={}\r\n", from, files, bytes);
		return write_marker_text(to / MIGRATION_MARKER, text);
	}

	// 开始复制前落盘的「迁移进行中」标记
	bool write_incomplete_marker(const std::filesystem::path& to, const std::wstring& from)
	{
		return write_marker_text(to / MIGRATION_INCOMPLETE_MARKER, std::format(L"version=1\r\nfrom={}\r\n", from));
	}

	// 读「迁移进行中」标记里记录的来源目录；不存在/损坏时返回空串
	std::wstring read_incomplete_marker_from(const std::filesystem::path& to)
	{
		const std::wstring text = read_marker_text(to / MIGRATION_INCOMPLETE_MARKER);
		std::size_t start = 0;
		while (start < text.size())
		{
			std::size_t end = text.find(L'\n', start);
			if (end == std::wstring::npos)
			{
				end = text.size();
			}
			std::wstring line = text.substr(start, end - start);
			start = end + 1;
			while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n'))
			{
				line.pop_back();
			}
			const auto eq = line.find(L'=');
			if (eq != std::wstring::npos && line.substr(0, eq) == L"from")
			{
				return line.substr(eq + 1);
			}
		}
		return {};
	}

	MigrationMarker read_migration_marker(const std::filesystem::path& to)
	{
		MigrationMarker marker;
		const std::wstring text = read_marker_text(to / MIGRATION_MARKER);

		std::size_t start = 0;
		while (start < text.size())
		{
			std::size_t end = text.find(L'\n', start);
			if (end == std::wstring::npos)
			{
				end = text.size();
			}
			std::wstring line = text.substr(start, end - start);
			start = end + 1;
			while (!line.empty() && (line.back() == L'\r' || line.back() == L'\n'))
			{
				line.pop_back();
			}
			const auto eq = line.find(L'=');
			if (eq == std::wstring::npos)
			{
				continue;
			}
			const std::wstring key = line.substr(0, eq);
			const std::wstring value = line.substr(eq + 1);
			if (key == L"from")
			{
				marker.from = value;
			}
			else if (key == L"files")
			{
				marker.files = parse_count(value);
			}
			else if (key == L"bytes")
			{
				marker.bytes = parse_count(value);
			}
		}
		return marker;
	}

	// 清掉目标目录根下的两个迁移标记。所有"迁移结束/放弃"的路径都必须调用：
	// 完整标记是下次启动「只续做删除」的依据，进行中标记是"目标数据由我们写入"的凭据，
	// 任一残留都会让下一次迁移误判目标目录的来源。
	void delete_migration_marker(const std::filesystem::path& to)
	{
		std::error_code ec;
		std::filesystem::remove(to / MIGRATION_MARKER, ec);
		ec.clear();
		std::filesystem::remove(to / MIGRATION_INCOMPLETE_MARKER, ec);
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
			const bool phaseChanged = state->uiPhase != phase;
			if (phaseChanged)
			{
				state->uiPhase = phase;
				// 0=复制、2=删除原目录 有确定的字节/文件进度；1=校验、3=清理半成品 用不确定进度
				SendMessageW(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR,
				             (phase == 0 || phase == 2) ? FALSE : TRUE, 0);
				state->shownText.clear();
				if (phase == 0)
				{
					// 清理完目标残骸回到复制阶段，进度条从 0 重新走
					SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_POS, 0, 0);
				}
			}
			if (phase != 0)
			{
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
				if (phase == 1)
				{
					// 逐文件校验（大小 + SHA-256）有明确文件数，显示 x / y 让用户看到在推进
					if (++state->uiTick % 5 != 0 && !state->shownText.empty())
					{
						return S_OK;
					}
					const std::uint64_t verifyFiles = state->verifyFiles.load();
					const std::uint64_t verifyTotal = state->verifyTotalFiles.load();
					const std::wstring text =
						verifyTotal > 0
							? std::format(L"文件已复制完成，正在逐文件校验数据内容：{} / {} 个文件，请稍候…\n\n"
							              L"每个文件都会比对大小与 SHA-256，确认与原目录完全一致后才切换数据目录。",
							              verifyFiles, verifyTotal)
							: std::wstring{L"文件已复制完成，正在校验数据，请稍候…"};
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
					const std::wstring text = L"迁移未完成，正在清理目标目录中的临时文件…";
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
		ec.clear();
		const fs::path to = fs::weakly_canonical(fs::path{toStr}, ec);

		MigrationState state;
		state.sourceDir = from.native();
		state.targetDir = to.native();

		// 续做判定：DataDir 已经指向目标目录，说明上一轮的"复制 + 逐文件校验"已全部通过，
		// 只是删除原目录的阶段被打断（断电/强杀/蓝屏）。此时源目录已被删掉一部分，
		// 绝不能重新复制 —— 那会用好数据覆盖成残缺数据（客户资料数量对不上的直接原因）。
		const std::wstring dataDirStr = read_reg_value(REG_VALUE_DATA_DIR);
		if (!dataDirStr.empty() && !to.empty())
		{
			std::error_code dirEc;
			const fs::path dataDir = fs::weakly_canonical(fs::path{dataDirStr}, dirEc);
			if (!dirEc && _wcsicmp(dataDir.native().c_str(), to.native().c_str()) == 0)
			{
				state.resumeDelete = true;
			}
		}

		// cleanupPartial=true 时清掉目标侧已复制的一半数据：否则下次切换会因
		// "目标目录已存在 Env" 被拒绝，用户再也切换不回去。
		// 续做删除时目标侧数据已校验通过、DataDir 正在使用，任何情况下都不能删。
		const auto abort = [&state, &to](const std::wstring& msg, bool cleanupPartial)
		{
			delete_reg_value(REG_VALUE_PENDING_FROM);
			delete_reg_value(REG_VALUE_PENDING_TO);
			if (cleanupPartial && !to.empty() && !state.resumeDelete)
			{
				std::error_code rmEc;
				fs::remove_all(to / L"Env", rmEc);
				fs::remove_all(to / L"bin", rmEc);
				delete_migration_marker(to);
			}
			MessageBoxW(nullptr, msg.c_str(), MainApp::appName.data(), MB_OK | MB_ICONERROR | MB_TASKMODAL);
		};

		if (from.empty() || to.empty())
		{
			abort(L"数据目录迁移失败：路径无效，已保留原目录。", false);
			return;
		}

		// 同一目录或互相包含的一组校验：这两种情况下"复制 → 切换 → 删源"会打到正在使用的
		// 数据本身。复制到自身只是空转，随后删源等于删掉唯一一份数据；目标在源内部时，
		// 删源会连带删掉刚复制好的目标；源在目标内部时，worker 开头清理目标 Env/bin
		// 会直接删掉源。任何一条都会造成环境数据（企微聊天记录/客户资料）整体丢失，
		// 因此必须在动任何文件之前拒绝执行。
		const std::wstring fromPath = from.native();
		const std::wstring toPath = to.native();
		const bool sameDir = _wcsicmp(fromPath.c_str(), toPath.c_str()) == 0;
		const bool nestedDir = path_within(toPath, fromPath) || path_within(fromPath, toPath);
		if (sameDir || nestedDir)
		{
			// 绝不能 cleanupPartial：目标与源重叠时清目标就可能清掉源里的真实数据
			abort(std::format(L"数据目录迁移已取消：目标目录与当前数据目录相同或存在包含关系。\n\n"
			                  L"当前数据目录：{}\n目标目录：{}\n\n"
			                  L"请选择一个不重叠的独立目录后再切换磁盘。已保留现有数据。",
			                  fromPath, toPath),
			      false);
			return;
		}

		std::vector<CopyTask> plan;
		if (state.resumeDelete)
		{
			// 必须凭上一轮写下的完成标记确认"目标目录确实是这次迁移的产物"；
			// 标记缺失或来源不一致时不做任何删除，只清挂起项，避免误删用户数据。
			const MigrationMarker marker = read_migration_marker(to);
			if (marker.from.empty() || _wcsicmp(marker.from.c_str(), from.native().c_str()) != 0)
			{
				delete_reg_value(REG_VALUE_PENDING_FROM);
				delete_reg_value(REG_VALUE_PENDING_TO);
				delete_migration_marker(to);
				MessageBoxW(nullptr,
				            std::format(L"上次的数据目录迁移记录不完整，已取消删除原目录。\n\n"
				                        L"当前数据目录：{}\n原目录（未删除）：{}",
				                        to.native(), from.native())
				                .c_str(),
				            MainApp::appName.data(), MB_OK | MB_ICONWARNING | MB_TASKMODAL);
				return;
			}

			// 守卫：目标侧已经找不到数据（目标盘未接/被清空）时，删源就等于彻底丢数据。
			// 把数据目录指回原目录并保留原目录，让用户检查目标盘后重新切换。
			std::uint64_t dstFiles = 0;
			std::uint64_t dstBytes = 0;
			count_delete_tree(to / L"Env", dstFiles, dstBytes);
			count_delete_tree(to / L"bin", dstFiles, dstBytes);
			// 目标侧数量少于标记记录的量（目标盘未接/被清空/只恢复了一半）时删源就等于丢数据。
			// count_delete_tree 统计的是目标全量（含日志等易变文件），正常情况只会比标记值多。
			const bool dstIncomplete =
				dstFiles == 0 || dstBytes == 0 || (marker.files > 0 && (dstFiles < marker.files || dstBytes < marker.bytes));
			if (dstIncomplete)
			{
				write_reg_value(REG_VALUE_DATA_DIR, from.native());
				delete_reg_value(REG_VALUE_PENDING_FROM);
				delete_reg_value(REG_VALUE_PENDING_TO);
				delete_migration_marker(to);
				MessageBoxW(nullptr,
				            std::format(L"目标目录中的数据不完整（少于迁移完成时校验的数量），已取消删除原目录。\n\n"
				                        L"迁移时校验：{} 个文件（{}）\n目标目录现有：{} 个文件（{}）\n\n"
				                        L"数据目录已指回原目录：{}\n请确认目标磁盘可用后重新切换磁盘。",
				                        marker.files, format_size(marker.bytes), dstFiles, format_size(dstBytes),
				                        from.native())
				                .c_str(),
				            MainApp::appName.data(), MB_OK | MB_ICONERROR | MB_TASKMODAL);
				return;
			}

			// 原目录已经删干净（例如上次在删除收尾时退出）：无需再做任何事
			std::error_code existEc;
			if (!fs::exists(from / L"Env", existEc) && !fs::exists(from / L"bin", existEc))
			{
				delete_reg_value(REG_VALUE_PENDING_FROM);
				delete_reg_value(REG_VALUE_PENDING_TO);
				delete_migration_marker(to);
				return;
			}

			// 只续做删除：跳过复制与校验，绝不用已被部分删除的源目录覆盖目标
			state.phase = 2;
			state.delScanning = true;
		}
		else
		{
			fs::create_directories(to, ec);
			if (ec)
			{
				abort(std::format(L"数据目录迁移失败：无法创建目标目录。\n{}\n\n已保留原目录。", to.native()), false);
				return;
			}

			// 目标目录守卫：worker 开头会先清空 to\Env、to\bin 再复制。若里面已经有不是本次
			// 迁移写入的文件（挂起项陈旧、用户手工把旧数据拷回目标盘、目标目录被别的程序写
			// 入），这一步会静默删掉真实数据。只有「迁移进行中」标记记录的来源与本次一致，
			// 才能确认这些文件是上次中断留下的半成品、可以清掉重拷。
			const auto target_has_entries = [](const std::filesystem::path& dir)
			{
				std::error_code dirEc;
				if (!std::filesystem::is_directory(dir, dirEc) || dirEc)
				{
					return false;
				}
				std::error_code emptyEc;
				const bool empty = std::filesystem::is_empty(dir, emptyEc);
				// 读不到内容时保守按"有数据"处理：宁可让用户手工确认，也不误删真实数据
				return emptyEc ? true : !empty;
			};
			if (target_has_entries(to / L"Env") || target_has_entries(to / L"bin"))
			{
				const std::wstring writtenFrom = read_incomplete_marker_from(to);
				if (writtenFrom.empty() || _wcsicmp(writtenFrom.c_str(), from.native().c_str()) != 0)
				{
					// 目标里不是我们写的数据：既不能清，也不能把 DataDir 切过去
					std::uint64_t dstFiles = 0;
					std::uint64_t dstBytes = 0;
					count_delete_tree(to / L"Env", dstFiles, dstBytes);
					count_delete_tree(to / L"bin", dstFiles, dstBytes);
					abort(std::format(L"数据目录迁移已取消：目标目录中已有数据，且不是上次迁移中断留下的半成品。\n\n"
					                  L"目标目录：{}\n其中已有 {} 个文件（{}）\n\n"
					                  L"为避免覆盖这些数据，已保留当前数据目录：{}\n"
					                  L"请换一个空目录，或先备份并清空目标目录后重新切换磁盘。",
					                  to.native(), dstFiles, format_size(dstBytes), from.native()),
					      false);
					return;
				}
			}
			if (!write_incomplete_marker(to, from.native()))
			{
				abort(L"数据目录迁移失败：无法在目标目录写入迁移标记（可能被安全软件拦截），已保留原目录。", false);
				return;
			}

			// 迁移前先结束数据目录下的环境子进程：它们是 cmd /c start 拉起的孤儿进程，
			// 不随 eBox 退出而退出，常驻持有 Env 内的 hive/数据库文件，是迁移失败的主要根因。
			terminate_processes_under(state.sourceDir, state, nullptr);
			// 再结束映像不在源目录、却占着源目录数据文件的进程（企业微信本体、数据库等）：
			// 不结束它们，WAL 模式的 SQLite 会在复制期间继续写入，拷出来是撕裂的快照。
			terminate_lockers_under_tree(from, state);

			// 生成复制计划：一次枚举拿到全部文件与总大小（顺带在目标侧建目录）
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
		}

		// 复制/校验失败时清掉目标侧已复制的一半数据：否则下次切换会因"目标目录已存在 Env"被拒绝。
		// 这一步也在 worker 里做（phase=3），几十 GB 的删除同样有进度提示，不会静默卡住。
		// 续做删除时目标侧数据已校验通过、DataDir 正在使用，绝不能清目标。
		const auto fail_migration = [&state, &to](std::wstring msg)
		{
			if (!state.resumeDelete)
			{
				state.phase = 3;
				std::error_code rmEc;
				fs::remove_all(to / L"Env", rmEc);
				fs::remove_all(to / L"bin", rmEc);
				std::error_code checkEc;
				if (!fs::exists(to / L"Env", checkEc) && !fs::exists(to / L"bin", checkEc))
				{
					delete_migration_marker(to);
				}
				else
				{
					// 半成品没清干净（被占用/被拦截）时必须留着「进行中」标记：下次启动
					// 才能确认目标里的残留是本次迁移写的，否则会被当成别人的数据而卡死。
					std::error_code markerEc;
					fs::remove(to / MIGRATION_MARKER, markerEc);
				}
			}
			{
				std::lock_guard lock{state.textMutex};
				state.error = std::move(msg);
			}
			state.status = 2;
		};

		// 复制、校验、删除旧数据全部在同一个 worker 里完成：进度框只有 worker 真正结束才关闭，
		// 避免"框已消失、后台还在删几十 GB 旧数据"的假死感。
		std::jthread worker{[&state, &plan, &to, &from, &fail_migration]
		{
			if (!state.resumeDelete)
			{
				// 清掉目标侧上一次失败留下的半成品：这些文件在源目录里可能已不存在，
				// 留着会被当成"迁移过来的数据"混进新数据目录。
				state.phase = 3;
				{
					std::error_code rmEc;
					fs::remove_all(to / L"Env", rmEc);
					fs::remove_all(to / L"bin", rmEc);
				}
				// 只清"完成"标记；"进行中"标记必须保留到本次迁移成功——复制再次中断时，
				// 下次启动正是靠它确认目标里的半成品是我们写的、可以清掉重拷。
				{
					std::error_code markerEc;
					fs::remove(to / MIGRATION_MARKER, markerEc);
				}
				state.phase = 0;

				copy_plan(plan, state);
				if (state.stop.load())
				{
					fail_migration(state.error);
					return;
				}
				// 复制期间源文件被删掉/移走（外部程序、清理工具）时，校验同样查不出来
				// —— 校验重新扫描源目录，那个文件已经不存在了。源已不可信，必须中止。
				const int vanished = state.skippedCount.load();
				if (vanished > 0)
				{
					fail_migration(std::format(
						L"数据目录迁移失败：复制期间有 {} 个源文件消失，无法确认数据完整。\n\n"
						L"请先退出所有环境（含企业微信）后重新切换磁盘。\n\n已保留原目录。",
						vanished));
					return;
				}
				state.phase = 1; // 校验数据
				std::wstring verifyError;
				if (!verify_copy_trees(from, to, state, verifyError))
				{
					fail_migration(std::move(verifyError));
					return;
				}
				// 先落"复制 + 校验已完成"的标记，再切换 DataDir：
				// 两步之间无论在哪一步中断，都不会出现"DataDir 已切换但数据不可信"。
				// 标记也是下次启动「只续做删除、绝不重拷」的唯一依据。
				if (!write_migration_marker(to, from.native(), state.verifiedFileCount,
				                            state.verifiedByteCount))
				{
					fail_migration(L"数据目录迁移失败：无法写入迁移完成标记，已保留原目录。");
					return;
				}
				// 复制并逐文件校验完成才切换 DataDir（写失败同样视为迁移失败，避免"指向空目录"）
				if (!write_reg_value(REG_VALUE_DATA_DIR, to.native()))
				{
					fail_migration(L"数据目录迁移失败：写入配置失败，已保留原目录。");
					return;
				}
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
		cfg.pszMainInstruction =
			state.resumeDelete ? L"正在清理原有数据目录，请稍候…" : L"正在迁移环境数据，请稍候…";
		const std::wstring content =
			state.resumeDelete
				? std::format(L"上次迁移的数据已逐文件校验通过，数据目录已切换为：\n{}\n\n"
				              L"现在只需删除原目录中的旧数据。\n原目录：{}\n\n"
				              L"删除期间请勿关闭电脑或强制结束进程。",
				              to.native(), from.native())
				: std::format(L"从：{}\n到：{}\n\n迁移期间请勿关闭电脑或强制结束进程。", fromStr, toStr);
		cfg.pszContent = content.c_str();
		cfg.pfCallback = migration_dlg_callback;
		cfg.lpCallbackData = reinterpret_cast<LONG_PTR>(&state);
		TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr);

		worker.join();

		if (state.status.load() != 1)
		{
			// 目标侧的半成品已由 worker 清掉（续做模式不会动目标），这里不用再删一遍
			abort(state.error.empty() ? std::wstring{L"数据目录迁移失败，已保留原目录。"} : state.error, false);
			return;
		}

		// 成功：清挂起项与完成标记（旧数据已在 worker 里删除）
		delete_reg_value(REG_VALUE_PENDING_FROM);
		delete_reg_value(REG_VALUE_PENDING_TO);
		delete_migration_marker(to);
		const bool oldKept = state.oldDeleteFailed;

		std::wstring done;
		if (state.resumeDelete)
		{
			done = std::format(L"数据目录迁移已完成（续做删除原目录），数据目录为：\n{}", to.native());
		}
		else
		{
			done = std::format(L"环境数据已迁移完成，数据目录已切换为：\n{}\n\n"
			                   L"已逐文件校验 {} 个文件（{}），与原目录内容完全一致。",
			                   to.native(), state.verifiedFileCount, format_size(state.verifiedByteCount));
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

			// 未提权时 hive 用不上（环境注册表走用户注册表存储），不检查 hive 文件，
			// 否则普通用户会一直看到"注册表数据无效"的误报
			if (!envWritable || inUse || !holds_hive_privileges())
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
	// allowElevation = true：当前未提权时通过 UAC 提权执行；false：只用现有权限执行，不弹 UAC
	bool add_defender_exclusions(bool allowElevation = true)
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
		if (allowElevation)
		{
			siw.lpVerb = L"runas";
		}
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

	// 是否已成功加过排除项（成功一次后不再重复执行 PowerShell）
	constexpr wchar_t REG_VALUE_DEFENDER_EXCLUDED[] = L"DefenderExcluded";

	// 启动时静默加入 Defender 排除项：后台线程执行，不阻塞启动、不打扰用户。
	// eBox 通常以管理员身份运行，无需 UAC；未提权则直接跳过（留给"手动处理"流程提示用户）。
	void ensure_defender_exclusions_async()
	{
		if (read_reg_value(REG_VALUE_DEFENDER_EXCLUDED) == L"1")
		{
			return;
		}
		std::thread{[]()
		{
			HANDLE token = nullptr;
			bool elevated = false;
			if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			{
				TOKEN_ELEVATION elevation{};
				DWORD size = sizeof(elevation);
				if (GetTokenInformation(token, TokenElevation, &elevation, size, &size))
				{
					elevated = elevation.TokenIsElevated != 0;
				}
				CloseHandle(token);
			}
			if (!elevated)
			{
				return;
			}
			if (add_defender_exclusions(false))
			{
				write_reg_value(REG_VALUE_DEFENDER_EXCLUDED, L"1");
			}
		}}.detach();
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

	// ---- 设置中心「修复企业微信数据」----
	// 背景：3.2.0/3.2.1 的「切换数据盘」在删源阶段被中断后，下次启动会用残缺的源覆盖目标，
	// 造成企业微信 Data 目录里 crm.db / user.db 等库缺失或变小（表现为客户备注、标签、
	// 添加时间全部消失，点「添加为联系人」才恢复）。企微检测到数据异常时会就地留一份
	// Backup\<时间戳>\Data 快照，这里按企业（corp）把当前库与快照比对，缺的、明显变小的回填。
	constexpr std::uint64_t WXWORK_DB_MIN_COMPARE_BYTES = 64ull * 1024ull; // 小库本身会波动，不参与比对

	struct WxWorkDbIssue
	{
		std::wstring fileName;
		std::uint64_t liveSize{0};
		std::uint64_t backupSize{0};
		std::filesystem::path backupFile;
		bool missing{false};
	};

	struct WxWorkCorpIssue
	{
		std::wstring envLabel;
		std::wstring corp;
		std::filesystem::path dataDir;
		std::vector<WxWorkDbIssue> files;
	};

	// 环境内的「文档」相对路径（与 MemoryDll 的重定向规则一致：去掉盘符）
	std::filesystem::path documents_rel_path()
	{
		PWSTR docs = nullptr;
		if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs) != S_OK || docs == nullptr)
		{
			return {};
		}
		const std::filesystem::path rel{std::filesystem::path{docs}.relative_path()};
		CoTaskMemFree(docs);
		return rel;
	}

	// 企微的企业目录名是纯数字的企业 id（排除 Cache / Global 等公共目录）
	bool is_corp_dir_name(const std::wstring& name)
	{
		if (name.size() < 10)
		{
			return false;
		}
		return std::all_of(name.begin(), name.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
	}

	// 该企业下企微自带的全部本地备份（<corp>\Backup\<时间戳>\Data），最新的在前
	std::vector<std::filesystem::path> list_wxwork_backups(const std::filesystem::path& corpDir)
	{
		namespace fs = std::filesystem;
		std::vector<std::pair<fs::file_time_type, fs::path>> found;
		std::error_code ec;
		const fs::path backupRoot = corpDir / L"Backup";
		if (!fs::is_directory(backupRoot, ec) || ec)
		{
			return {};
		}
		std::error_code iterEc;
		for (const auto& entry : fs::directory_iterator(backupRoot, fs::directory_options::skip_permission_denied, iterEc))
		{
			std::error_code entryEc;
			if (!entry.is_directory(entryEc) || entryEc)
			{
				continue;
			}
			const fs::path data = entry.path() / L"Data";
			if (!fs::is_directory(data, entryEc) || entryEc)
			{
				continue;
			}
			const auto time = fs::last_write_time(entry.path(), entryEc);
			if (entryEc)
			{
				continue;
			}
			found.emplace_back(time, data);
		}
		std::sort(found.begin(), found.end(),
		          [](const auto& a, const auto& b) { return a.first > b.first; });
		std::vector<fs::path> result;
		result.reserve(found.size());
		for (auto& item : found)
		{
			result.push_back(std::move(item.second));
		}
		return result;
	}

	void scan_wxwork_corps(std::vector<WxWorkCorpIssue>& issues, std::size_t& envCount,
	                       std::size_t& corpCount, std::size_t& noBackupCount)
	{
		namespace fs = std::filesystem;
		const fs::path rel = documents_rel_path();
		if (rel.empty())
		{
			return;
		}
		for (const auto& env : biz::env_mgr().getAllEnv())
		{
			++envCount;
			const fs::path wxRoot = fs::path{app().envDataRoot()} / L"Env" /
				std::format(L"{}", env->getIndex()) / rel / L"WXWork";
			std::error_code rootEc;
			if (!fs::is_directory(wxRoot, rootEc) || rootEc)
			{
				continue;
			}
			const std::wstring envLabel = env->getName().empty()
				? std::format(L"环境 {}", env->getIndex())
				: std::format(L"环境「{}」", env->getName());
			std::error_code iterEc;
			for (const auto& entry : fs::directory_iterator(wxRoot, fs::directory_options::skip_permission_denied, iterEc))
			{
				std::error_code entryEc;
				if (!entry.is_directory(entryEc) || entryEc ||
					!is_corp_dir_name(entry.path().filename().native()))
				{
					continue;
				}
				++corpCount;
				const fs::path dataDir = entry.path() / L"Data";
				const std::vector<fs::path> backups = list_wxwork_backups(entry.path());
				if (backups.empty())
				{
					++noBackupCount;
					continue;
				}

				WxWorkCorpIssue corp;
				corp.envLabel = envLabel;
				corp.corp = entry.path().filename().native();
				corp.dataDir = dataDir;

				// 同一个库可能并不在最新一份备份里（备份内容随时间变化），只比对最新备份会
				// 漏掉"最新备份根本没有 crm.db"这种情况 —— 那正是客户备注消失的主因。
				// 这里按文件名汇总所有备份提供的版本，新的在前。
				struct BackupCandidate
				{
					fs::path file;
					std::uint64_t size{0};
				};
				std::map<std::wstring, std::vector<BackupCandidate>> candidates;
				for (const fs::path& backup : backups)
				{
					std::error_code backupEc;
					for (const auto& file :
					     fs::directory_iterator(backup, fs::directory_options::skip_permission_denied, backupEc))
					{
						std::error_code fileEc;
						if (!file.is_regular_file(fileEc) || fileEc || file.path().extension() != L".db")
						{
							continue;
						}
						const std::uint64_t backupSize = file.file_size(fileEc);
						if (fileEc || backupSize == 0)
						{
							continue;
						}
						candidates[file.path().filename().native()].push_back(
							BackupCandidate{file.path(), backupSize});
					}
				}

				// 以备份为基准：备份里有、当前没有或不足备份一半的库，判为不完整
				for (const auto& [name, versions] : candidates)
				{
					const fs::path live = dataDir / name;
					std::error_code liveEc;
					const bool exists = fs::exists(live, liveEc) && !liveEc;
					if (!exists)
					{
						// 缺失：取最新一份含该文件的备份
						WxWorkDbIssue issue;
						issue.fileName = name;
						issue.backupFile = versions.front().file;
						issue.backupSize = versions.front().size;
						issue.missing = true;
						corp.files.push_back(std::move(issue));
						continue;
					}
					const std::uint64_t liveSize = fs::file_size(live, liveEc);
					if (liveEc)
					{
						continue;
					}
					// 明显缩小：优先用最新一份"足以证明当前库不完整"的备份；最新备份本身也残缺时
					// 继续往旧备份找，避免把恢复数据又恢复成残库
					const BackupCandidate* pick = nullptr;
					for (const BackupCandidate& candidate : versions)
					{
						if (candidate.size >= WXWORK_DB_MIN_COMPARE_BYTES && liveSize * 2 < candidate.size)
						{
							pick = &candidate;
							break;
						}
					}
					if (pick == nullptr)
					{
						continue;
					}
					WxWorkDbIssue issue;
					issue.fileName = name;
					issue.liveSize = liveSize;
					issue.backupSize = pick->size;
					issue.backupFile = pick->file;
					corp.files.push_back(std::move(issue));
				}
				if (!corp.files.empty())
				{
					issues.push_back(std::move(corp));
				}
			}
		}
	}

	std::wstring build_wxwork_details(const std::vector<WxWorkCorpIssue>& issues)
	{
		std::wstring text;
		for (std::size_t i = 0; i < issues.size(); ++i)
		{
			const WxWorkCorpIssue& corp = issues[i];
			text += std::format(L"{} {}，企业 {}\n", i + 1, corp.envLabel, corp.corp);
			for (const WxWorkDbIssue& file : corp.files)
			{
				// 备份目录名就是企微的备份时间戳，便于用户判断会退回到哪个时间点
				const std::wstring stamp = file.backupFile.parent_path().parent_path().filename().native();
				if (file.missing)
				{
					text += std::format(L"    {}：缺失，用备份 {}（{}）回填\n", file.fileName, stamp,
					                    format_size(file.backupSize));
				}
				else
				{
					text += std::format(L"    {}：当前 {}，备份 {} 为 {}\n", file.fileName,
					                    format_size(file.liveSize), stamp, format_size(file.backupSize));
				}
			}
			text += L"\n";
		}
		return text;
	}

	// 先把当前文件另存，再从备份回填；失败时尽量还原，避免把库弄成半截
	bool restore_wxwork_db(const WxWorkCorpIssue& corp, const WxWorkDbIssue& issue,
	                       const std::filesystem::path& keepDir, std::wstring& note)
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		const fs::path source = issue.backupFile;
		if (source.empty())
		{
			note = L"未找到可用的备份文件";
			return false;
		}
		const fs::path live = corp.dataDir / issue.fileName;
		fs::create_directories(corp.dataDir, ec);
		if (ec)
		{
			note = std::format(L"无法创建数据目录：{}", win_error_text(static_cast<DWORD>(ec.value())));
			return false;
		}
		fs::create_directories(keepDir, ec);
		if (ec)
		{
			note = std::format(L"无法创建备份目录：{}", win_error_text(static_cast<DWORD>(ec.value())));
			return false;
		}

		// 旧库的 -wal/-shm/-journal 只属于旧文件。回填前必须一并移走：留着会被 SQLite
		// 应用到新库上，同样会把库弄坏。这里移动而不是删除，回滚时能原样放回去。
		std::vector<fs::path> sidecars;
		{
			const std::wstring base = live.native();
			for (const wchar_t* suffix : {L"-wal", L"-shm", L"-journal"})
			{
				sidecars.emplace_back(base + suffix);
			}
		}
		std::vector<std::pair<fs::path, fs::path>> movedSidecars; // <另存位置, 原位置>

		fs::path kept;
		const auto rollback = [&kept, &live, &movedSidecars]()
		{
			if (!kept.empty())
			{
				MoveFileExW(kept.c_str(), live.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
			}
			for (auto& [stored, original] : movedSidecars)
			{
				MoveFileExW(stored.c_str(), original.c_str(),
				            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
			}
		};

		if (fs::exists(live, ec) && !ec)
		{
			kept = keepDir / issue.fileName;
			DeleteFileW(kept.c_str());
			if (!MoveFileExW(live.c_str(), kept.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				note = std::format(L"原文件被占用，无法替换（{}），请先退出该环境", win_error_text(GetLastError()));
				return false;
			}
		}
		for (const fs::path& sidecar : sidecars)
		{
			std::error_code sidecarEc;
			if (!fs::exists(sidecar, sidecarEc) || sidecarEc)
			{
				continue;
			}
			const fs::path stored = keepDir / sidecar.filename();
			DeleteFileW(stored.c_str());
			if (!MoveFileExW(sidecar.c_str(), stored.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				note = std::format(L"旧的 {} 被占用，无法移除（{}），请先退出该环境",
				                   sidecar.filename().native(), win_error_text(GetLastError()));
				rollback();
				return false;
			}
			movedSidecars.emplace_back(stored, sidecar);
		}

		if (!CopyFileW(source.c_str(), live.c_str(), FALSE))
		{
			const DWORD err = GetLastError();
			DeleteFileW(live.c_str());
			rollback();
			note = std::format(L"从备份复制失败：{}", win_error_text(err));
			return false;
		}
		std::error_code sizeEc;
		const std::uint64_t copied = fs::file_size(live, sizeEc);
		if (sizeEc || copied != issue.backupSize)
		{
			DeleteFileW(live.c_str());
			rollback();
			note = L"复制后大小与备份不一致，已还原原文件";
			return false;
		}
		return true;
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

int run_wxwork_data_repair(HWND owner)
{
	std::vector<WxWorkCorpIssue> issues;
	std::size_t envCount = 0;
	std::size_t corpCount = 0;
	std::size_t noBackupCount = 0;
	scan_wxwork_corps(issues, envCount, corpCount, noBackupCount);

	std::size_t fileCount = 0;
	for (const WxWorkCorpIssue& corp : issues)
	{
		fileCount += corp.files.size();
	}

	if (issues.empty())
	{
		std::wstring text = std::format(L"检查完成，未发现企业微信数据缺失。\n\n已检查 {} 个环境、{} 个企业。",
		                                envCount, corpCount);
		if (noBackupCount > 0)
		{
			text += std::format(L"\n其中 {} 个企业没有企微本地备份，无法比对。", noBackupCount);
		}
		MessageBoxW(owner, text.c_str(), MainApp::appName.data(), MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
		return 0;
	}

	// 先列出问题，由用户决定是否回填
	{
		const std::wstring instruction = std::format(L"发现 {} 个企业的 {} 个数据文件不完整", issues.size(), fileCount);
		const std::wstring details = build_wxwork_details(issues);
		std::wstring content = L"这些文件不完整时，企业微信对话列表会丢失客户备注、标签与添加时间。\n"
		                       L"修复会用企微自带的本地备份回填，当前文件先另存到 .ebox-repair-backup 目录。\n\n"
		                       L"注意：回填会把对应文件恢复到备份生成时的状态，备份之后新增的记录可能需要"
		                       L"企微重新同步才会出现。\n\n"
		                       L"请先退出对应环境，否则文件被占用会导致修复失败。";
		if (noBackupCount > 0)
		{
			content += std::format(L"\n另有 {} 个企业没有企微本地备份，无法比对。", noBackupCount);
		}
		TASKDIALOGCONFIG cfg{sizeof(TASKDIALOGCONFIG)};
		cfg.hwndParent = owner;
		cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_EXPANDED_BY_DEFAULT;
		cfg.pszWindowTitle = MainApp::appName.data();
		cfg.pszMainIcon = TD_WARNING_ICON;
		cfg.pszMainInstruction = instruction.c_str();
		cfg.pszContent = content.c_str();
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

	SYSTEMTIME st{};
	GetLocalTime(&st);
	const std::wstring stamp = std::format(L"{:04}{:02}{:02}-{:02}{:02}{:02}", st.wYear, st.wMonth,
	                                       st.wDay, st.wHour, st.wMinute, st.wSecond);

	std::size_t fixedCount = 0;
	std::vector<std::wstring> failures;
	for (const WxWorkCorpIssue& corp : issues)
	{
		const std::filesystem::path keepDir = corp.dataDir.parent_path() / L".ebox-repair-backup" / stamp;
		for (const WxWorkDbIssue& file : corp.files)
		{
			std::wstring note;
			if (restore_wxwork_db(corp, file, keepDir, note))
			{
				++fixedCount;
			}
			else
			{
				failures.push_back(std::format(L"{} {} / {}：{}", corp.envLabel, corp.corp, file.fileName, note));
			}
		}
	}

	if (failures.empty())
	{
		MessageBoxW(owner,
		            std::format(L"修复完成：已回填 {} 个数据文件。\n\n"
		                        L"请重新启动对应环境并登录企业微信，确认客户列表已恢复正常。",
		                        fixedCount).c_str(),
		            MainApp::appName.data(), MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
		return 1;
	}

	std::wstring details;
	for (const std::wstring& line : failures)
	{
		details += line;
		details += L"\n";
	}
	const std::wstring instruction = std::format(L"有 {} 个数据文件未能修复", failures.size());
	TASKDIALOGCONFIG cfg{sizeof(TASKDIALOGCONFIG)};
	cfg.hwndParent = owner;
	cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_EXPANDED_BY_DEFAULT;
	cfg.pszWindowTitle = MainApp::appName.data();
	cfg.pszMainIcon = TD_ERROR_ICON;
	cfg.pszMainInstruction = instruction.c_str();
	cfg.pszContent = L"文件被占用是最常见的原因。请先退出对应环境（含企微相关进程）后，"
	                 L"再点击「修复企业微信数据」重试。";
	cfg.pszExpandedInformation = details.c_str();
	cfg.dwCommonButtons = TDCBF_CLOSE_BUTTON;
	TaskDialogIndirect(&cfg, nullptr, nullptr, nullptr);
	return 2;
}

bool request_data_dir_change(const std::wstring& targetDir)
{
	try
	{
		const std::wstring from{app().envDataRoot()};
		if (targetDir.empty())
		{
			return false;
		}
		// 与启动期迁移同一套判定：相同目录或互相包含时，"复制 + 删除原目录"会直接毁掉
		// 正在使用的环境数据，这里提前拒绝，避免先写挂起项、重启后才失败。
		std::error_code fromEc;
		std::error_code toEc;
		const std::wstring fromPath = std::filesystem::weakly_canonical(std::filesystem::path{from}, fromEc).native();
		const std::wstring toPath =
			std::filesystem::weakly_canonical(std::filesystem::path{targetDir}, toEc).native();
		if (fromEc || toEc || fromPath.empty() || toPath.empty())
		{
			return false;
		}
		if (_wcsicmp(fromPath.c_str(), toPath.c_str()) == 0 || path_within(toPath, fromPath) ||
			path_within(fromPath, toPath))
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
