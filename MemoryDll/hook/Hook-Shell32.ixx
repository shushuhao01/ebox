export module Hook:Shell32;

import "sys_defs.h";
import "hook_proc.h";
import std;
import :Core;
import GlobalData;
import RpcClient;

namespace hook
{
	namespace
	{
		// ShellExecute 系入参改写：可能是单个路径（文件/文件夹），也可能是整条命令行
		// （explorer.exe /select,"D:\..."）。
		// 原因：explorer.exe 不在被注入的重定向进程范围内，交给它的必须是环境内真实路径，
		// 否则右键"打开文件位置"会弹"位置不可用"。
		std::optional<std::wstring> redirect_shell_text(std::wstring_view text)
		{
			if (text.empty())
			{
				return std::nullopt;
			}
			if (std::optional<std::wstring> direct = global::redirect_known_folder_path(text))
			{
				return direct;
			}
			return global::redirect_paths_in_command_line(text);
		}

		// 用重定向后的路径重建 pidl（失败返回 nullptr，调用方应回退到原 pidl）
		PIDLIST_ABSOLUTE make_redirected_pidl(std::wstring_view path)
		{
			PIDLIST_ABSOLUTE pidl = nullptr;
			if (FAILED(::SHParseDisplayName(std::wstring{path}.c_str(), nullptr, &pidl, 0, nullptr)))
			{
				return nullptr;
			}
			return pidl;
		}
	}

	template <auto Trampoline>
	HINSTANCE STDAPICALLTYPE ShellExecuteA(_In_opt_ HWND hwnd, _In_opt_ LPCSTR lpOperation, _In_ LPCSTR lpFile, _In_opt_ LPCSTR lpParameters, _In_opt_ LPCSTR lpDirectory, _In_ INT nShowCmd)
	{
		global::debug_trace(std::format(L"ShellExecuteA op={} file={} params={} dir={}",
		                                hook_proc::a2w(lpOperation ? lpOperation : ""),
		                                hook_proc::a2w(lpFile ? lpFile : ""),
		                                hook_proc::a2w(lpParameters ? lpParameters : ""),
		                                hook_proc::a2w(lpDirectory ? lpDirectory : "")));
		if (lpOperation && _stricmp(lpOperation, "runas") == 0 && !global::Data::get().isNonLimitedAdmin())
		{
			const std::filesystem::path path{lpFile ? lpFile : ""};
			rpc::default_call_ignore_error(&rpc::ClientDefault::requireElevation, GetCurrentProcessId(), global::Data::get().envFlag(), path.native());
			return reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED);
		}

