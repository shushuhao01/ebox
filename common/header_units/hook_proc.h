// ReSharper disable CppUnusedIncludeDirective
#pragma once
#ifndef _HOOK_PROC_H_
#define _HOOK_PROC_H_

// 多环境资源治理：把"企业微信 WXWork"的 CEF(Chromium) 辅助进程降为低于正常优先级。
//
// 背景: 20 个环境同时运行企业微信时, 每环境 10~20 个 Chromium 子进程
// (renderer/gpu/network/utility 等, 命令行带 --type=) 是 CPU 大户。
// 系统 CPU 饱和时它们互相抢占, 前台交互(输入法/打字/点按钮)被饿死
// → 表现为卡顿、"应用程序没有响应"。
// 本模块在子进程创建 hook 里把 WXWork 的 CEF 辅助进程设为 BELOW_NORMAL,
// Windows 调度器在 CPU 繁忙时优先保障正常优先级的前台交互进程,
// 空闲时 BELOW_NORMAL 依然全速运行(毫秒级差别, 不影响使用)。
// 纯调度层改动, 不改变任何功能行为。
//
// 兼容性: 仅使用 Win7+ 可用 API(CreateFile 无关, SetPriorityClass / 字符串处理)。

#ifndef NOMINMAX
#define NOMINMAX  // 防 windows.h 的 min/max 宏经 header unit 泄漏，破坏 std::min/std::max
#endif
#include <windows.h>
#include <string>
#include <string.h>

namespace hook_proc
{
	// 窄字符串 → 宽字符串（用于 CreateProcessA 系 hook 的入参转换）
	inline std::wstring a2w(const char* s)
	{
		if (!s)
		{
			return {};
		}
		const int len = ::MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
		if (len <= 0)
		{
			return {};
		}
		std::wstring ws(static_cast<std::size_t>(len), L'\0');
		::MultiByteToWideChar(CP_ACP, 0, s, -1, ws.data(), len);
		if (!ws.empty() && ws.back() == L'\0')
		{
			ws.pop_back();
		}
		return ws;
	}

	// 从命令行解析第一个 token（exe 路径，可能带引号），写入 outBuf；返回 outBuf
	inline const wchar_t* first_cmd_token(const wchar_t* cmdLine, wchar_t* outBuf, std::size_t outSize)
	{
		if (!cmdLine || outSize == 0)
		{
			return nullptr;
		}
		const wchar_t* p = cmdLine;
		while (*p == L' ' || *p == L'\t')
		{
			++p;
		}
		std::size_t i = 0;
		if (*p == L'"')
		{
			++p;
			while (*p && *p != L'"' && i + 1 < outSize)
			{
				outBuf[i++] = *p++;
			}
		}
		else
		{
			while (*p && *p != L' ' && *p != L'\t' && i + 1 < outSize)
			{
				outBuf[i++] = *p++;
			}
		}
		outBuf[i] = L'\0';
		return outBuf;
	}

	// 判断是否应把该子进程降为低于正常优先级：
	// 进程名必须是 WXWork.exe（企业微信主程序与其全部 CEF 子进程同名），
	// 且命令行含 "--type="（Chromium 多进程模型的辅助进程标记）。
	// 主进程（无 --type=）保持正常优先级，保证前台交互流畅。
	inline bool should_degrade(const wchar_t* appName, const wchar_t* cmdLine)
	{
		if (!cmdLine)
		{
			return false;
		}
		wchar_t exeBuf[MAX_PATH]{};
		const wchar_t* exe = nullptr;
		if (appName && *appName)
		{
			exe = appName;
		}
		else
		{
			exe = first_cmd_token(cmdLine, exeBuf, MAX_PATH);
		}
		if (!exe || !*exe)
		{
			return false;
		}
		const wchar_t* slash = ::wcsrchr(exe, L'\\');
		const wchar_t* base = slash ? slash + 1 : exe;
		if (_wcsicmp(base, L"WXWork.exe") != 0)
		{
			return false;
		}
		return ::wcsstr(cmdLine, L"--type=") != nullptr;
	}

	// 宽字符版本：进程创建 hook（CreateProcessW 系）在 ResumeThread 前调用
	inline void apply_child_priority(HANDLE hProcess, const wchar_t* appName, const wchar_t* cmdLine)
	{
		if (hProcess && hProcess != INVALID_HANDLE_VALUE && should_degrade(appName, cmdLine))
		{
			::SetPriorityClass(hProcess, BELOW_NORMAL_PRIORITY_CLASS);
		}
	}

	// 窄字符版本（重载）：CreateProcessA 系 hook 使用（内部转宽后复用同一判定）
	inline void apply_child_priority(HANDLE hProcess, const char* appName, const char* cmdLine)
	{
		if (!hProcess || hProcess == INVALID_HANDLE_VALUE)
		{
			return;
		}
		const std::wstring appW = a2w(appName);
		const std::wstring cmdW = a2w(cmdLine);
		apply_child_priority(hProcess, appW.empty() ? nullptr : appW.c_str(), cmdW.empty() ? nullptr : cmdW.c_str());
	}
}
#endif // _HOOK_PROC_H_
