module;
#include <windows.h>
#include <tlhelp32.h>

module Launcher;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import std;
import MainApp;
import EssentialData;
import Utility.SystemInfo;
import Biz.Core;
import biz.License;

namespace
{
	PROCESS_INFORMATION create_and_inject(const biz::Env* env, std::wstring_view exePath, std::wstring_view params)
	{
		PROCESS_INFORMATION procInfo = {nullptr};
		STARTUPINFOW startupInfo = {sizeof(startupInfo)};
		startupInfo.dwFlags = STARTF_USESHOWWINDOW;
		startupInfo.wShowWindow = SW_HIDE;

		namespace fs = std::filesystem;
		const fs::path cmdPath{fs::weakly_canonical(fs::path{sys_info::get_system_dir()} / fs::path{L"cmd.exe"})};
		std::wstring cmdLine = params.empty() ? std::format(LR"(/c start "" "{}")", exePath) : std::format(LR"(/c start "" "{}" {})", exePath, params);
		if (!DetourCreateProcessWithDllExW(cmdPath.c_str(), cmdLine.data(), nullptr, nullptr, 0,
		                                   CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED, nullptr,
		                                   std::filesystem::path{exePath}.parent_path().native().c_str(), &startupInfo, &procInfo,
		                                   env->ensureDllInDeviceAndReturnPath().c_str(), &::CreateProcessW))
		{
			throw std::runtime_error(std::format("CreateProcessW Failed, error code: {}", GetLastError()));
		}
		try
		{
			const std::wstring rootPath = app().envDataRoot();
			const std::uint32_t rootPathCount = static_cast<std::uint32_t>(rootPath.length());
			const std::uint32_t rootPathSize = rootPathCount * sizeof(wchar_t);
			const std::uint32_t paramsSize = sizeof(DetourInjectParams) + rootPathSize;
			std::vector<std::byte> buffer(paramsSize);
			DetourInjectParams* injectParams = reinterpret_cast<DetourInjectParams*>(buffer.data());
			injectParams->version = biz::get_core_data().version;
			injectParams->envFlag = env->getFlag();
			injectParams->envIndex = env->getIndex();
			injectParams->rootPathCount = rootPathCount;
			memcpy(injectParams->rootPath, rootPath.data(), rootPathSize);
			if (!DetourCopyPayloadToProcess(procInfo.hProcess, DETOUR_INJECT_PARAMS_GUID, injectParams, paramsSize))
			{
				throw std::runtime_error(std::format("copy payload failed, error code: {}", GetLastError()));
			}
		}
		catch (...)
		{
			TerminateProcess(procInfo.hProcess, 0);
			CloseHandle(procInfo.hThread);
			CloseHandle(procInfo.hProcess);
			throw;
		}
		return procInfo;
	}
}

namespace biz
{
	void Launcher::run(const std::shared_ptr<Env>& env, std::wstring_view exePath, std::wstring_view params /*= L""*/)
	{
		// UI 入口：启动成功后附带轮询兜底（慢电脑/注入失败时提示并可重试）
		m_asyncScope.spawn(launchWithPoll(env, std::wstring{exePath}, std::wstring{params}));
	}

	void Launcher::runInNewEnv(std::wstring_view exePath, std::wstring_view params /*= L""*/)
	{
		m_asyncScope.spawn(launchWithPoll(std::shared_ptr<Env>{}, std::wstring{exePath}, std::wstring{params}));
	}

	coro::LazyTask<void> Launcher::coRun(std::shared_ptr<Env> env, std::wstring_view exePath, std::wstring_view params)
	{
		coro::SharedTask<void> sharedTask = coro::start_and_shared(launchInternal(env, std::wstring{exePath}, std::wstring{params}));
		m_asyncScope.spawn(sharedTask);
		co_await sharedTask;
		co_return;
	}

	coro::LazyTask<void> Launcher::launch(const std::shared_ptr<Env>& env, std::wstring_view exePath, std::wstring_view params) const
	{
		try
		{
			co_await launchInternal(env, std::wstring{exePath}, std::wstring{params});
		}
		catch (const std::exception& e)
		{
			show_utf8_error_message(std::format("启动进程失败：{}", e.what()));
		}
		catch (...)
		{
			show_error_message(L"启动进程失败：发生未知错误");
		}
		co_return;
	}

