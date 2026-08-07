module;
#define NOMINMAX
#include <shobjidl_core.h>
#include <shellapi.h>
#pragma comment(lib, "msimg32.lib")
module UI.Core;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif
import MainApp;
import biz.License;

namespace
{
	constexpr wchar_t INPUT_DLG_CLASS[] = L"2BoxInputDialog";
	constexpr int INPUT_EDIT_ID = 1001;
	constexpr int INPUT_OK_ID = IDOK;
	constexpr int INPUT_CANCEL_ID = IDCANCEL;

	struct InputDialogData
	{
		std::wstring initial;
		std::wstring result;
		HWND hEdit{nullptr};
		bool done{false};
		bool cancelled{true};
	};

	LRESULT CALLBACK input_dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		auto* data = reinterpret_cast<InputDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		switch (msg)
		{
		case WM_CREATE:
		{
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			data = static_cast<InputDialogData*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

			const HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", data->initial.c_str(),
			                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
			                                   16, 20, 344, 26, hwnd, reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INPUT_EDIT_ID)),
			                                   GetModuleHandleW(nullptr), nullptr);
			data->hEdit = hEdit;
			CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
			                192, 64, 78, 32, hwnd, reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INPUT_OK_ID)),
			                GetModuleHandleW(nullptr), nullptr);
			CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			                282, 64, 78, 32, hwnd, reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INPUT_CANCEL_ID)),
			                GetModuleHandleW(nullptr), nullptr);
			return 0;
		}
		case WM_COMMAND:
			if (LOWORD(wParam) == INPUT_OK_ID)
			{
				// 必须在销毁窗口前读取输入框文本，否则 hEdit 已失效
				wchar_t buf[512]{};
				GetWindowTextW(data->hEdit, buf, 512);
				data->result = buf;
				data->cancelled = false;
				DestroyWindow(hwnd);
				return 0;
			}
			if (LOWORD(wParam) == INPUT_CANCEL_ID)
			{
				data->cancelled = true;
				DestroyWindow(hwnd);
				return 0;
			}
			return 0;
		case WM_CLOSE:
			data->cancelled = true;
			DestroyWindow(hwnd);
			return 0;
		case WM_DESTROY:
			data->done = true;
			return 0;
		default:
			break;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}