		std::string fileA;
		std::string paramsA;
		std::string dirA;
		LPCSTR file = lpFile;
		LPCSTR params = lpParameters;
		LPCSTR dir = lpDirectory;
		if (lpFile && *lpFile)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(hook_proc::a2w(lpFile)))
			{
				fileA = hook_proc::w2a(redirected->c_str());
				file = fileA.c_str();
			}
		}
		if (lpParameters && *lpParameters)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(hook_proc::a2w(lpParameters)))
			{
				paramsA = hook_proc::w2a(redirected->c_str());
				params = paramsA.c_str();
			}
		}
		if (lpDirectory && *lpDirectory)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(hook_proc::a2w(lpDirectory)))
			{
				dirA = hook_proc::w2a(redirected->c_str());
				dir = dirA.c_str();
			}
		}
		return Trampoline(hwnd, lpOperation, file, params, dir, nShowCmd);
	}

	template <auto Trampoline>
	HINSTANCE STDAPICALLTYPE ShellExecuteW(_In_opt_ HWND hwnd, _In_opt_ LPCWSTR lpOperation, _In_ LPCWSTR lpFile, _In_opt_ LPCWSTR lpParameters, _In_opt_ LPCWSTR lpDirectory, _In_ INT nShowCmd)
	{
		global::debug_trace(std::format(L"ShellExecuteW op={} file={} params={} dir={}",
		                                lpOperation ? lpOperation : L"",
		                                lpFile ? lpFile : L"",
		                                lpParameters ? lpParameters : L"",
		                                lpDirectory ? lpDirectory : L""));
		if (lpOperation && _wcsicmp(lpOperation, L"runas") == 0 && !global::Data::get().isNonLimitedAdmin())
		{
			rpc::default_call_ignore_error(&rpc::ClientDefault::requireElevation, GetCurrentProcessId(), global::Data::get().envFlag(), lpFile ? lpFile : L"");
			return reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED);
		}

		std::wstring fileW;
		std::wstring paramsW;
		std::wstring dirW;
		LPCWSTR file = lpFile;
		LPCWSTR params = lpParameters;
		LPCWSTR dir = lpDirectory;
		if (lpFile && *lpFile)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(lpFile))
			{
				fileW = std::move(*redirected);
				file = fileW.c_str();
			}
		}
		if (lpParameters && *lpParameters)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(lpParameters))
			{
				paramsW = std::move(*redirected);
				params = paramsW.c_str();
			}
		}
		if (lpDirectory && *lpDirectory)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(lpDirectory))
			{
				dirW = std::move(*redirected);
				dir = dirW.c_str();
			}
		}
		return Trampoline(hwnd, lpOperation, file, params, dir, nShowCmd);
	}

	template <auto Trampoline>
	BOOL STDAPICALLTYPE ShellExecuteExA(_Inout_ SHELLEXECUTEINFOA* pExecInfo)
	{
		if (!pExecInfo)
		{
			return Trampoline(pExecInfo);
		}
		global::debug_trace(std::format(L"ShellExecuteExA op={} file={} params={} dir={} pidl={}",
		                                hook_proc::a2w(pExecInfo->lpVerb ? pExecInfo->lpVerb : ""),
		                                hook_proc::a2w(pExecInfo->lpFile ? pExecInfo->lpFile : ""),
		                                hook_proc::a2w(pExecInfo->lpParameters ? pExecInfo->lpParameters : ""),
		                                hook_proc::a2w(pExecInfo->lpDirectory ? pExecInfo->lpDirectory : ""),
		                                pExecInfo->lpIDList ? 1 : 0));

		if (pExecInfo->lpVerb && _stricmp(pExecInfo->lpVerb, "runas") == 0 && !global::Data::get().isNonLimitedAdmin())
		{
			pExecInfo->hInstApp = reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED);
			SetLastError(ERROR_CANCELLED);
			const std::filesystem::path path{pExecInfo->lpFile ? pExecInfo->lpFile : ""};
			rpc::default_call_ignore_error(&rpc::ClientDefault::requireElevation, GetCurrentProcessId(), global::Data::get().envFlag(), path.native());
			return FALSE;
		}

		// 副本改写路径，调用后回写结果字段（并还原入参指针）
		SHELLEXECUTEINFOA info = *pExecInfo;
		std::string fileA;
		std::string paramsA;
		std::string dirA;
		if (info.lpFile && *info.lpFile)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(hook_proc::a2w(info.lpFile)))
			{
				fileA = hook_proc::w2a(redirected->c_str());
				info.lpFile = fileA.c_str();
			}
		}
		if (info.lpParameters && *info.lpParameters)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(hook_proc::a2w(info.lpParameters)))
			{
				paramsA = hook_proc::w2a(redirected->c_str());
				info.lpParameters = paramsA.c_str();
			}
		}
		if (info.lpDirectory && *info.lpDirectory)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(hook_proc::a2w(info.lpDirectory)))
			{
				dirA = hook_proc::w2a(redirected->c_str());
				info.lpDirectory = dirA.c_str();
			}
		}

		const BOOL ok = Trampoline(&info);

		info.lpVerb = pExecInfo->lpVerb;
		info.lpFile = pExecInfo->lpFile;
		info.lpParameters = pExecInfo->lpParameters;
		info.lpDirectory = pExecInfo->lpDirectory;
		*pExecInfo = info;
		return ok;
	}

	template <auto Trampoline>
	BOOL STDAPICALLTYPE ShellExecuteExW(_Inout_ SHELLEXECUTEINFOW* pExecInfo)
	{
		if (!pExecInfo)
		{
			return Trampoline(pExecInfo);
		}
		global::debug_trace(std::format(L"ShellExecuteExW op={} file={} params={} dir={} pidl={}",
		                                pExecInfo->lpVerb ? pExecInfo->lpVerb : L"",
		                                pExecInfo->lpFile ? pExecInfo->lpFile : L"",
		                                pExecInfo->lpParameters ? pExecInfo->lpParameters : L"",
		                                pExecInfo->lpDirectory ? pExecInfo->lpDirectory : L"",
		                                pExecInfo->lpIDList ? 1 : 0));

		if (pExecInfo->lpVerb && _wcsicmp(pExecInfo->lpVerb, L"runas") == 0 && !global::Data::get().isNonLimitedAdmin())
		{
			pExecInfo->hInstApp = reinterpret_cast<HINSTANCE>(SE_ERR_ACCESSDENIED);
			SetLastError(ERROR_CANCELLED);
			rpc::default_call_ignore_error(&rpc::ClientDefault::requireElevation, GetCurrentProcessId(), global::Data::get().envFlag(), pExecInfo->lpFile ? pExecInfo->lpFile : L"");
			return FALSE;
		}

		// 副本改写路径，调用后回写结果字段（并还原入参指针）
		SHELLEXECUTEINFOW info = *pExecInfo;
		std::wstring fileW;
		std::wstring paramsW;
		std::wstring dirW;
		if (info.lpFile && *info.lpFile)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(info.lpFile))
			{
				fileW = std::move(*redirected);
				info.lpFile = fileW.c_str();
			}
		}
		if (info.lpParameters && *info.lpParameters)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(info.lpParameters))
			{
				paramsW = std::move(*redirected);
				info.lpParameters = paramsW.c_str();
			}
		}
		if (info.lpDirectory && *info.lpDirectory)
		{
			if (std::optional<std::wstring> redirected = redirect_shell_text(info.lpDirectory))
			{
				dirW = std::move(*redirected);
				info.lpDirectory = dirW.c_str();
			}
		}

		const BOOL ok = Trampoline(&info);

		info.lpVerb = pExecInfo->lpVerb;
		info.lpFile = pExecInfo->lpFile;
		info.lpParameters = pExecInfo->lpParameters;
		info.lpDirectory = pExecInfo->lpDirectory;
		*pExecInfo = info;
		return ok;
	}

	// 企业微信等 CEF/Chromium 程序"在文件夹中显示"实际调用的是这个 API
	// （Chromium platform_util_win.cc: SHOpenFolderAndSelectItems(ILCreateFromPath(...))），
	// 不经过 ShellExecute，需要单独重定向，否则同样弹"位置不可用"。
	template <auto Trampoline>
	HRESULT STDAPICALLTYPE SHOpenFolderAndSelectItems(PCIDLIST_ABSOLUTE pidlFolder, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD dwFlags)
	{
		if (!pidlFolder)
		{
			return Trampoline(pidlFolder, cidl, apidl, dwFlags);
		}
		std::wstring pathBuf(MAX_PATH * 2, L'\0');
		if (!::SHGetPathFromIDListW(pidlFolder, pathBuf.data()))
		{
			return Trampoline(pidlFolder, cidl, apidl, dwFlags); // 非文件系统项（虚拟项），不处理
		}
		pathBuf.resize(::wcslen(pathBuf.c_str()));
		global::debug_trace(std::format(L"SHOpenFolderAndSelectItems: {} cidl={} flags={}", pathBuf, cidl, dwFlags));
		const std::optional<std::wstring> redirected = redirect_shell_text(pathBuf);
		if (!redirected)
		{
			return Trampoline(pidlFolder, cidl, apidl, dwFlags);
		}
		PIDLIST_ABSOLUTE newPidl = make_redirected_pidl(*redirected);
		if (!newPidl)
		{
			return Trampoline(pidlFolder, cidl, apidl, dwFlags);
		}
		const HRESULT hr = Trampoline(newPidl, cidl, apidl, dwFlags);
		::CoTaskMemFree(newPidl);
		return hr;
	}

	void hook_shell32()
	{
		// runas 拦截逻辑只对未提升进程生效（见各包装函数内的 isNonLimitedAdmin 判断）；
		// 路径重定向在所有环境下都需要生效，因此 hook 始终安装。
		// 背景：通过 cmd 运行的程序不是通过 runas 谓词创建管理员进程的；
		// cmd 会先尝试正常创建进程，遇到 ERROR_ELEVATION_REQUIRED 才尝试提权，
		// 所以在 CreateProcessW 里抹去该错误即可。
		create_hook_by_func_ptr<&::ShellExecuteA>().setHookFromGetter([&](auto trampolineConst)
		{
			return HookInfo{&ShellExecuteA<trampolineConst.value>};
		});
		create_hook_by_func_ptr<&::ShellExecuteW>().setHookFromGetter([&](auto trampolineConst)
		{
			return HookInfo{&ShellExecuteW<trampolineConst.value>};
		});
		create_hook_by_func_ptr<&::ShellExecuteExA>().setHookFromGetter([&](auto trampolineConst)
		{
			return HookInfo{&ShellExecuteExA<trampolineConst.value>};
		});
		create_hook_by_func_ptr<&::ShellExecuteExW>().setHookFromGetter([&](auto trampolineConst)
		{
			return HookInfo{&ShellExecuteExW<trampolineConst.value>};
		});
		create_hook_by_func_ptr<&::SHOpenFolderAndSelectItems>().setHookFromGetter([&](auto trampolineConst)
		{
			return HookInfo{&SHOpenFolderAndSelectItems<trampolineConst.value>};
		});
	}
}