	coro::LazyTask<void> Launcher::launchInternal(std::shared_ptr<Env> env, std::wstring exePath, std::wstring params) const
	{
		co_await sched::transfer_to(m_execCtx);

		// ===== 授权检查：到期后禁止启动环境 / 新建环境 =====
		if (!biz::license::canLaunch())
		{
			show_error_message(L"授权已到期，无法启动环境。请联系作者续期激活码。");
			co_return;
		}

		if (!env)
		{
			env = env_mgr().createEnv();
			// 新建环境
			env_logger().append(env->getIndex(), EnvLogType::Info, EnvLogStatus::Info,
			                    L"新建环境", std::format(L"环境{} 已创建", env->getIndex()));
		}
		// 记住该程序使用的环境，下次启动时优先复用（登录态/数据随环境持久化）
		env_mgr().setLastEnvForProc(exePath, env);
		// 首次启动的应用路径绑定为该环境的“启动”按钮入口（后续启动同一环境时固定使用它）
		if (env->getAppPath().empty())
		{
			env_mgr().setEnvAppPath(env, exePath);
		}
		env_logger().append(env->getIndex(), EnvLogType::Process, EnvLogStatus::Info,
		                    L"启动进程", exePath);
		try
		{
			// 启动前确保该环境注册表 hive 已加载（DLL 注入后优先使用 HKU\eBox_Env_<idx> 的虚拟注册表）。
			// 若加载失败，DLL 侧会自动回退为按进程加载，进程仍能正常启动，这里只记录不阻塞。
			if (!env->loadRegistryHive())
			{
				env_logger().append(env->getIndex(), EnvLogType::Info, EnvLogStatus::Info,
				                    L"注册表", L"hive 加载失败，已回退为进程级虚拟注册表（数据不持久化）");
			}
			const PROCESS_INFORMATION procInfo = create_and_inject(env.get(), exePath, params);
			ResumeThread(procInfo.hThread);
			CloseHandle(procInfo.hThread);
			CloseHandle(procInfo.hProcess);
			env_logger().append(env->getIndex(), EnvLogType::Process, EnvLogStatus::Success,
			                    L"进程已启动", exePath);
		}
		catch (const std::exception& e)
		{
			std::wstring errDetail;
			if (e.what())
			{
				const std::string errStr{e.what()};
				errDetail.assign(errStr.begin(), errStr.end());
			}
			env_logger().append(env->getIndex(), EnvLogType::Error, EnvLogStatus::Fail,
			                    L"启动进程失败", errDetail);
			throw;
		}
		co_return;
	}

	coro::LazyTask<void> Launcher::launchWithPoll(std::shared_ptr<Env> env, std::wstring exePath, std::wstring params)
	{
		// 明确先切到独立工作线程（m_execCtx），确保本协程第一行起就绝不在 UI 线程执行——
		// 即便 future 变更了 AsyncScope::spawn 的起始执行线程，也不会在 UI 线程运行任何
		// 可能阻塞的启动逻辑（如注册表 hive 加载、进程注入）。
		co_await sched::transfer_to(m_execCtx);
		co_await launch(env, exePath, params);
		// 只对轮询部分启用取消：应用退出（Launcher 析构）时立即终止轮询，
		// 避免 join() 阻塞最多 60 秒、或超时弹窗挡住退出流程。
		co_await coro::co_with_cancellation(pollTargetProcess(std::move(env), std::move(exePath), std::move(params)), m_stopSource.get_token());
		co_return;
	}

	coro::LazyTask<void> Launcher::pollTargetProcess(std::shared_ptr<Env> env, std::wstring exePath, std::wstring params)
	{
		std::stop_token token = co_await coro::get_current_cancellation_token();
		namespace fs = std::filesystem;
		const std::wstring exeName = fs::path{exePath}.filename().native();
		if (exeName.empty())
		{
			co_return;
		}
		// 最多等待 60 秒，每秒检查一次目标 exe 是否已出现在系统中。
		// 只要进程出现过即视为启动成功（哪怕随后退出），不再打扰用户；
		// 慢电脑/老系统上应用进程创建本身较慢，也可能注入失败导致进程从未出现。
		bool bSeen = false;
		for (int i = 0; i < 60; ++i)
		{
			if (token.stop_requested())
			{
				// 应用正在退出，不再轮询
				co_return;
			}
			if (isProcessRunning(exeName))
			{
				bSeen = true;
				break;
			}
			co_await sched::transfer_after(std::chrono::seconds{1}, m_execCtx);
		}
		if (bSeen || token.stop_requested())
		{
			co_return;
		}
		// 超时未出现：切到 UI 线程弹窗，询问是否重试
		co_await sched::transfer_to(app().get_scheduler());
		if (token.stop_requested())
		{
			// 退出过程中超时，不弹窗打扰
			co_return;
		}
		const int ret = MessageBoxW(nullptr,
		                            std::format(L"「{}」未能正常启动。\n电脑配置较低或系统较旧时首次启动可能较慢，\n是否再试一次？", exePath).c_str(),
		                            MainApp::appName.data(), MB_YESNO | MB_ICONQUESTION | MB_TASKMODAL);
		if (ret == IDYES)
		{
			co_await launch(env, exePath, params);
		}
		co_return;
	}

	bool Launcher::isProcessRunning(std::wstring_view exeName)
	{
		const HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnapshot == INVALID_HANDLE_VALUE)
		{
			return false;
		}
		bool bFound = false;
		PROCESSENTRY32W entry{sizeof(entry)};
		if (Process32FirstW(hSnapshot, &entry))
		{
			do
			{
				if (_wcsicmp(entry.szExeFile, std::wstring{exeName}.c_str()) == 0)
				{
					bFound = true;
					break;
				}
			} while (Process32NextW(hSnapshot, &entry));
		}
		CloseHandle(hSnapshot);
		return bFound;
	}
}