namespace ui
{
	std::optional<std::wstring> input_text(const WindowBase* owner, std::wstring_view title, std::wstring_view initial)
	{
		static const bool classRegistered = []()
		{
			WNDCLASSEXW wc = {sizeof(wc)};
			wc.lpfnWndProc = input_dlg_proc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
			wc.lpszClassName = INPUT_DLG_CLASS;
			return RegisterClassExW(&wc) != 0;
		}();
		(void)classRegistered;

		InputDialogData data;
		data.initial = initial;

		HWND hOwner = owner ? owner->nativeHandle() : nullptr;
		int x = 0;
		int y = 0;
		if (hOwner && IsWindow(hOwner))
		{
			RECT rc{};
			GetWindowRect(hOwner, &rc);
			constexpr int width = 380;
			constexpr int height = 170;
			x = rc.left + (rc.right - rc.left - width) / 2;
			y = rc.top + (rc.bottom - rc.top - height) / 2;
		}
		const HWND hDlg = CreateWindowExW(0, INPUT_DLG_CLASS, title.data(),
		                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
		                                  x, y, 380, 170, hOwner, nullptr,
		                                  GetModuleHandleW(nullptr), &data);
		if (!hDlg)
		{
			return std::nullopt;
		}
		ShowWindow(hDlg, SW_SHOW);
		SetFocus(data.hEdit);
		SendMessageW(data.hEdit, EM_SETSEL, 0, static_cast<LPARAM>(-1));

		if (hOwner && IsWindow(hOwner))
		{
			EnableWindow(hOwner, FALSE);
		}

		MSG msg{};
		while (!data.done && GetMessageW(&msg, nullptr, 0, 0))
		{
			if (IsDialogMessageW(hDlg, &msg))
			{
				continue;
			}
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		if (hOwner && IsWindow(hOwner))
		{
			EnableWindow(hOwner, TRUE);
			SetForegroundWindow(hOwner);
		}

		if (data.cancelled)
		{
			return std::nullopt;
		}
		if (data.result.empty())
		{
			return std::nullopt;
		}
		return data.result;
	}

	std::optional<std::wstring> select_file(const WindowBase* owner)
	{
		UniqueComPtr<IFileOpenDialog> fileOpen;
		HResult hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&fileOpen));
		if (FAILED(hr))
		{
			MessageBoxW(owner->nativeHandle(),
			            std::format(L"创建文件选择对话框失败! CoCreateInstance fail, HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)).c_str(),
			            MainApp::appName.data(),
			            MB_OK | MB_ICONERROR | MB_TASKMODAL);
			return std::nullopt;
		}
		COMDLG_FILTERSPEC rgSpec[] =
		{
			{L"可执行文件和快捷方式", L"*.exe;*.lnk;*.url"},
			{L"可执行文件", L"*.exe"},
			{L"所有文件", L"*.*"}
		};
		fileOpen->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);
		fileOpen->SetFileTypeIndex(1);
		DWORD dwOptions = 0;
		fileOpen->GetOptions(&dwOptions);
		fileOpen->SetOptions(dwOptions | FOS_STRICTFILETYPES | FOS_FORCEFILESYSTEM);
		hr = fileOpen->Show(owner->nativeHandle());
		if (FAILED(hr))
		{
			if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
			{
				MessageBoxW(owner->nativeHandle(),
				            std::format(L"显示文件选择对话框失败! HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)).c_str(),
				            MainApp::appName.data(),
				            MB_OK | MB_ICONERROR | MB_TASKMODAL);
			}
			return std::nullopt;
		}
		UniqueComPtr<IShellItem> item;
		hr = fileOpen->GetResult(&item);
		if (FAILED(hr))
		{
			MessageBoxW(owner->nativeHandle(),
			            std::format(L"无法获取选择的文件! HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)).c_str(),
			            MainApp::appName.data(),
			            MB_OK | MB_ICONERROR | MB_TASKMODAL);
			return std::nullopt;
		}
		PWSTR pszFilePath;
		hr = item->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
		if (FAILED(hr))
		{
			MessageBoxW(owner->nativeHandle(),
			            std::format(L"无法获取选择的文件路径! HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)).c_str(),
			            MainApp::appName.data(),
			            MB_OK | MB_ICONERROR | MB_TASKMODAL);
			return std::nullopt;
		}
		std::wstring procFullPath{pszFilePath};
		CoTaskMemFree(pszFilePath);
		return procFullPath;
	}

	// ---- 清理环境数据对话框：仅缓存 / 仅聊天记录 / 都清理 ----
	namespace
	{
		constexpr wchar_t CLEAN_DLG_CLASS[] = L"2BoxCleanDialog";
		constexpr int CLEAN_RB_BASE_ID = 2001;   // 三个单选按钮：2001/2002/2003
		constexpr int CLEAN_OK_ID = 2004;
		constexpr int CLEAN_CANCEL_ID = 2005;

		// 对话框设计尺寸（客户区）
		constexpr int CLEAN_DLG_WIDTH = 480;
		constexpr int CLEAN_DLG_HEIGHT = 360;
		// 主题色（与 2Box 主色调一致的现代蓝）
		constexpr COLORREF CLEAN_ACCENT = RGB(0x00, 0x78, 0xd4);
		constexpr COLORREF CLEAN_ACCENT_LIGHT = RGB(0x4a, 0xa3, 0xe8);
		constexpr COLORREF CLEAN_BG = RGB(0xfa, 0xfb, 0xfd);
		constexpr COLORREF CLEAN_TEXT = RGB(0x24, 0x28, 0x2e);
		constexpr COLORREF CLEAN_TEXT_SUB = RGB(0x8a, 0x91, 0x9c);
		constexpr COLORREF CLEAN_CARD = RGB(0xff, 0xff, 0xff);
		constexpr COLORREF CLEAN_BORDER = RGB(0xe4, 0xe8, 0xef);
		constexpr COLORREF CLEAN_DANGER = RGB(0xe5, 0x39, 0x35);

		struct CleanDialogData
		{
			HWND hwnd{nullptr};
			HWND hRadio[3]{};
			HWND hDesc{nullptr};
			HWND hOk{nullptr};
			HWND hCancel{nullptr};
			HWND hTitle{nullptr};
			HFONT hFontTitle{nullptr};
			HFONT hFontBody{nullptr};
			HFONT hFontDesc{nullptr};
			// 描述区背景画刷：切换提示词时先清底再画新文字，实现互斥显示（否则旧文字残留重叠）
			HBRUSH hDescBg{nullptr};
			// 当前选中的选项下标（0=仅缓存 1=仅聊天记录 2=都清理）。
			// BS_OWNERDRAW 按钮的 BM_SETCHECK/BM_GETCHECK 不可靠，故用成员变量维护。
			int selectedRadio{0};
			std::wstring envName;
			bool done{false};
			bool cancelled{true};
			std::optional<ECleanOption> result{std::nullopt};

			// 各选项对应的清理内容提示
			static constexpr std::wstring_view descCache{
				L"清理该环境企业微信的 CEF 渲染缓存（qtCef / WXWorkCefCache /\n"
				L"GPU 着色器等），下次启动自动重建。\n\n"
				L"不影响：聊天记录、登录状态、企业配置、Default 会话数据。"
			};
			static constexpr std::wstring_view descChatData{
				L"清理该环境企业微信的聊天记录（Profiles / Data 等消息库、\n"
				L"搜索索引 Index 等）。\n\n"
				L"⚠ 聊天记录将永久删除且无法恢复，登录状态保留。"
			};
			static constexpr std::wstring_view descBoth{
				L"同时清理该环境的 CEF 缓存 与 聊天记录（包含搜索索引）。\n\n"
				L"⚠ 聊天记录将永久删除且无法恢复。"
			};

			std::wstring_view descFor(ECleanOption opt) const
			{
				switch (opt)
				{
				case ECleanOption::Cache:
					return descCache;
				case ECleanOption::ChatData:
					return descChatData;
				case ECleanOption::Both:
					return descBoth;
				}
				return {};
			}

			ECleanOption currentOption() const
			{
				return static_cast<ECleanOption>(selectedRadio);
			}
		};

		// 现代方形复选框单选（自绘）：选中项在方框内打勾并着色
		void draw_modern_radio(HDC hdc, const RECT& rc, bool selected, bool hover, bool danger, LPCWSTR text)
		{
			// 选项整行背景（选中时极浅主色，未选中白色）
			HBRUSH hBg = CreateSolidBrush(selected ? RGB(0xe3, 0xf2, 0xfd) : CLEAN_CARD);
			FillRect(hdc, &rc, hBg);
			DeleteObject(hBg);

			// 方框（16x16）左侧垂直居中
			const int boxSize = 16;
			const int boxX = rc.left + 6;
			const int boxY = (rc.top + rc.bottom - boxSize) / 2;

			// 未选中：细描边方框；选中：主色实底方框 + 白色对勾
			HPEN hPen = CreatePen(PS_SOLID, selected ? 2 : 1, selected ? CLEAN_ACCENT : (hover ? CLEAN_ACCENT_LIGHT : CLEAN_BORDER));
			HBRUSH hBoxBg = CreateSolidBrush(selected ? CLEAN_ACCENT : CLEAN_CARD);
			HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
			HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hBoxBg));
			RECT boxRc{boxX, boxY, boxX + boxSize, boxY + boxSize};
			RoundRect(hdc, boxRc.left, boxRc.top, boxRc.right, boxRc.bottom, 3, 3);
			SelectObject(hdc, hOldBr);
			DeleteObject(hBoxBg);
			SelectObject(hdc, hOldPen);
			DeleteObject(hPen);

			// 选中：画白色对勾（两段线段）
			if (selected)
			{
				HPEN hCheckPen = CreatePen(PS_SOLID, 2, RGB(0xff, 0xff, 0xff));
				HPEN hOldCheckPen = static_cast<HPEN>(SelectObject(hdc, hCheckPen));
				const float cx0 = boxX + 3.5f, cy0 = boxY + 8.5f;
				const float cx1 = boxX + 6.5f, cy1 = boxY + 11.5f;
				const float cx2 = boxX + 12.5f, cy2 = boxY + 5.0f;
				MoveToEx(hdc, static_cast<int>(cx0), static_cast<int>(cy0), nullptr);
				LineTo(hdc, static_cast<int>(cx1), static_cast<int>(cy1));
				LineTo(hdc, static_cast<int>(cx2), static_cast<int>(cy2));
				SelectObject(hdc, hOldCheckPen);
				DeleteObject(hCheckPen);
			}

			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, danger ? CLEAN_DANGER : CLEAN_TEXT);
			RECT textRc = rc;
			textRc.left = boxX + boxSize + 8;
			DrawTextW(hdc, text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}

		void clean_dlg_update_desc(CleanDialogData& data)
		{
			if (!data.hDesc)
			{
				return;
			}
			const std::wstring_view desc = data.descFor(data.currentOption());
			SetWindowTextW(data.hDesc, desc.data());
			// 强制重绘：配合实心背景画刷先清底再画新文字，保证只显示当前选项的提示词
			InvalidateRect(data.hDesc, nullptr, TRUE);
		}

		LRESULT CALLBACK clean_dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* data = reinterpret_cast<CleanDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			switch (msg)
			{
			case WM_CREATE:
			{
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
				data = static_cast<CleanDialogData*>(cs->lpCreateParams);
				data->hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

				const HMODULE hInst = GetModuleHandleW(nullptr);
				// 字体：标题 16px Semibold，选项 13px，描述 11px（小字紧凑显示全部内容）
				{
					const HDC hdc = GetDC(hwnd);
					const int titleSize = -MulDiv(16, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					const int bodySize = -MulDiv(13, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					const int descSize = -MulDiv(11, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					ReleaseDC(hwnd, hdc);
					data->hFontTitle = CreateFontW(titleSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
					                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
					data->hFontBody = CreateFontW(bodySize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
					                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
					data->hFontDesc = CreateFontW(descSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
				}
				// 描述区背景画刷（与对话框底色一致，重绘时清底防止旧文字残留）
				data->hDescBg = CreateSolidBrush(CLEAN_BG);

				// 标题文字（顶部大标题）
				data->hTitle = CreateWindowExW(0, L"STATIC", L"清理环境数据",
				                               WS_CHILD | WS_VISIBLE | SS_LEFT,
				                               24, 18, CLEAN_DLG_WIDTH - 48, 26, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(1000)),
				                               hInst, nullptr);
				SendMessageW(data->hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontTitle), TRUE);

				// 三个自绘单选按钮（卡片式）
				constexpr std::wstring_view labels[3] = {L"仅清理缓存", L"仅清理聊天记录", L"都清理"};
				for (int i = 0; i < 3; ++i)
				{
					data->hRadio[i] = CreateWindowExW(0, L"BUTTON", labels[i].data(),
					                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | (i == 0 ? WS_GROUP : 0),
					                                  24, 60 + i * 36, CLEAN_DLG_WIDTH - 48, 30, hwnd,
					                                  reinterpret_cast<HMENU>(static_cast<std::intptr_t>(CLEAN_RB_BASE_ID + i)),
					                                  hInst, nullptr);
					SendMessageW(data->hRadio[i], WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontBody), TRUE);
				}

				// 描述区（无边框，11px 小字，SS_LEFT 自动换行完整显示）
				data->hDesc = CreateWindowExW(0, L"STATIC", L"",
				                              WS_CHILD | WS_VISIBLE | SS_LEFT,
				                              24, 172, CLEAN_DLG_WIDTH - 48, 116, hwnd,
				                              reinterpret_cast<HMENU>(static_cast<std::intptr_t>(1007)),
				                              hInst, nullptr);
				SendMessageW(data->hDesc, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontDesc), TRUE);

				// 按钮：确认（主色实底）/ 取消（描边）
				data->hOk = CreateWindowExW(0, L"BUTTON", L"确认清理",
				                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
				                            CLEAN_DLG_WIDTH - 190, CLEAN_DLG_HEIGHT - 52, 88, 34, hwnd,
				                            reinterpret_cast<HMENU>(static_cast<std::intptr_t>(CLEAN_OK_ID)),
				                            hInst, nullptr);
				SendMessageW(data->hOk, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontBody), TRUE);
				data->hCancel = CreateWindowExW(0, L"BUTTON", L"取消",
				                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                                CLEAN_DLG_WIDTH - 96, CLEAN_DLG_HEIGHT - 52, 72, 34, hwnd,
				                                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(CLEAN_CANCEL_ID)),
				                                hInst, nullptr);
				SendMessageW(data->hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontBody), TRUE);

				clean_dlg_update_desc(*data);
				return 0;
			}
			case WM_ERASEBKGND:
				return 1; // 防止闪烁，背景在 WM_PAINT 统一绘制
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				HDC hdc = BeginPaint(hwnd, &ps);
				// 整体背景（描述区不再绘制卡片方框，直接以浅色背景+小字显示提示词）
				HBRUSH hBg = CreateSolidBrush(CLEAN_BG);
				FillRect(hdc, &ps.rcPaint, hBg);
				DeleteObject(hBg);
				EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_CTLCOLORSTATIC:
			{
				// 描述区必须返回实心背景画刷：切换选项时先填背景擦除旧文字，再画新文字，
				// 否则旧提示词残留导致多个提示词堆叠重叠。
				HDC hdc = reinterpret_cast<HDC>(wParam);
				const HWND hWnd = reinterpret_cast<HWND>(lParam);
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, (hWnd == data->hTitle) ? CLEAN_TEXT : CLEAN_TEXT_SUB);
				if (hWnd == data->hDesc && data->hDescBg)
				{
					return reinterpret_cast<LRESULT>(data->hDescBg);
				}
				return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
			}
			case WM_CTLCOLORBTN:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetBkMode(hdc, TRANSPARENT);
				return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
			}
			case WM_DRAWITEM:
			{
				const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
				const int id = dis->CtlID;
				if (id >= CLEAN_RB_BASE_ID && id < CLEAN_RB_BASE_ID + 3)
				{
					// 选中态以成员变量 selectedRadio 为准（BS_OWNERDRAW 下 BM_GETCHECK 不可靠）
					const bool selected = (id - CLEAN_RB_BASE_ID) == data->selectedRadio;
					const bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
					const bool danger = (id == CLEAN_RB_BASE_ID + 1 || id == CLEAN_RB_BASE_ID + 2);
					RECT rc = dis->rcItem;
					// 绘制前清底
					HBRUSH hBg = CreateSolidBrush(CLEAN_BG);
					FillRect(dis->hDC, &rc, hBg);
					DeleteObject(hBg);
					// 文本从控件标题取
					wchar_t buf[64]{};
					GetWindowTextW(dis->hwndItem, buf, 64);
					draw_modern_radio(dis->hDC, rc, selected, hover, danger, buf);
					return TRUE;
				}
				if (id == CLEAN_OK_ID || id == CLEAN_CANCEL_ID)
				{
					const bool isOk = (id == CLEAN_OK_ID);
					const bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
					RECT rc = dis->rcItem;
					HBRUSH hBg = CreateSolidBrush(CLEAN_BG);
					FillRect(dis->hDC, &rc, hBg);
					DeleteObject(hBg);
					// 圆角按钮
					HBRUSH hBtn = CreateSolidBrush(isOk ? (hover ? CLEAN_ACCENT_LIGHT : CLEAN_ACCENT) : (hover ? RGB(0xf0,0xf2,0xf5) : CLEAN_CARD));
					HPEN hPen = CreatePen(PS_SOLID, 1, isOk ? CLEAN_ACCENT : CLEAN_BORDER);
					HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(dis->hDC, hBtn));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(dis->hDC, hPen));
					RoundRect(dis->hDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
					SelectObject(dis->hDC, hOldBr);
					DeleteObject(hBtn);
					SelectObject(dis->hDC, hOldPen);
					DeleteObject(hPen);
					SetBkMode(dis->hDC, TRANSPARENT);
					SetTextColor(dis->hDC, isOk ? RGB(0xff, 0xff, 0xff) : CLEAN_TEXT);
					wchar_t buf[32]{};
					GetWindowTextW(dis->hwndItem, buf, 32);
					DrawTextW(dis->hDC, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
					return TRUE;
				}
				return FALSE;
			}
			case WM_COMMAND:
			{
				const int id = LOWORD(wParam);
				if (id >= CLEAN_RB_BASE_ID && id < CLEAN_RB_BASE_ID + 3)
				{
					// 记录选中下标并刷新三个按钮（BS_OWNERDRAW 需手动管理勾选态）
					data->selectedRadio = id - CLEAN_RB_BASE_ID;
					for (int i = 0; i < 3; ++i)
					{
						if (data->hRadio[i])
						{
							InvalidateRect(data->hRadio[i], nullptr, TRUE);
						}
					}
					clean_dlg_update_desc(*data);
					return 0;
				}
				if (id == CLEAN_OK_ID)
				{
					data->result = data->currentOption();
					data->cancelled = false;
					DestroyWindow(hwnd);
					return 0;
				}
				if (id == CLEAN_CANCEL_ID)
				{
					data->cancelled = true;
					DestroyWindow(hwnd);
					return 0;
				}
				return 0;
			}
			case WM_CLOSE:
				data->cancelled = true;
				DestroyWindow(hwnd);
				return 0;
			case WM_DESTROY:
				if (data->hFontTitle)
				{
					DeleteObject(data->hFontTitle);
				}
				if (data->hFontBody)
				{
					DeleteObject(data->hFontBody);
				}
				if (data->hFontDesc)
				{
					DeleteObject(data->hFontDesc);
				}
				if (data->hDescBg)
				{
					DeleteObject(data->hDescBg);
				}
				data->done = true;
				return 0;
			default:
				break;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}

	std::optional<ECleanOption> confirm_clean_dialog(const WindowBase* owner, std::wstring_view envName)
	{
		static const bool classRegistered = []()
		{
			WNDCLASSEXW wc = {sizeof(wc)};
			wc.lpfnWndProc = clean_dlg_proc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
			wc.lpszClassName = CLEAN_DLG_CLASS;
			return RegisterClassExW(&wc) != 0;
		}();
		(void)classRegistered;

		CleanDialogData data;
		data.envName = envName;

		// CreateWindowExW 的 nWidth/nHeight 是整个窗口（含标题栏/边框）尺寸，
		// 客户区才是控件坐标的参照。为保证客户区达到设计高度（按钮不被裁剪），
		// 创建时按标题栏+边框高度补偿。
		const int titleBarHeight = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CXFIXEDFRAME) * 2;
		const int dlgWndWidth = CLEAN_DLG_WIDTH + GetSystemMetrics(SM_CXFIXEDFRAME) * 2;
		const int dlgWndHeight = CLEAN_DLG_HEIGHT + titleBarHeight;

		HWND hOwner = owner ? owner->nativeHandle() : nullptr;
		int x = 0;
		int y = 0;
		if (hOwner && IsWindow(hOwner))
		{
			RECT rc{};
			GetWindowRect(hOwner, &rc);
			x = rc.left + (rc.right - rc.left - dlgWndWidth) / 2;
			y = rc.top + (rc.bottom - rc.top - dlgWndHeight) / 2;
		}
		const HWND hDlg = CreateWindowExW(0, CLEAN_DLG_CLASS, std::format(L"清理环境数据 - {}", envName).c_str(),
		                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
		                                  x, y, dlgWndWidth, dlgWndHeight, hOwner, nullptr,
		                                  GetModuleHandleW(nullptr), &data);
		if (!hDlg)
		{
			return std::nullopt;
		}
		ShowWindow(hDlg, SW_SHOW);
		SetFocus(data.hRadio[0]);

		if (hOwner && IsWindow(hOwner))
		{
			EnableWindow(hOwner, FALSE);
		}

		MSG msg{};
		while (!data.done && GetMessageW(&msg, nullptr, 0, 0))
		{
			if (IsDialogMessageW(hDlg, &msg))
			{
				continue;
			}
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		if (hOwner && IsWindow(hOwner))
		{
			EnableWindow(hOwner, TRUE);
			SetForegroundWindow(hOwner);
		}

		return data.result;
	}

	// ==================== 授权相关弹窗（激活 / 授权信息）====================
	// 通用的美化绘制工具：垂直渐变背景、圆角矩形、圆角按钮
	namespace
	{
		void fill_v_gradient(HDC hdc, const RECT& rc, COLORREF top, COLORREF bottom)
		{
			TRIVERTEX v[2]{};
			v[0].x = rc.left;
			v[0].y = rc.top;
			v[0].Red = static_cast<COLOR16>(GetRValue(top) << 8);
			v[0].Green = static_cast<COLOR16>(GetGValue(top) << 8);
			v[0].Blue = static_cast<COLOR16>(GetBValue(top) << 8);
			v[0].Alpha = 0;
			v[1].x = rc.right;
			v[1].y = rc.bottom;
			v[1].Red = static_cast<COLOR16>(GetRValue(bottom) << 8);
			v[1].Green = static_cast<COLOR16>(GetGValue(bottom) << 8);
			v[1].Blue = static_cast<COLOR16>(GetBValue(bottom) << 8);
			v[1].Alpha = 0;
			GRADIENT_RECT g{0, 1};
			GradientFill(hdc, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
		}

		void draw_round_rect(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF border, int borderWidth)
		{
			HBRUSH hFill = CreateSolidBrush(fill);
			HPEN hPen = CreatePen(PS_SOLID, borderWidth, border);
			HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hFill));
			HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
			RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius * 2, radius * 2);
			SelectObject(hdc, hOldBr);
			DeleteObject(hFill);
			SelectObject(hdc, hOldPen);
			DeleteObject(hPen);
		}

		// 现代化圆角按钮：主按钮为蓝色实底，次要按钮为白底描边，danger 为红字红描边，success 为绿色实底
		void draw_modern_dlg_button(HDC hdc, const RECT& rc, bool primary, bool hover, bool pressed, LPCWSTR text, bool danger = false, bool success = false)
		{
			COLORREF fill;
			COLORREF border;
			if (danger)
			{
				fill = pressed ? RGB(0xfb, 0xe3, 0xe3) : (hover ? RGB(0xfd, 0xef, 0xef) : RGB(0xff, 0xff, 0xff));
				border = pressed ? CLEAN_DANGER : (hover ? RGB(0xee, 0x6c, 0x6c) : RGB(0xe5, 0x39, 0x35));
			}
			else if (success)
			{
				fill = pressed ? RGB(0x12, 0x8a, 0x3e) : (hover ? RGB(0x1a, 0xb8, 0x52) : RGB(0x16, 0xa3, 0x4a));
				border = fill;
			}
			else if (primary)
			{
				fill = pressed ? RGB(0x00, 0x62, 0xb0) : (hover ? RGB(0x1a, 0x86, 0xe0) : RGB(0x00, 0x78, 0xd4));
				border = fill;
			}
			else
			{
				fill = pressed ? RGB(0xe4, 0xe9, 0xf0) : (hover ? RGB(0xf0, 0xf4, 0xfa) : RGB(0xff, 0xff, 0xff));
				border = hover ? RGB(0x9c, 0xb8, 0xd8) : RGB(0xe4, 0xe8, 0xef);
			}
			draw_round_rect(hdc, rc, 8, fill, border, 1);
			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, danger ? CLEAN_DANGER : ((primary || success) ? RGB(0xff, 0xff, 0xff) : RGB(0x33, 0x3a, 0x45)));
			DrawTextW(hdc, text, -1, const_cast<RECT*>(&rc), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}
	}

	// ---- 激活码输入对话框（现代化渐变风格）----
	namespace
	{
		constexpr wchar_t ACTIVATE_DLG_CLASS[] = L"2BoxActivateDialog";
		constexpr int ACT_EDIT_ID = 3001;
		constexpr int ACT_OK_ID = 3002;
		constexpr int ACT_CANCEL_ID = 3003;
		constexpr int ACT_BUY_ID = 3007;
		constexpr int ACT_DLG_WIDTH = 480;
		constexpr int ACT_DLG_HEIGHT = 300;

		struct ActivateDialogData
		{
			HWND hwnd{nullptr};
			HWND hEdit{nullptr};
			HWND hError{nullptr};
			HWND hOk{nullptr};
			HWND hCancel{nullptr};
			HWND hBuy{nullptr};
			HWND hTitle{nullptr};
			HWND hSub{nullptr};
			HFONT hFontTitle{nullptr};
			HFONT hFontBody{nullptr};
			HFONT hFontSmall{nullptr};
			HFONT hFontHint{nullptr};
			bool errorMode{false};      // false=灰色提示词 true=红色错误提示（互斥显示）
			bool done{false};
			bool activated{false};
		};

		// 默认提示词（灰字，贴近输入框下方，自动换行）
		constexpr std::wstring_view ACT_HINT_TEXT{
			L"激活码由作者签发 · 绑定码首次激活自动绑定本机 · 到期后可在界面内续期"
		};

		void activate_dlg_show_error(ActivateDialogData& data, std::wstring_view msg)
		{
			if (!data.hError)
			{
				return;
			}
			data.errorMode = true;
			SetWindowTextW(data.hError, msg.data());
			InvalidateRect(data.hError, nullptr, TRUE);
		}

		void activate_dlg_show_hint(ActivateDialogData& data)
		{
			if (!data.hError)
			{
				return;
			}
			data.errorMode = false;
			SetWindowTextW(data.hError, ACT_HINT_TEXT.data());
			InvalidateRect(data.hError, nullptr, TRUE);
		}

		LRESULT CALLBACK activate_dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* data = reinterpret_cast<ActivateDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			switch (msg)
			{
			case WM_CREATE:
			{
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
				data = static_cast<ActivateDialogData*>(cs->lpCreateParams);
				data->hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

				const HMODULE hInst = GetModuleHandleW(nullptr);
				{
					const HDC hdc = GetDC(hwnd);
					const int titleSize = -MulDiv(19, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					const int bodySize = -MulDiv(14, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					const int smallSize = -MulDiv(12, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					const int hintSize = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					ReleaseDC(hwnd, hdc);
					data->hFontTitle = CreateFontW(titleSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
					                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
					data->hFontBody = CreateFontW(bodySize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
					                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
					data->hFontSmall = CreateFontW(smallSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
					                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
					data->hFontHint = CreateFontW(hintSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
					                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
				}

				data->hTitle = CreateWindowExW(0, L"STATIC", L"软件激活",
				                               WS_CHILD | WS_VISIBLE | SS_LEFT,
				                               58, 22, ACT_DLG_WIDTH - 90, 30, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(3000)),
				                               hInst, nullptr);
				SendMessageW(data->hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontTitle), TRUE);

				data->hSub = CreateWindowExW(0, L"STATIC", L"请输入激活码激活本软件",
				                             WS_CHILD | WS_VISIBLE | SS_LEFT,
				                             58, 56, ACT_DLG_WIDTH - 90, 20, hwnd,
				                             reinterpret_cast<HMENU>(static_cast<std::intptr_t>(3004)),
				                             hInst, nullptr);
				SendMessageW(data->hSub, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontSmall), TRUE);

				// 输入框：无自带边框（圆角面板由 WM_PAINT 绘制），白底深色文字
				data->hEdit = CreateWindowExW(0, L"EDIT", L"",
				                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
				                              40, 102, ACT_DLG_WIDTH - 80, 38, hwnd,
				                              reinterpret_cast<HMENU>(static_cast<std::intptr_t>(ACT_EDIT_ID)),
				                              hInst, nullptr);
				SendMessageW(data->hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontBody), TRUE);

				// 提示行：贴近输入框下方；默认灰色提示词，激活失败时切换红色错误提示（互斥显示）
				data->hError = CreateWindowExW(0, L"STATIC", ACT_HINT_TEXT.data(),
				                               WS_CHILD | WS_VISIBLE | SS_LEFT,
				                               40, 150, ACT_DLG_WIDTH - 80, 28, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(3005)),
				                               hInst, nullptr);
				SendMessageW(data->hError, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontHint), TRUE);

				// 按钮行：购买激活码（左）· 激 活 · 取 消（右）
				data->hBuy = CreateWindowExW(0, L"BUTTON", L"购买激活码",
				                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                             24, ACT_DLG_HEIGHT - 56, 100, 38, hwnd,
				                             reinterpret_cast<HMENU>(static_cast<std::intptr_t>(ACT_BUY_ID)),
				                             hInst, nullptr);
				SendMessageW(data->hBuy, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontSmall), TRUE);
				data->hOk = CreateWindowExW(0, L"BUTTON", L"激  活",
				                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
				                            ACT_DLG_WIDTH - 200, ACT_DLG_HEIGHT - 56, 110, 38, hwnd,
				                            reinterpret_cast<HMENU>(static_cast<std::intptr_t>(ACT_OK_ID)),
				                            hInst, nullptr);
				SendMessageW(data->hOk, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontSmall), TRUE);
				data->hCancel = CreateWindowExW(0, L"BUTTON", L"取  消",
				                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                                ACT_DLG_WIDTH - 88, ACT_DLG_HEIGHT - 56, 74, 38, hwnd,
				                                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(ACT_CANCEL_ID)),
				                                hInst, nullptr);
				SendMessageW(data->hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontSmall), TRUE);
				return 0;
			}
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				HDC hdc = BeginPaint(hwnd, &ps);
				RECT rcClient{};
				GetClientRect(hwnd, &rcClient);
				// 背景：浅蓝 → 白 垂直渐变
				fill_v_gradient(hdc, rcClient, RGB(0xee, 0xf5, 0xfe), RGB(0xff, 0xff, 0xff));
				// 左上角锁图标（主题蓝描边）
				{
					HPEN hPen = CreatePen(PS_SOLID, 2, CLEAN_ACCENT);
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
					// 锁梁（圆弧）
					Ellipse(hdc, 30, 24, 44, 38);
					// 锁体（圆角矩形）
					RoundRect(hdc, 26, 34, 48, 50, 3, 3);
					// 锁孔
					Ellipse(hdc, 34, 39, 40, 45);
					SelectObject(hdc, hOldBr);
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
				}
				// 标题下分隔线
				{
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xe8, 0xee, 0xf6));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					MoveToEx(hdc, 24, 82, nullptr);
					LineTo(hdc, ACT_DLG_WIDTH - 24, 82);
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
				}
				// 输入框白色圆角面板 + 主题色边框
				{
					RECT rcInput{24, 92, ACT_DLG_WIDTH - 24, 144};
					draw_round_rect(hdc, rcInput, 8, RGB(0xff, 0xff, 0xff), CLEAN_ACCENT, 1);
				}
				EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_CTLCOLOREDIT:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, RGB(0x24, 0x28, 0x2e));
				return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
			}
			case WM_CTLCOLORSTATIC:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				const HWND hWnd = reinterpret_cast<HWND>(lParam);
				SetBkMode(hdc, TRANSPARENT);
				if (hWnd == data->hError)
				{
					SetTextColor(hdc, data->errorMode ? CLEAN_DANGER : CLEAN_TEXT_SUB);
				}
				else if (hWnd == data->hSub)
				{
					SetTextColor(hdc, CLEAN_TEXT_SUB);
				}
				else
				{
					SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
				}
				return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
			}
			case WM_CTLCOLORBTN:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetBkMode(hdc, TRANSPARENT);
				return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
			}
			case WM_DRAWITEM:
			{
				const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
				const int id = dis->CtlID;
				if (id == ACT_OK_ID || id == ACT_CANCEL_ID || id == ACT_BUY_ID)
				{
					const bool isOk = (id == ACT_OK_ID);
					const bool isBuy = (id == ACT_BUY_ID);
					const bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
					const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
					wchar_t buf[32]{};
					GetWindowTextW(dis->hwndItem, buf, 32);
					draw_modern_dlg_button(dis->hDC, dis->rcItem, isOk, hover, pressed, buf, isBuy);
					return TRUE;
				}
				return FALSE;
			}
			case WM_COMMAND:
			{
				const int id = LOWORD(wParam);
				if (id == ACT_EDIT_ID && HIWORD(wParam) == EN_CHANGE)
				{
					// 重新输入时恢复灰色提示词（清除红色错误提示）
					activate_dlg_show_hint(*data);
					return 0;
				}
				if (id == ACT_BUY_ID)
				{
					if (MainApp::kBuyLicenseUrl.empty())
					{
						MessageBoxW(hwnd, L"购买链接尚未配置，请联系作者获取。", L"购买激活码", MB_OK | MB_ICONINFORMATION);
					}
					else
					{
						ShellExecuteW(nullptr, L"open", std::wstring{MainApp::kBuyLicenseUrl}.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
					return 0;
				}
				if (id == ACT_OK_ID)
				{
					wchar_t buf[1024]{};
					GetWindowTextW(data->hEdit, buf, 1024);
					const std::wstring code = buf;
					if (code.empty())
					{
						activate_dlg_show_error(*data, L"请输入激活码");
						return 0;
					}
					if (biz::license::tryActivate(code))
					{
						data->activated = true;
						DestroyWindow(hwnd);
					}
					else
					{
						activate_dlg_show_error(*data, L"激活码无效，请重新输入");
						SetFocus(data->hEdit);
						SendMessageW(data->hEdit, EM_SETSEL, 0, static_cast<LPARAM>(-1));
					}
					return 0;
				}
				if (id == ACT_CANCEL_ID)
				{
					data->activated = false;
					DestroyWindow(hwnd);
					return 0;
				}
				return 0;
			}
			case WM_CLOSE:
				data->activated = false;
				DestroyWindow(hwnd);
				return 0;
			case WM_DESTROY:
				if (data->hFontTitle)
				{
					DeleteObject(data->hFontTitle);
				}
				if (data->hFontBody)
				{
					DeleteObject(data->hFontBody);
				}
				if (data->hFontSmall)
				{
					DeleteObject(data->hFontSmall);
				}
				if (data->hFontHint)
				{
					DeleteObject(data->hFontHint);
				}
				data->done = true;
				return 0;
			default:
				break;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}

	bool license_activation_dialog(const WindowBase* owner)
	{
		static const bool classRegistered = []()
		{
			WNDCLASSEXW wc = {sizeof(wc)};
			wc.lpfnWndProc = activate_dlg_proc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
			wc.lpszClassName = ACTIVATE_DLG_CLASS;
			return RegisterClassExW(&wc) != 0;
		}();
		(void)classRegistered;

		ActivateDialogData data;
		const int titleBarHeight = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CXFIXEDFRAME) * 2;
		const int dlgWndWidth = ACT_DLG_WIDTH + GetSystemMetrics(SM_CXFIXEDFRAME) * 2;
		const int dlgWndHeight = ACT_DLG_HEIGHT + titleBarHeight;

		HWND hOwner = owner ? owner->nativeHandle() : nullptr;
		int x = 0;
		int y = 0;
		if (hOwner && IsWindow(hOwner))
		{
			RECT rc{};
			GetWindowRect(hOwner, &rc);
			x = rc.left + (rc.right - rc.left - dlgWndWidth) / 2;
			y = rc.top + (rc.bottom - rc.top - dlgWndHeight) / 2;
		}
		const HWND hDlg = CreateWindowExW(0, ACTIVATE_DLG_CLASS, L"软件激活",
		                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
		                                  x, y, dlgWndWidth, dlgWndHeight, hOwner, nullptr,
		                                  GetModuleHandleW(nullptr), &data);
		if (!hDlg)
		{
			return false;
		}
		ShowWindow(hDlg, SW_SHOW);
		SetFocus(data.hEdit);
		SendMessageW(data.hEdit, EM_SETSEL, 0, static_cast<LPARAM>(-1));

		if (hOwner && IsWindow(hOwner))
		{
			EnableWindow(hOwner, FALSE);
		}

		MSG msg{};
		while (!data.done && GetMessageW(&msg, nullptr, 0, 0))
		{
			if (IsDialogMessageW(hDlg, &msg))
			{
				continue;
			}
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		if (hOwner && IsWindow(hOwner))
		{
			EnableWindow(hOwner, TRUE);
			SetForegroundWindow(hOwner);
		}

		return data.activated;
	}

	// ---- 授权信息对话框（现代化渐变风格）----
	namespace
	{
		constexpr wchar_t LICENSE_INFO_CLASS[] = L"2BoxLicenseInfoDialog";
		constexpr int INFO_REACTIVATE_ID = 4001;
		constexpr int INFO_BUY_ID = 4002;
		constexpr int INFO_SERVICE_ID = 4003;
		constexpr int INFO_CLOSE_ID = 4004;
		constexpr int INFO_UNBIND_ID = 4005;
		constexpr int INFO_COPY_CODE_ID = 4006;
		constexpr int INFO_DLG_WIDTH = 520;
		constexpr int INFO_DLG_HEIGHT = 360;

		struct LicenseInfoDialogData
		{
			HWND hwnd{nullptr};
			HFONT hFontTitle{nullptr};
			HFONT hFontBody{nullptr};
			HFONT hFontSmall{nullptr};
			HFONT hFontHint{nullptr};
			bool done{false};
			LicenseInfoResult result{LicenseInfoResult::None};
			bool activated{false};
			bool isBound{false};
			int unbindCount{0};
			int unbindMax{0};
			std::wstring expireText;
			std::wstring fp;
			std::wstring version;
		};

		LRESULT CALLBACK license_info_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* data = reinterpret_cast<LicenseInfoDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			switch (msg)
			{
			case WM_CREATE:
			{
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
				data = static_cast<LicenseInfoDialogData*>(cs->lpCreateParams);
				data->hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

				// 收集展示数据
				data->activated = biz::license::isActivated();
				data->expireText = biz::license::expireDateText();
				data->fp = biz::license::machineFingerprint();
				data->version = MainApp::appVersion;
				data->isBound = biz::license::isBound();
				data->unbindCount = biz::license::unbindCountThisMonth();
				data->unbindMax = biz::license::unbindMaxPerMonth();

				const HMODULE hInst = GetModuleHandleW(nullptr);
				{
					const HDC hdc = GetDC(hwnd);
					const int titleSize = -MulDiv(19, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					const int bodySize = -MulDiv(13, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					const int smallSize = -MulDiv(11, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					const int hintSize = -MulDiv(10, GetDeviceCaps(hdc, LOGPIXELSY), 72);
					ReleaseDC(hwnd, hdc);
					data->hFontTitle = CreateFontW(titleSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
					                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
					data->hFontBody = CreateFontW(bodySize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
					                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
					data->hFontSmall = CreateFontW(smallSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
					                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
					data->hFontHint = CreateFontW(hintSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
					                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
					                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
				}

				// 底部按钮行：重新激活/续期 · 购买激活码 · 联系客服 · 关闭（解绑按钮移到解绑次数行内）
				auto createBtn = [&](int id, LPCWSTR text, int x, int w, bool defPush = false)
				{
					CreateWindowExW(0, L"BUTTON", text,
					                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | (defPush ? BS_DEFPUSHBUTTON : 0),
					                x, INFO_DLG_HEIGHT - 54, w, 38, hwnd,
					                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(id)),
					                hInst, nullptr);
				};
				if (data->isBound)
				{
					createBtn(INFO_REACTIVATE_ID, L"重新激活/续期", 24, 120, true);
					createBtn(INFO_BUY_ID, L"购买激活码", 160, 110);
					createBtn(INFO_SERVICE_ID, L"联系客服", 290, 96);
					createBtn(INFO_CLOSE_ID, L"关闭", 408, 88);
					// 解绑按钮放在"解绑次数"行内（rowTop=230，与值文本等高）
					CreateWindowExW(0, L"BUTTON", L"解绑本机",
					                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
					                396, 229, 88, 28, hwnd,
					                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INFO_UNBIND_ID)),
					                hInst, nullptr);
				}
				else
				{
					createBtn(INFO_REACTIVATE_ID, L"重新激活/续期", 24, 130, true);
					createBtn(INFO_BUY_ID, L"购买激活码", 170, 120);
					createBtn(INFO_SERVICE_ID, L"联系客服", 310, 100);
					createBtn(INFO_CLOSE_ID, L"关闭", 432, 64);
				}
				// 授权信息标题右侧：复制激活码按钮（仅已激活时可用）
				if (data->activated)
				{
					CreateWindowExW(0, L"BUTTON", L"复制激活码",
					                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
					                INFO_DLG_WIDTH - 140, 24, 116, 28, hwnd,
					                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INFO_COPY_CODE_ID)),
					                hInst, nullptr);
				}
				return 0;
			}
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				HDC hdc = BeginPaint(hwnd, &ps);
				RECT rcClient{};
				GetClientRect(hwnd, &rcClient);
				// 背景：浅蓝 → 白 垂直渐变
				fill_v_gradient(hdc, rcClient, RGB(0xee, 0xf5, 0xfe), RGB(0xff, 0xff, 0xff));

				// 头部信息徽标（主题蓝圆底 + 白色 i）
				{
					HBRUSH hCircle = CreateSolidBrush(CLEAN_ACCENT);
					HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hCircle));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, GetStockObject(NULL_PEN)));
					Ellipse(hdc, 26, 22, 50, 46);
					SelectObject(hdc, hOldBr);
					DeleteObject(hCircle);
					SelectObject(hdc, hOldPen);
					SetTextColor(hdc, RGB(0xff, 0xff, 0xff));
					SetBkMode(hdc, TRANSPARENT);
					RECT rcI{26, 24, 50, 44};
					DrawTextW(hdc, L"i", -1, &rcI, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				}
				// 标题
				{
					SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
					SetBkMode(hdc, TRANSPARENT);
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontTitle));
					RECT rcTitle{58, 22, INFO_DLG_WIDTH - 24, 50};
					DrawTextW(hdc, L"授权信息", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SelectObject(hdc, hOldFont);
				}
				// 副标题
				{
					SetTextColor(hdc, CLEAN_TEXT_SUB);
					SetBkMode(hdc, TRANSPARENT);
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontSmall));
					RECT rcSub{58, 52, INFO_DLG_WIDTH - 24, 72};
					DrawTextW(hdc, L"软件授权状态与续期入口", -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SelectObject(hdc, hOldFont);
				}
				// 分隔线
				{
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xe8, 0xee, 0xf6));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					MoveToEx(hdc, 24, 80, nullptr);
					LineTo(hdc, INFO_DLG_WIDTH - 24, 80);
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
				}
				// 白色信息卡片（5 行信息）
				{
					RECT rcCard{24, 92, INFO_DLG_WIDTH - 24, 260};
					draw_round_rect(hdc, rcCard, 10, RGB(0xff, 0xff, 0xff), RGB(0xe8, 0xee, 0xf6), 1);
					// 行分隔线
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xf0, 0xf4, 0xf8));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					for (int row = 1; row < 5; ++row)
					{
						const int y = rcCard.top + 6 + row * 33;
						MoveToEx(hdc, rcCard.left + 16, y, nullptr);
						LineTo(hdc, rcCard.right - 16, y);
					}
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);

					const wchar_t* labels[5] = {L"激活状态", L"到期时间", L"当前版本", L"本机指纹", L"解绑次数"};
					std::wstring unbindText;
					if (data->isBound)
					{
						if (data->unbindMax < 0)
						{
							unbindText = std::format(L"本月已解绑 {} 次 · 不限次数", data->unbindCount);
						}
						else
						{
							unbindText = std::format(L"本月已解绑 {} / {} 次", data->unbindCount, data->unbindMax);
						}
					}
					else
					{
						unbindText = L"非绑定码，无需解绑";
					}
					const std::wstring values[5] =
					{
						data->activated ? L"已激活" : L"未激活",
						data->activated && !data->expireText.empty() ? data->expireText : L"（未激活）",
						data->version,
						data->fp,
						unbindText,
					};
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontSmall));
					SetBkMode(hdc, TRANSPARENT);
					for (int row = 0; row < 5; ++row)
					{
						const int rowTop = rcCard.top + 6 + row * 33;
						// 标签（浅灰）
						SetTextColor(hdc, CLEAN_TEXT_SUB);
						RECT rcLabel{rcCard.left + 18, rowTop, rcCard.left + 140, rowTop + 26};
						DrawTextW(hdc, labels[row], -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
						// 值（深色；激活状态/解绑次数特殊着色）
						if (row == 0)
						{
							SetTextColor(hdc, data->activated ? RGB(0x16, 0xa3, 0x4a) : CLEAN_DANGER);
						}
						else if (row == 4 && data->isBound && data->unbindMax >= 0 &&
						         data->unbindCount >= data->unbindMax)
						{
							// 解绑次数已用尽 → 红色警示
							SetTextColor(hdc, CLEAN_DANGER);
						}
						else
						{
							SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
						}
						RECT rcValue{rcCard.left + 140, rowTop, rcCard.right - 18, rowTop + 26};
						// 解绑次数行：行内有解绑按钮，值文本结尾避开按钮
						if (row == 4 && data->isBound)
						{
							rcValue.right = 388;
						}
						DrawTextW(hdc, values[row].c_str(), -1, &rcValue, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
					}
					SelectObject(hdc, hOldFont);
				}
				// 底部提示（10px 小字）
				{
					SetTextColor(hdc, CLEAN_TEXT_SUB);
					SetBkMode(hdc, TRANSPARENT);
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontHint));
					RECT rcHint{28, 272, INFO_DLG_WIDTH - 28, 292};
					DrawTextW(hdc, L"到期后无法启动环境和新增环境，可在界面内重新输入激活码续期", -1, &rcHint, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SelectObject(hdc, hOldFont);
				}
				EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_CTLCOLORBTN:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetBkMode(hdc, TRANSPARENT);
				return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
			}
			case WM_DRAWITEM:
			{
				const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
				const int id = dis->CtlID;
				if (id == INFO_REACTIVATE_ID || id == INFO_BUY_ID || id == INFO_SERVICE_ID || id == INFO_CLOSE_ID || id == INFO_UNBIND_ID || id == INFO_COPY_CODE_ID)
				{
					const bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
					const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
					const bool primary = (id == INFO_REACTIVATE_ID);
					const bool danger = (id == INFO_UNBIND_ID);
					const bool success = (id == INFO_BUY_ID); // 购买激活码按钮绿色
					wchar_t buf[32]{};
					GetWindowTextW(dis->hwndItem, buf, 32);
					draw_modern_dlg_button(dis->hDC, dis->rcItem, primary, hover, pressed, buf, danger, success);
					return TRUE;
				}
				return FALSE;
			}
			case WM_COMMAND:
			{
				const int id = LOWORD(wParam);
				if (id == INFO_COPY_CODE_ID)
				{
					const std::wstring code = biz::license::currentActivationCode();
					if (!code.empty() && OpenClipboard(hwnd))
					{
						EmptyClipboard();
						const std::size_t bytes = (code.size() + 1) * sizeof(wchar_t);
						if (HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes))
						{
							if (wchar_t* p = static_cast<wchar_t*>(GlobalLock(hMem)))
							{
								memcpy(p, code.c_str(), bytes);
								GlobalUnlock(hMem);
								SetClipboardData(CF_UNICODETEXT, hMem);
							}
						}
						CloseClipboard();
						MessageBoxW(hwnd, L"激活码已复制到剪贴板", L"复制成功", MB_OK | MB_ICONINFORMATION);
					}
					return 0;
				}
				if (id == INFO_REACTIVATE_ID)
				{
					data->result = LicenseInfoResult::Reactivate;
					DestroyWindow(hwnd);
					return 0;
				}
				if (id == INFO_UNBIND_ID)
				{
					const std::wstring limitText = (data->unbindMax < 0)
						? std::format(L"本月已解绑 {} 次（不限次数）", data->unbindCount)
						: std::format(L"本月已解绑 {} / {} 次", data->unbindCount, data->unbindMax);
					const int ask = MessageBoxW(hwnd,
						(L"解绑后本机将无法使用本软件，该激活码可转移到其他电脑激活。\n\n" + limitText + L"\n\n确认解绑本机？").c_str(),
						L"解绑本机", MB_YESNO | MB_ICONWARNING | MB_TASKMODAL);
					if (ask != IDYES)
					{
						return 0;
					}
					const biz::license::UnbindResult r = biz::license::unbind();
					switch (r)
					{
					case biz::license::UnbindResult::Success:
						data->result = LicenseInfoResult::Unbound;
						MessageBoxW(hwnd, L"解绑成功！本机已退出授权，应用即将关闭。", L"解绑本机", MB_OK | MB_ICONINFORMATION);
						DestroyWindow(hwnd);
						return 0;
					case biz::license::UnbindResult::OtherInstancesRunning:
						MessageBoxW(hwnd, L"检测到其他 2Box 进程正在运行。\n请先关闭所有 2Box 窗口和进程后再解绑。", L"解绑本机", MB_OK | MB_ICONWARNING);
						return 0;
					case biz::license::UnbindResult::ExceededLimit:
						MessageBoxW(hwnd,
							(data->unbindMax == 0
								? L"该激活码已设置禁止解绑。"
								: std::format(L"本月解绑次数已达上限（{} 次），请下月再试。", data->unbindMax).c_str()),
							L"解绑本机", MB_OK | MB_ICONWARNING);
						return 0;
					default:
						return 0;
					}
				}
				if (id == INFO_BUY_ID)
				{
					if (MainApp::kBuyLicenseUrl.empty())
					{
						MessageBoxW(hwnd, L"购买链接尚未配置，请联系作者获取。", L"购买激活码", MB_OK | MB_ICONINFORMATION);
					}
					else
					{
						ShellExecuteW(nullptr, L"open", std::wstring{MainApp::kBuyLicenseUrl}.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
					return 0;
				}
				if (id == INFO_SERVICE_ID)
				{
					if (MainApp::kServiceUrl.empty())
					{
						MessageBoxW(hwnd, L"客服链接尚未配置，请联系作者获取。", L"联系客服", MB_OK | MB_ICONINFORMATION);
					}
					else
					{
						ShellExecuteW(nullptr, L"open", std::wstring{MainApp::kServiceUrl}.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
					return 0;
				}
				if (id == INFO_CLOSE_ID)
				{
					DestroyWindow(hwnd);
					return 0;
				}
				return 0;
			}
			case WM_CLOSE:
				DestroyWindow(hwnd);
				return 0;
			case WM_DESTROY:
				if (data->hFontTitle)
				{
					DeleteObject(data->hFontTitle);
				}
				if (data->hFontBody)
				{
					DeleteObject(data->hFontBody);
				}
				if (data->hFontSmall)
				{
					DeleteObject(data->hFontSmall);
				}
				if (data->hFontHint)
				{
					DeleteObject(data->hFontHint);
				}
				data->done = true;
				return 0;
			default:
				break;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}

	LicenseInfoResult license_info_dialog(const WindowBase* owner)
	{
		static const bool classRegistered = []()
		{
			WNDCLASSEXW wc = {sizeof(wc)};
			wc.lpfnWndProc = license_info_proc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
			wc.lpszClassName = LICENSE_INFO_CLASS;
			return RegisterClassExW(&wc) != 0;
		}();
		(void)classRegistered;

		LicenseInfoDialogData data;
		const int titleBarHeight = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CXFIXEDFRAME) * 2;
		const int dlgWndWidth = INFO_DLG_WIDTH + GetSystemMetrics(SM_CXFIXEDFRAME) * 2;
		const int dlgWndHeight = INFO_DLG_HEIGHT + titleBarHeight;

		HWND hOwner = owner ? owner->nativeHandle() : nullptr;
		int x = 0;
		int y = 0;
		if (hOwner && IsWindow(hOwner))
		{
			RECT rc{};
			GetWindowRect(hOwner, &rc);
			x = rc.left + (rc.right - rc.left - dlgWndWidth) / 2;
			y = rc.top + (rc.bottom - rc.top - dlgWndHeight) / 2;
		}
		const HWND hDlg = CreateWindowExW(0, LICENSE_INFO_CLASS, L"授权信息",
		                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
		                                  x, y, dlgWndWidth, dlgWndHeight, hOwner, nullptr,
		                                  GetModuleHandleW(nullptr), &data);
		if (!hDlg)
		{
			return LicenseInfoResult::None;
		}
		ShowWindow(hDlg, SW_SHOW);

		if (hOwner && IsWindow(hOwner))
		{
			EnableWindow(hOwner, FALSE);
		}

		MSG msg{};
		while (!data.done && GetMessageW(&msg, nullptr, 0, 0))
		{
			if (IsDialogMessageW(hDlg, &msg))
			{
				continue;
			}
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		if (hOwner && IsWindow(hOwner))
		{
			EnableWindow(hOwner, TRUE);
			SetForegroundWindow(hOwner);
		}

		return data.result;
	}
}
