module;
#define NOMINMAX
#include <shobjidl_core.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>
#include <tlhelp32.h>
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "comctl32.lib")
module UI.Core;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif
import MainApp;
import biz.License;

// ---- GDI 弹窗统一 DPI 缩放 ----
// 所有自绘弹窗的布局尺寸一律按 96 DPI 的设计值书写，运行时再用 dlg_s() 换算成
// 窗口所在显示器的实际像素，避免"字号随 DPI 放大、容器尺寸不变"造成的拥挤/过小。
namespace ui
{
	int dlg_dpi(HWND hwnd)
	{
		const HDC hdc = GetDC(hwnd);
		if (hdc != nullptr)
		{
			const int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
			ReleaseDC(hwnd, hdc);
			if (dpi > 0)
			{
				return dpi;
			}
		}
		return 96;
	}

	int dlg_s(int value, int dpi)
	{
		return MulDiv(value, dpi > 0 ? dpi : 96, 96);
	}

	// 弹窗所在显示器的工作区尺寸（限制弹窗最大尺寸，避免高缩放/矮屏下超出屏幕）
	SIZE dlg_work_area(HWND owner)
	{
		const HWND hRef = (owner != nullptr && IsWindow(owner)) ? owner : GetForegroundWindow();
		MONITORINFO mi{sizeof(MONITORINFO)};
		const HMONITOR hMon = MonitorFromWindow(hRef, MONITOR_DEFAULTTONEAREST);
		if (hMon != nullptr && GetMonitorInfoW(hMon, &mi))
		{
			return SIZE{mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top};
		}
		return SIZE{GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
	}

	// WM_DPICHANGED 公共处理：按系统建议矩形调整窗口，并返回窗口新的 DPI
	int dlg_on_dpi_changed(HWND hwnd, LPARAM lParam)
	{
		const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
		if (suggested != nullptr)
		{
			SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
			             suggested->right - suggested->left, suggested->bottom - suggested->top,
			             SWP_NOZORDER | SWP_NOACTIVATE);
		}
		return dlg_dpi(hwnd);
	}

	// 按 DPI 缩放设计客户区尺寸，并以当前显示器工作区为上限（极端缩放/矮屏下不至于超出屏幕）
	SIZE dlg_client_size(HWND owner, int clientW, int clientH)
	{
		const int dpi = dlg_dpi(owner);
		const SIZE work = dlg_work_area(owner);
		const int w = dlg_s(clientW, dpi);
		const int h = dlg_s(clientH, dpi);
		const int maxW = std::max(dlg_s(320, dpi), static_cast<int>(work.cx) - dlg_s(48, dpi));
		const int maxH = std::max(dlg_s(240, dpi), static_cast<int>(work.cy) - dlg_s(48, dpi));
		return SIZE{w > maxW ? maxW : w, h > maxH ? maxH : h};
	}

	// 把客户区精确校正为期望像素尺寸：非客户区由系统按窗口 DPI 缩放，
	// 直接用系统度量估算会在高 DPI 下偏小，导致底部按钮被裁掉。
	void dlg_fit_client(HWND hwnd, SIZE client)
	{
		RECT rcClient{};
		RECT rcWnd{};
		GetClientRect(hwnd, &rcClient);
		GetWindowRect(hwnd, &rcWnd);
		const int extraW = (rcWnd.right - rcWnd.left) - (rcClient.right - rcClient.left);
		const int extraH = (rcWnd.bottom - rcWnd.top) - (rcClient.bottom - rcClient.top);
		const int w = client.cx + extraW;
		const int h = client.cy + extraH;
		if (w != rcWnd.right - rcWnd.left || h != rcWnd.bottom - rcWnd.top)
		{
			SetWindowPos(hwnd, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
		}
	}

	// 相对 owner 居中，并保证窗口完整落在 owner 所在显示器的工作区内
	void dlg_center_on(HWND hwnd, HWND owner)
	{
		RECT rc{};
		RECT rcOwner{};
		GetWindowRect(hwnd, &rc);
		const int w = rc.right - rc.left;
		const int h = rc.bottom - rc.top;
		int x = 0;
		int y = 0;
		if (owner != nullptr && IsWindow(owner) && GetWindowRect(owner, &rcOwner))
		{
			x = rcOwner.left + (rcOwner.right - rcOwner.left - w) / 2;
			y = rcOwner.top + (rcOwner.bottom - rcOwner.top - h) / 2;
		}
		MONITORINFO mi{sizeof(MONITORINFO)};
		const HMONITOR hMon = MonitorFromWindow(owner != nullptr ? owner : hwnd, MONITOR_DEFAULTTONEAREST);
		if (hMon != nullptr && GetMonitorInfoW(hMon, &mi))
		{
			if (x + w > mi.rcWork.right)
			{
				x = mi.rcWork.right - w;
			}
			if (y + h > mi.rcWork.bottom)
			{
				y = mi.rcWork.bottom - h;
			}
			if (x < mi.rcWork.left)
			{
				x = mi.rcWork.left;
			}
			if (y < mi.rcWork.top)
			{
				y = mi.rcWork.top;
			}
		}
		SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

namespace ui
{
	namespace
	{
		// 现代化文本输入弹窗（内容全部 GDI 自绘，实现在本文件的绘制工具之后）
		std::optional<std::wstring> show_input_dialog(HWND owner, std::wstring_view title, std::wstring_view initial);
	}

	std::optional<std::wstring> input_text(const WindowBase* owner, std::wstring_view title, std::wstring_view initial)
	{
		return show_input_dialog(owner ? owner->nativeHandle() : nullptr, title, initial);
	}

	// 通用"选择可执行文件"系统对话框（父窗口句柄版本，供 select_file 与应用选择器共用）
	std::optional<std::wstring> run_file_open_dialog(HWND parent)
	{
		UniqueComPtr<IFileOpenDialog> fileOpen;
		HResult hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&fileOpen));
		if (FAILED(hr))
		{
			MessageBoxW(parent,
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
		hr = fileOpen->Show(parent);
		if (FAILED(hr))
		{
			if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
			{
				MessageBoxW(parent,
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
			MessageBoxW(parent,
			            std::format(L"无法获取选择的文件! HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)).c_str(),
			            MainApp::appName.data(),
			            MB_OK | MB_ICONERROR | MB_TASKMODAL);
			return std::nullopt;
		}
		PWSTR pszFilePath;
		hr = item->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
		if (FAILED(hr))
		{
			MessageBoxW(parent,
			            std::format(L"无法获取选择的文件路径! HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)).c_str(),
			            MainApp::appName.data(),
			            MB_OK | MB_ICONERROR | MB_TASKMODAL);
			return std::nullopt;
		}
		std::wstring procFullPath{pszFilePath};
		CoTaskMemFree(pszFilePath);
		return procFullPath;
	}

	std::optional<std::wstring> select_file(const WindowBase* owner)
	{
		return run_file_open_dialog(owner ? owner->nativeHandle() : nullptr);
	}

	// ---- 清理环境数据对话框：仅缓存 / 仅聊天记录 / 都清理 ----
	namespace
	{
		constexpr wchar_t CLEAN_DLG_CLASS[] = L"eBoxCleanDialog";
		constexpr int CLEAN_RB_BASE_ID = 2001;   // 三个单选按钮：2001/2002/2003
		constexpr int CLEAN_OK_ID = 2004;
		constexpr int CLEAN_CANCEL_ID = 2005;

		// 对话框设计尺寸（客户区）
		constexpr int CLEAN_DLG_WIDTH = 480;
		constexpr int CLEAN_DLG_HEIGHT = 360;
		// 主题色（与 eBox 主色调一致的现代蓝）
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
			int dpi{96};
			std::wstring envName;
			bool done{false};
			bool cancelled{true};
			std::optional<ECleanOption> result{std::nullopt};

			// 各选项对应的清理内容提示
			static constexpr std::wstring_view descCache{
				L"清理该环境 QYWX 的 CEF 渲染缓存（qtCef / WXWorkCefCache /\n"
				L"GPU 着色器等），下次启动自动重建。\n\n"
				L"不影响：聊天记录、登录状态、企业配置、Default 会话数据。"
			};
			static constexpr std::wstring_view descChatData{
				L"清理该环境 QYWX 的聊天记录（Profiles / Data 等消息库、\n"
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
		void draw_modern_radio(HDC hdc, const RECT& rc, bool selected, bool hover, bool danger, LPCWSTR text, int dpi = 96)
		{
			const auto S = [dpi](int v) { return ui::dlg_s(v, dpi); };
			// 选项整行背景（选中时极浅主色，未选中白色）
			HBRUSH hBg = CreateSolidBrush(selected ? RGB(0xe3, 0xf2, 0xfd) : CLEAN_CARD);
			FillRect(hdc, &rc, hBg);
			DeleteObject(hBg);

			// 方框（16x16）左侧垂直居中
			const int boxSize = S(16);
			const int boxX = rc.left + S(6);
			const int boxY = (rc.top + rc.bottom - boxSize) / 2;

			// 未选中：细描边方框；选中：主色实底方框 + 白色对勾
			HPEN hPen = CreatePen(PS_SOLID, selected ? 2 : 1, selected ? CLEAN_ACCENT : (hover ? CLEAN_ACCENT_LIGHT : CLEAN_BORDER));
			HBRUSH hBoxBg = CreateSolidBrush(selected ? CLEAN_ACCENT : CLEAN_CARD);
			HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
			HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hBoxBg));
			RECT boxRc{boxX, boxY, boxX + boxSize, boxY + boxSize};
			RoundRect(hdc, boxRc.left, boxRc.top, boxRc.right, boxRc.bottom, S(3), S(3));
			SelectObject(hdc, hOldBr);
			DeleteObject(hBoxBg);
			SelectObject(hdc, hOldPen);
			DeleteObject(hPen);

			// 选中：画白色对勾（两段线段）
			if (selected)
			{
				HPEN hCheckPen = CreatePen(PS_SOLID, 2, RGB(0xff, 0xff, 0xff));
				HPEN hOldCheckPen = static_cast<HPEN>(SelectObject(hdc, hCheckPen));
				const float cx0 = static_cast<float>(boxX + S(3)), cy0 = static_cast<float>(boxY + S(9));
				const float cx1 = static_cast<float>(boxX + S(7)), cy1 = static_cast<float>(boxY + S(12));
				const float cx2 = static_cast<float>(boxX + S(13)), cy2 = static_cast<float>(boxY + S(5));
				MoveToEx(hdc, static_cast<int>(cx0), static_cast<int>(cy0), nullptr);
				LineTo(hdc, static_cast<int>(cx1), static_cast<int>(cy1));
				LineTo(hdc, static_cast<int>(cx2), static_cast<int>(cy2));
				SelectObject(hdc, hOldCheckPen);
				DeleteObject(hCheckPen);
			}

			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, danger ? CLEAN_DANGER : CLEAN_TEXT);
			RECT textRc = rc;
			textRc.left = boxX + boxSize + S(8);
			DrawTextW(hdc, text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		}

		// 按当前 DPI 摆放清理弹窗的子控件（WM_CREATE 与 WM_DPICHANGED 共用）
		void clean_dlg_layout(CleanDialogData& data)
		{
			const auto S = [dpi = data.dpi](int v) { return ui::dlg_s(v, dpi); };
			const auto place = [](HWND h, int x, int y, int w, int hgt)
			{
				if (h)
				{
					SetWindowPos(h, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
				}
			};
			place(data.hTitle, S(24), S(18), S(CLEAN_DLG_WIDTH - 48), S(26));
			for (int i = 0; i < 3; ++i)
			{
				place(data.hRadio[i], S(24), S(60 + i * 36), S(CLEAN_DLG_WIDTH - 48), S(30));
			}
			place(data.hDesc, S(24), S(172), S(CLEAN_DLG_WIDTH - 48), S(116));
			place(data.hOk, S(CLEAN_DLG_WIDTH - 190), S(CLEAN_DLG_HEIGHT - 52), S(88), S(34));
			place(data.hCancel, S(CLEAN_DLG_WIDTH - 96), S(CLEAN_DLG_HEIGHT - 52), S(72), S(34));
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
				data->dpi = ui::dlg_dpi(hwnd);
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
				                               0, 0, 0, 0, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(1000)),
				                               hInst, nullptr);
				SendMessageW(data->hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontTitle), TRUE);

				// 三个自绘单选按钮（卡片式）
				constexpr std::wstring_view labels[3] = {L"仅清理缓存", L"仅清理聊天记录", L"都清理"};
				for (int i = 0; i < 3; ++i)
				{
					data->hRadio[i] = CreateWindowExW(0, L"BUTTON", labels[i].data(),
					                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | (i == 0 ? WS_GROUP : 0),
					                                  0, 0, 0, 0, hwnd,
					                                  reinterpret_cast<HMENU>(static_cast<std::intptr_t>(CLEAN_RB_BASE_ID + i)),
					                                  hInst, nullptr);
					SendMessageW(data->hRadio[i], WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontBody), TRUE);
				}

				// 描述区（无边框，11px 小字，SS_LEFT 自动换行完整显示）
				data->hDesc = CreateWindowExW(0, L"STATIC", L"",
				                              WS_CHILD | WS_VISIBLE | SS_LEFT,
				                              0, 0, 0, 0, hwnd,
				                              reinterpret_cast<HMENU>(static_cast<std::intptr_t>(1007)),
				                              hInst, nullptr);
				SendMessageW(data->hDesc, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontDesc), TRUE);

				// 按钮：确认（主色实底）/ 取消（描边）
				data->hOk = CreateWindowExW(0, L"BUTTON", L"确认清理",
				                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
				                            0, 0, 0, 0, hwnd,
				                            reinterpret_cast<HMENU>(static_cast<std::intptr_t>(CLEAN_OK_ID)),
				                            hInst, nullptr);
				SendMessageW(data->hOk, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontBody), TRUE);
				data->hCancel = CreateWindowExW(0, L"BUTTON", L"取消",
				                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                                0, 0, 0, 0, hwnd,
				                                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(CLEAN_CANCEL_ID)),
				                                hInst, nullptr);
				SendMessageW(data->hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontBody), TRUE);

				clean_dlg_layout(*data);
				clean_dlg_update_desc(*data);
				return 0;
			}
			case WM_DPICHANGED:
				data->dpi = ui::dlg_on_dpi_changed(hwnd, lParam);
				clean_dlg_layout(*data);
				InvalidateRect(hwnd, nullptr, TRUE);
				return 0;
			case WM_SIZE:
				// 创建后校正客户区尺寸会触发 WM_SIZE，需按最终客户区重新摆放控件
				if (data && data->hwnd && wParam != SIZE_MINIMIZED)
				{
					clean_dlg_layout(*data);
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				return 0;
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
					draw_modern_radio(dis->hDC, rc, selected, hover, danger, buf, data->dpi);
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
					const int radius = ui::dlg_s(6, data->dpi);
					RoundRect(dis->hDC, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
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

		// 客户区按 DPI 放大（设计值 480x360），窗口客户区在创建后再精确校正，
		// 避免高 DPI 下标题栏变高、内容被裁掉。
		HWND hOwner = owner ? owner->nativeHandle() : nullptr;
		const SIZE clientSize = dlg_client_size(hOwner, CLEAN_DLG_WIDTH, CLEAN_DLG_HEIGHT);

		const HWND hDlg = CreateWindowExW(0, CLEAN_DLG_CLASS, std::format(L"清理环境数据 - {}", envName).c_str(),
		                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
		                                  0, 0, clientSize.cx, clientSize.cy, hOwner, nullptr,
		                                  GetModuleHandleW(nullptr), &data);
		if (!hDlg)
		{
			return std::nullopt;
		}
		dlg_fit_client(hDlg, clientSize);
		dlg_center_on(hDlg, hOwner);
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

	// ---- 通用文本输入对话框（现代化渐变风格，供环境改名等处复用）----
	namespace
	{
		constexpr wchar_t INPUT_DLG_CLASS[] = L"eBoxInputDialog";
		constexpr int INPUT_TITLE_ID = 1000;
		constexpr int INPUT_SUB_ID = 1001;
		constexpr int INPUT_EDIT_ID = 1002;
		constexpr int INPUT_COUNT_ID = 1003;
		constexpr int INPUT_OK_ID = IDOK;
		constexpr int INPUT_CANCEL_ID = IDCANCEL;
		// 客户区设计尺寸（96 DPI 设计值，运行时一律经 ui::dlg_s 缩放）
		constexpr int INPUT_DLG_WIDTH = 440;
		constexpr int INPUT_DLG_HEIGHT = 224;
		// 渐变背景的下边界：此线以下统一纯白，使"字符计数"等会变的文字可用不透明白底擦除
		constexpr int INPUT_GRADIENT_BOTTOM = 126;

		struct InputDialogData
		{
			HWND hwnd{nullptr};
			HWND hTitle{nullptr};
			HWND hSub{nullptr};
			HWND hEdit{nullptr};
			HWND hCount{nullptr};
			HWND hOk{nullptr};
			HWND hCancel{nullptr};
			HFONT hFontTitle{nullptr};
			HFONT hFontBody{nullptr};
			HFONT hFontSmall{nullptr};
			HFONT hFontHint{nullptr};
			std::wstring title;
			std::wstring initial;
			std::wstring result;
			int dpi{96};
			bool editFocused{false}; // 输入框是否获得焦点（决定圆角面板的描边颜色）
			bool done{false};
			bool cancelled{true};
		};

		// 输入框白色圆角面板区域（WM_PAINT 绘制与鼠标命中判断共用）
		RECT input_dlg_field_rect(int dpi)
		{
			return RECT{ui::dlg_s(24, dpi), ui::dlg_s(78, dpi),
			            ui::dlg_s(INPUT_DLG_WIDTH - 24, dpi), ui::dlg_s(122, dpi)};
		}

		// 按窗口 DPI 重建字体（首次创建与跨屏 DPI 变化共用）
		void input_dlg_create_fonts(InputDialogData& data)
		{
			if (data.hFontTitle)
			{
				DeleteObject(data.hFontTitle);
			}
			if (data.hFontBody)
			{
				DeleteObject(data.hFontBody);
			}
			if (data.hFontSmall)
			{
				DeleteObject(data.hFontSmall);
			}
			if (data.hFontHint)
			{
				DeleteObject(data.hFontHint);
			}
			const auto make = [dpi = data.dpi](int pt, int weight)
			{
				return CreateFontW(-MulDiv(pt, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
				                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			};
			data.hFontTitle = make(16, FW_SEMIBOLD);
			data.hFontBody = make(13, FW_NORMAL);
			data.hFontSmall = make(12, FW_NORMAL);
			data.hFontHint = make(10, FW_NORMAL);
			if (data.hTitle)
			{
				SendMessageW(data.hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontTitle), TRUE);
			}
			if (data.hSub)
			{
				SendMessageW(data.hSub, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontSmall), TRUE);
			}
			if (data.hEdit)
			{
				SendMessageW(data.hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontBody), TRUE);
			}
			if (data.hCount)
			{
				SendMessageW(data.hCount, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontHint), TRUE);
			}
			if (data.hOk)
			{
				SendMessageW(data.hOk, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontSmall), TRUE);
			}
			if (data.hCancel)
			{
				SendMessageW(data.hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontSmall), TRUE);
			}
		}

		// 按当前 DPI 摆放子控件（WM_CREATE / WM_SIZE / WM_DPICHANGED 共用）
		void input_dlg_layout(InputDialogData& data)
		{
			const auto S = [dpi = data.dpi](int v) { return ui::dlg_s(v, dpi); };
			const auto place = [](HWND h, int x, int y, int w, int hgt)
			{
				if (h)
				{
					SetWindowPos(h, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
				}
			};
			place(data.hTitle, S(24), S(18), S(INPUT_DLG_WIDTH - 48), S(28));
			place(data.hSub, S(24), S(48), S(INPUT_DLG_WIDTH - 48), S(20));
			place(data.hEdit, S(36), S(87), S(INPUT_DLG_WIDTH - 72), S(26));
			place(data.hCount, S(INPUT_DLG_WIDTH - 164), S(130), S(140), S(16));
			place(data.hCancel, S(INPUT_DLG_WIDTH - 208), S(172), S(88), S(34));
			place(data.hOk, S(INPUT_DLG_WIDTH - 112), S(172), S(88), S(34));
		}

		// 刷新右下角字符计数（随输入实时更新）
		void input_dlg_update_count(InputDialogData& data)
		{
			if (!data.hCount)
			{
				return;
			}
			const int len = GetWindowTextLengthW(data.hEdit);
			SetWindowTextW(data.hCount, std::format(L"{} 个字符", len).c_str());
		}

		LRESULT CALLBACK input_dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* data = reinterpret_cast<InputDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			switch (msg)
			{
			case WM_CREATE:
			{
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
				data = static_cast<InputDialogData*>(cs->lpCreateParams);
				data->hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

				const HMODULE hInst = GetModuleHandleW(nullptr);
				data->dpi = ui::dlg_dpi(hwnd);

				data->hTitle = CreateWindowExW(0, L"STATIC", data->title.c_str(),
				                               WS_CHILD | WS_VISIBLE | SS_LEFT,
				                               0, 0, 0, 0, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INPUT_TITLE_ID)),
				                               hInst, nullptr);
				data->hSub = CreateWindowExW(0, L"STATIC", L"输入新的名称，仅在本机生效",
				                             WS_CHILD | WS_VISIBLE | SS_LEFT,
				                             0, 0, 0, 0, hwnd,
				                             reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INPUT_SUB_ID)),
				                             hInst, nullptr);
				// 输入框：无自带边框，白色圆角面板由 WM_PAINT 绘制
				data->hEdit = CreateWindowExW(0, L"EDIT", data->initial.c_str(),
				                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
				                              0, 0, 0, 0, hwnd,
				                              reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INPUT_EDIT_ID)),
				                              hInst, nullptr);
				data->hCount = CreateWindowExW(0, L"STATIC", L"",
				                               WS_CHILD | WS_VISIBLE | SS_RIGHT,
				                               0, 0, 0, 0, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INPUT_COUNT_ID)),
				                               hInst, nullptr);

				// 按钮：确定（主色实底）· 取消（描边）
				data->hOk = CreateWindowExW(0, L"BUTTON", L"确定",
				                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
				                            0, 0, 0, 0, hwnd,
				                            reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INPUT_OK_ID)),
				                            hInst, nullptr);
				data->hCancel = CreateWindowExW(0, L"BUTTON", L"取消",
				                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                                0, 0, 0, 0, hwnd,
				                                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INPUT_CANCEL_ID)),
				                                hInst, nullptr);

				input_dlg_create_fonts(*data);
				input_dlg_layout(*data);
				input_dlg_update_count(*data);
				return 0;
			}
			case WM_DPICHANGED:
				data->dpi = ui::dlg_on_dpi_changed(hwnd, lParam);
				input_dlg_create_fonts(*data);
				input_dlg_layout(*data);
				InvalidateRect(hwnd, nullptr, TRUE);
				return 0;
			case WM_SIZE:
				// 创建后校正客户区尺寸会触发 WM_SIZE，需按最终客户区重新摆放控件
				if (data && data->hwnd && wParam != SIZE_MINIMIZED)
				{
					input_dlg_layout(*data);
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				return 0;
			case WM_ERASEBKGND:
				return 1; // 背景在 WM_PAINT 统一绘制，避免闪烁
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				const HDC hdc = BeginPaint(hwnd, &ps);
				const auto S = [dpi = data->dpi](int v) { return ui::dlg_s(v, dpi); };
				RECT rcClient{};
				GetClientRect(hwnd, &rcClient);
				// 顶部浅蓝 → 白垂直渐变，渐变线以下保持纯白（计数文字用不透明白底擦除）
				const int gradBottom = S(INPUT_GRADIENT_BOTTOM);
				RECT rcTop = rcClient;
				rcTop.bottom = gradBottom;
				fill_v_gradient(hdc, rcTop, RGB(0xef, 0xf6, 0xfe), RGB(0xff, 0xff, 0xff));
				RECT rcBottom = rcClient;
				rcBottom.top = gradBottom;
				if (rcBottom.bottom > rcBottom.top)
				{
					FillRect(hdc, &rcBottom, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
				}
				// 输入框白色圆角面板：聚焦时主题色加粗描边，未聚焦为浅灰细描边
				const RECT rcField = input_dlg_field_rect(data->dpi);
				draw_round_rect(hdc, rcField, S(8), RGB(0xff, 0xff, 0xff),
				                data->editFocused ? CLEAN_ACCENT : CLEAN_BORDER,
				                data->editFocused ? 2 : 1);
				// 按钮行上方的细分隔线
				{
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xed, 0xf1, 0xf7));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					MoveToEx(hdc, S(24), S(158), nullptr);
					LineTo(hdc, S(INPUT_DLG_WIDTH - 24), S(158));
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
				}
				EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_CTLCOLOREDIT:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, CLEAN_TEXT);
				return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
			}
			case WM_CTLCOLORSTATIC:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				const HWND hWnd = reinterpret_cast<HWND>(lParam);
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, hWnd == data->hTitle ? CLEAN_TEXT : CLEAN_TEXT_SUB);
				if (hWnd == data->hCount)
				{
					// 计数文字随输入实时变化：用不透明白底（该区域已在纯白底色上）擦除旧文字
					SetBkMode(hdc, OPAQUE);
					SetBkColor(hdc, RGB(0xff, 0xff, 0xff));
					return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
				}
				return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
			}
			case WM_CTLCOLORBTN:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetBkMode(hdc, TRANSPARENT);
				return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
			}
			case WM_SETCURSOR:
				// 悬停在圆角面板（输入框可视范围）上显示文本光标
				if (LOWORD(lParam) == HTCLIENT)
				{
					POINT pt{};
					GetCursorPos(&pt);
					ScreenToClient(hwnd, &pt);
					const RECT rcField = input_dlg_field_rect(data->dpi);
					if (PtInRect(&rcField, pt))
					{
						SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
						return TRUE;
					}
				}
				break;
			case WM_LBUTTONDOWN:
			{
				const POINTS pts = MAKEPOINTS(lParam);
				const POINT pt{pts.x, pts.y};
				const RECT rcField = input_dlg_field_rect(data->dpi);
				if (PtInRect(&rcField, pt))
				{
					SetFocus(data->hEdit);
					return 0;
				}
				break;
			}
			case WM_DRAWITEM:
			{
				const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
				const int id = dis->CtlID;
				if (id == INPUT_OK_ID || id == INPUT_CANCEL_ID)
				{
					const bool primary = (id == INPUT_OK_ID);
					const bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
					const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
					wchar_t buf[32]{};
					GetWindowTextW(dis->hwndItem, buf, 32);
					// 先清底：按钮区域位于纯白背景上，圆角外的残留像素需擦除
					FillRect(dis->hDC, &dis->rcItem, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
					draw_modern_dlg_button(dis->hDC, dis->rcItem, primary, hover, pressed, buf);
					// 键盘焦点环：仅次要按钮绘制（主按钮本身已足够醒目）
					if (!primary && (dis->itemState & ODS_FOCUS) != 0)
					{
						const auto S = [dpi = data->dpi](int v) { return ui::dlg_s(v, dpi); };
						RECT rcFocus = dis->rcItem;
						InflateRect(&rcFocus, -S(4), -S(4));
						HPEN hPen = CreatePen(PS_SOLID, 1, CLEAN_ACCENT_LIGHT);
						HPEN hOldPen = static_cast<HPEN>(SelectObject(dis->hDC, hPen));
						HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(dis->hDC, GetStockObject(NULL_BRUSH)));
						RoundRect(dis->hDC, rcFocus.left, rcFocus.top, rcFocus.right, rcFocus.bottom,
						          S(6) * 2, S(6) * 2);
						SelectObject(dis->hDC, hOldBr);
						SelectObject(dis->hDC, hOldPen);
						DeleteObject(hPen);
					}
					return TRUE;
				}
				return FALSE;
			}
			case WM_COMMAND:
			{
				const int id = LOWORD(wParam);
				const int code = HIWORD(wParam);
				if (id == INPUT_EDIT_ID)
				{
					if (code == EN_CHANGE)
					{
						input_dlg_update_count(*data);
					}
					else if (code == EN_SETFOCUS || code == EN_KILLFOCUS)
					{
						data->editFocused = (code == EN_SETFOCUS);
						const RECT rcField = input_dlg_field_rect(data->dpi);
						InvalidateRect(hwnd, &rcField, FALSE);
					}
					return 0;
				}
				if (id == INPUT_OK_ID)
				{
					// 必须在销毁窗口前读取输入框文本，否则 hEdit 已失效
					wchar_t buf[512]{};
					GetWindowTextW(data->hEdit, buf, 512);
					data->result = buf;
					data->cancelled = false;
					DestroyWindow(hwnd);
					return 0;
				}
				if (id == INPUT_CANCEL_ID)
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
					data->hFontTitle = nullptr;
				}
				if (data->hFontBody)
				{
					DeleteObject(data->hFontBody);
					data->hFontBody = nullptr;
				}
				if (data->hFontSmall)
				{
					DeleteObject(data->hFontSmall);
					data->hFontSmall = nullptr;
				}
				if (data->hFontHint)
				{
					DeleteObject(data->hFontHint);
					data->hFontHint = nullptr;
				}
				data->done = true;
				return 0;
			default:
				break;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		std::optional<std::wstring> show_input_dialog(HWND owner, std::wstring_view title, std::wstring_view initial)
		{
			static const bool classRegistered = []()
			{
				WNDCLASSEXW wc = {sizeof(wc)};
				wc.lpfnWndProc = input_dlg_proc;
				wc.hInstance = GetModuleHandleW(nullptr);
				wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				wc.hbrBackground = nullptr; // 背景全部自绘，避免创建/改变尺寸时闪一帧系统底色
				wc.lpszClassName = INPUT_DLG_CLASS;
				return RegisterClassExW(&wc) != 0;
			}();
			(void)classRegistered;

			InputDialogData data;
			data.title = title;
			data.initial = initial;

			const SIZE clientSize = ui::dlg_client_size(owner, INPUT_DLG_WIDTH, INPUT_DLG_HEIGHT);
			const HWND hDlg = CreateWindowExW(0, INPUT_DLG_CLASS, data.title.c_str(),
			                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
			                                  0, 0, clientSize.cx, clientSize.cy, owner, nullptr,
			                                  GetModuleHandleW(nullptr), &data);
			if (hDlg == nullptr)
			{
				return std::nullopt;
			}
			ui::dlg_fit_client(hDlg, clientSize);
			ui::dlg_center_on(hDlg, owner);
			ShowWindow(hDlg, SW_SHOW);
			SetFocus(data.hEdit);
			SendMessageW(data.hEdit, EM_SETSEL, 0, static_cast<LPARAM>(-1));

			if (owner != nullptr && IsWindow(owner))
			{
				EnableWindow(owner, FALSE);
			}

			MSG msg{};
			while (!data.done && GetMessageW(&msg, nullptr, 0, 0) > 0)
			{
				if (IsDialogMessageW(hDlg, &msg))
				{
					continue;
				}
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			if (owner != nullptr && IsWindow(owner))
			{
				EnableWindow(owner, TRUE);
				SetForegroundWindow(owner);
			}

			if (data.cancelled || data.result.empty())
			{
				return std::nullopt;
			}
			return data.result;
		}
	}

	// ---- 激活码输入对话框（现代化渐变风格）----
	namespace
	{
		constexpr wchar_t ACTIVATE_DLG_CLASS[] = L"eBoxActivateDialog";
		constexpr int ACT_EDIT_ID = 3001;
		constexpr int ACT_OK_ID = 3002;
		constexpr int ACT_CANCEL_ID = 3003;
		constexpr int ACT_BUY_ID = 3007;
		constexpr int ACT_DLG_WIDTH = 480;
		constexpr int ACT_DLG_HEIGHT = 300;
		constexpr UINT ACT_WM_DONE = WM_APP + 0x32; // 后台激活完成（线程消息，结果经 ActShared 交接，消息不携带指针）

		// 后台激活共享结果：工作线程与 UI 交接数据，消息本身不携带指针，杜绝悬垂/泄漏
		struct ActShared
		{
			bool ok{false};
			std::wstring reason; // 失败原因（成功为空）
			std::atomic_bool ready{false};
		};

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
			HBRUSH hHintBg{nullptr};    // 提示行不透明底刷：切换提示词时先清底再画新文字（互斥，防旧文字残留重叠）
			bool errorMode{false};      // false=灰色提示词 true=红色错误提示（互斥显示）
			bool done{false};
			bool activated{false};
			bool activating{false};     // 后台激活校验进行中（防重入）
			bool agreed{false};         // 是否已勾选同意《用户协议》（未勾选不可激活）
			bool boxHover{false};       // 鼠标悬停在同意勾选框上
			bool linkUAHover{false};    // 鼠标悬停在《用户协议》链接上
			bool linkPAHover{false};    // 鼠标悬停在《隐私协议》链接上
			bool mouseTrack{false};     // 已调用 TrackMouseEvent（用于收到 WM_MOUSELEAVE）
			RECT rcAgreeBox{};          // 同意勾选框命中区域（WM_CREATE 时按字体计算缓存）
			RECT rcAgreeUA{};           // 用户协议链接命中区域
			RECT rcAgreeConj{};         // 连词「和」显示区域
			RECT rcAgreePA{};           // 隐私协议链接命中区域
			int dpi{96};                // 窗口 DPI（布局与自绘坐标按此缩放）
			std::shared_ptr<ActShared> actShared; // 与工作线程共享的激活结果块
		};

		// 默认提示词（灰字，贴近输入框下方，自动换行）
		constexpr std::wstring_view ACT_HINT_TEXT{
			L"激活码由作者签发 · 绑定码首次激活自动绑定本机 · 到期后可在界面内续期"
		};

		// 勾选同意《用户协议》行的布局（提示行与按钮行之间的空白带内）
		constexpr int AGREE_BOX_LEFT = 40;
		constexpr int AGREE_BOX_TOP  = 194;
		constexpr int AGREE_BOX_SIZE = 18;
		constexpr wchar_t AGREE_LABEL[] = L"我已阅读并同意";
		constexpr wchar_t AGREE_LINK_UA[] = L"《eBox 用户协议》";
		constexpr wchar_t AGREE_LINK_PA[] = L"《eBox 隐私协议》";
		constexpr wchar_t AGREE_CONJ[]    = L"和";

		// 计算同意勾选框、用户协议链接、连词「和」、隐私协议链接的命中区域（随 DPI 计算文字宽度）
		void activate_agree_rects(HWND hwnd, HFONT hFont, int dpi, RECT& box, RECT& linkUA, RECT& conj, RECT& linkPA)
		{
			const auto S = [dpi](int v) { return ui::dlg_s(v, dpi); };
			box = RECT{S(AGREE_BOX_LEFT), S(AGREE_BOX_TOP),
			           S(AGREE_BOX_LEFT + AGREE_BOX_SIZE), S(AGREE_BOX_TOP + AGREE_BOX_SIZE)};
			HDC hdc = GetDC(hwnd);
			HFONT hOld = static_cast<HFONT>(SelectObject(hdc, hFont));
			SIZE lab{}; GetTextExtentPoint32W(hdc, AGREE_LABEL, static_cast<int>(wcslen(AGREE_LABEL)), &lab);
			SIZE ua{}; GetTextExtentPoint32W(hdc, AGREE_LINK_UA, static_cast<int>(wcslen(AGREE_LINK_UA)), &ua);
			SIZE cj{}; GetTextExtentPoint32W(hdc, AGREE_CONJ, static_cast<int>(wcslen(AGREE_CONJ)), &cj);
			SIZE pa{}; GetTextExtentPoint32W(hdc, AGREE_LINK_PA, static_cast<int>(wcslen(AGREE_LINK_PA)), &pa);
			SelectObject(hdc, hOld);
			ReleaseDC(hwnd, hdc);

			const int top = box.top + S(2);
			const int labX = box.right + S(10);
			const int uaX = labX + lab.cx + S(6);
			const int cjX = uaX + ua.cx + S(5);
			const int paX = cjX + cj.cx + S(5);
			linkUA = RECT{uaX, top, uaX + ua.cx, top + ua.cy};
			conj   = RECT{cjX, top, cjX + cj.cx, top + cj.cy};
			linkPA = RECT{paX, top, paX + pa.cx, top + pa.cy};
		}

		// 完整用户协议正文（富文本弹窗展示，`# ` 开头的行为章节标题）
		constexpr wchar_t AGREEMENT_TEXT[] = LR"(
# eBox 用户协议

更新日期：2026-09-03 · 版本：v1.1

重要提示：请您在使用前仔细阅读并充分理解本协议全部内容。本协议中以红色标注的内容为重要条款，请务必重点关注；如您对任何条款有异议，可在同意前通过本软件官方邮箱（xianhuquwang@163.com）向开发者提出。您首次启动并点击激活、或继续使用本软件，即视为您已阅读、理解并同意本协议。若您不同意本协议，请立即停止使用并卸载本软件。

# 一、总则

1.1 本软件（即 eBox）是由开发者开发的 Windows 平台多实例运行/环境隔离工具。
1.2 本软件定位为：多实例环境隔离与并行，用于在同一台设备上创建多个相互独立的运行环境，便于多账号、多场景的规范使用。
1.3 您在使用本软件时，应遵守中华人民共和国法律法规、本协议以及相关平台（如微信、企业微信、QQ 等）的使用条款与平台规则。
1.4 本软件由开发者依法运营。如您对本协议或本软件有任何疑问、投诉或建议，可通过本软件官方邮箱（xianhuquwang@163.com）与我们联系，我们将在合理期限内予以答复；与本协议相关的通知、修订，均通过本软件内页面或官方渠道公布。

# 二、软件许可与授权

2.1 本软件为免费软件，仅供个人非商业目的使用。未经开发者书面许可，任何单位或个人不得用于商业用途。
2.2 本软件通过激活码授权，分为单机绑定码与通用码等类型，具体以激活页面说明为准。
2.3 请妥善保管激活码，不得转卖、转借、共享或用于其他违反本协议的目的。
2.4 开发者有权根据运营需要调整授权方式、功能范围与版本策略，并有权调整或终止部分或全部服务。

# 三、软件功能说明

3.1 本软件通过内存加载、接口转发与路径重定向等方式，为同一程序创建多个相互独立、互不干扰的运行环境。
3.2 本软件仅提供多实例环境的隔离与并行，不提供、也不依赖任何“外挂/作弊/风控规避”功能。
3.3 本软件只能做简单隔离，不阻止环境内进程访问环境外资源，也不阻止环境外进程感知环境内进程，请知悉该技术边界。

# 四、用户行为规范与禁止事项

4.1 您承诺遵守法律法规、相关平台使用条款，不利用本软件从事违法犯罪活动，不损害开发者及他人合法权益。
4.2 明令禁止将本软件用于：诈骗、传销、刷单、洗钱、非法集资、赌博、侵犯他人隐私与个人信息、侵犯知识产权、发布传播违法信息；批量骚扰、规避他人或平台安全机制；批量群发、定时群发、非官方自动回复、数据抓取/监控/保存、批量拉群、批量自动加好友等违反平台规则的行为。
4.3 因您违反本条约定的行为所产生的一切后果（包括账号受限、封禁、法律纠纷、行政处罚等），由您自行承担。
4.4 您应确保通过本软件启动、运行的任何程序、应用均已取得合法授权；因您运行侵权、盗版或违法程序/应用所引发的纠纷、诉讼及损失，由您自行承担，与开发者无关。
4.5 本软件仅供具备完全民事行为能力的成年人使用；未成年人须在监护人同意并在监护人指导下使用，未成年人违规使用本软件产生的一切后果由其监护人承担。

# 五、第三方平台多开的风控风险

5.1 您知悉：微信、企业微信等平台对多开/第三方工具/虚拟机/模拟器等运行方式设有识别与风控机制。
5.2 根据企业微信官方《用户外挂自查指南》，“多开企业微信，或将多个企微账号聚合在一台设备上运营”被列为外挂行为，官方明确“企业使用外挂会导致账号被封禁”。
5.3 因此，使用第三方工具多开微信、企业微信等平台，存在账号受限、功能异常或封禁的风险，该风险由您自行评估并承担，开发者无法、也不承诺规避该等风控。
5.4 对于明确不允许多实例运行（如禁止多开、禁止多账号聚合运营、禁止使用第三方工具）的平台、程序或应用，建议您严格遵守其规则与条款，以避免造成账号受限、封禁或其他损失。若您仍执意使用本软件多开此类平台，相关行为属于用户个人行为，是否使用以及如何使用由您自主决定，由此产生的一切后果由您自行承担，与开发者无关。
5.5 您理解并同意：如因您多开某程序、平台、应用，而引发相关程序、平台、公司或官方对您（使用者）的质疑、投诉、追责、账号限制或处罚等，相关责任与争议均由您本人（使用者）承担；相关平台、程序、公司或官方如有任何问题，应直接与使用者沟通或追责，与开发者无关。开发者仅提供多实例环境隔离功能，不对您与任何第三方之间的争议、纠纷或损失承担任何责任。
5.6 开发者不提供、不协助、不引导任何规避平台风控、绕过平台安全机制的功能或方法；本软件仅提供多实例环境的隔离与并行，对上述用途不做任何承诺、协助或担保。

# 六、数据与隐私

6.1 本软件不收集、不分析、不存储、不上传您的任何账号、聊天记录或企业数据；软件仅在本地运行，激活采用离线验证方式。
6.2 软件可能生成必要的本地配置与日志，用于自身运行与问题排查，这些数据存储于您本机的环境目录中，由您自行管理。
6.3 请妥善保管您的账号、密码、激活码等信息，因保管不善导致的损失由您自行承担。

# 七、免责声明

7.1 本软件按“现状”与“可用”原则提供，不保证无错误、不中断、完全兼容所有软硬件环境。
7.2 因您违规使用或违反本协议及法律法规、第三方平台风控封禁、不可抗力或第三方服务中断、操作不当或使用盗版非官方渠道等导致损失的，开发者不承担责任。
7.3 在法律允许范围内，开发者不对间接、偶然、特殊、惩罚性或后果性损害承担责任。在适用法律允许的最大范围内，开发者对您承担的累计赔偿责任总额，不超过您为获得本软件授权所实际支付的费用；因本软件为免费提供，故该赔偿责任上限为零。本条不适用于依法不得免除或限制的责任（包括因开发者故意或重大过失造成的损失）。

# 八、知识产权

8.1 本软件及相关代码、文档、图标、界面、商标的知识产权归开发者或其权利人所有。
8.2 未经书面许可，不得反向工程、反编译、复制、修改、出售、出租、分发或以其他方式侵权。

# 九、协议变更

9.1 开发者可能适时更新、升级本软件；更新后的功能、界面、授权方式以实际版本为准。
9.2 开发者有权在法律允许范围内修订本协议，修订后自公布之日起生效；您继续使用即视为接受修订后的协议。

# 十、协议的终止

10.1 若您违反本协议任何条款，开发者有权采取警告、限制或暂停授权、终止服务、追究法律责任等措施。
10.2 您有权随时停止使用并卸载本软件，以终止本协议。

# 十一、法律适用与争议解决

11.1 本协议的订立、执行与解释适用中华人民共和国法律。
11.2 因本协议引发的争议，双方应友好协商；协商不成，可向开发者所在地有管辖权的人民法院提起诉讼。

# 十二、其他

12.1 本协议部分条款无效或不可执行的，不影响其他条款的效力。
12.2 继续使用本软件，即表示您已阅读、理解并同意本协议的全部内容。若您不同意，请立即停止使用并卸载本软件。

# 十三、第三方组件与开源许可

13.1 本软件可能包含第三方开源组件，其版权归其原作者所有，相关组件的使用遵循其各自的开源许可证（如 Microsoft Public License 等）。
13.2 本软件使用的第三方组件清单及许可证信息，可通过本软件官方邮箱（xianhuquwang@163.com）查询；开发者依法尊重并保留第三方之知识产权。
)";

		// 完整隐私协议正文（富文本弹窗展示，`# ` 开头的行为章节标题）
		constexpr wchar_t AGREEMENT_TEXT_PRIVACY[] = LR"(
# eBox 隐私协议

更新日期：2026-09-03 · 版本：v1.0

重要提示：本隐私协议是《eBox 用户协议》的组成部分。我们非常重视您的个人信息与隐私保护，请在使用本软件前仔细阅读并充分理解本协议。

# 一、我们收集的信息

1.1 本软件不收集、不分析、不存储、不上传您的任何账号、密码、聊天记录、联系人、企业数据或个人隐私信息。
1.2 本软件仅在您的本地设备上运行；激活采用离线验证方式，您输入的激活码仅用于本地校验，不会发送到任何服务器。
1.3 我们不会申请与软件功能无关的权限，也不会在后台采集您的设备信息、位置信息、通讯录、相册等敏感数据。

# 二、信息的使用

2.1 由于软件本身不收集任何个人信息，我们不会以任何方式对外提供、出售或共享您的个人信息。
2.2 软件可能在本机生成必要的运行配置与日志，用于自身运行与问题排查；这些数据只保存在软件环境目录中，由您自行管理与删除。

# 三、本地数据的存储与安全

3.1 您在软件运行过程中产生的多开环境、配置、缓存等数据，均存储于您本机的环境目录（如 C:\eBoxData\Env\）内，由您自行负责保管。
3.2 请您妥善保管您的账号、密码、激活码及本地环境目录，避免泄露或因保管不善导致的损失；因您自身原因造成的损失由您自行承担。

# 四、第三方平台与第三方服务

4.1 您在使用本软件多开微信、企业微信、QQ 等第三方平台时，相关账号数据由您与对应平台之间直接交互，本软件不接触、不存储此类数据。
4.2 请您遵守对应平台的隐私政策与使用条款，因您违规操作导致的账号数据风险与损失由您自行承担。

# 五、未成年人保护

5.1 本软件仅供具备完全民事行为能力的成年人使用。若您为未成年人，请在监护人指导下使用，并事先取得监护人的同意。

# 六、协议的更新与联系

6.1 我们可能适时更新本隐私协议，更新后自公布之日起生效，您继续使用本软件即视为接受更新后的协议。
6.2 如您对本隐私协议或个人信息保护有任何疑问，可通过本软件官方邮箱（xianhuquwang@163.com）与我们联系。
)";

		// ---- 用户协议弹窗（RichEdit 富文本排版，可滚动）----
		constexpr wchar_t AGREEMENT_DLG_CLASS[] = L"eBoxAgreementDialog";
		constexpr int AGREEMENT_CLOSE_ID = 3009;
		constexpr int AGREEMENT_DLG_WIDTH = 560;
		constexpr int AGREEMENT_DLG_HEIGHT = 560;

		// 协议正文自绘区域（相对客户区）与自绘滚动条布局
		constexpr int AGREE_BODY_LEFT   = 28;
		constexpr int AGREE_BODY_TOP    = 96;
		constexpr int AGREE_BODY_BOTTOM = AGREEMENT_DLG_HEIGHT - 60;
		constexpr int AGREE_SB_WIDTH    = 8;                                // 滚动条宽度
		constexpr int AGREE_SB_LEFT     = AGREEMENT_DLG_WIDTH - 30;         // 滚动条左缘
		constexpr int AGREE_BODY_RIGHT  = AGREE_SB_LEFT - 14;               // 正文右边界（避开滚动条）
		constexpr int AGREE_SCROLL_STEP = 24;                               // 滚轮/按键一次滚动像素
		constexpr COLORREF AGREE_TEXT_COLOR      = RGB(0x33, 0x3a, 0x45);
		constexpr COLORREF AGREE_TITLE_COLOR     = RGB(0x1f, 0x2a, 0x37);
		constexpr COLORREF AGREE_RED_COLOR       = RGB(0xd9, 0x2b, 0x2b);   // 关键信息标红

		// 需要标红的关键词/短语（命中即整段标红，其余文字保持默认颜色）
		constexpr const wchar_t* AGREE_RED_KEYWORDS[] = {
			L"封禁", L"封号", L"外挂", L"违法犯罪", L"违规",
			L"由您自行承担", L"不承担任何责任", L"不承担责任",
			L"账号受限", L"该风险由您自行评估并承担",
		};

		// 一段文字中的一段切片：red 表示该段需要标红
		struct AgreementSeg
		{
			std::wstring_view text;
			bool red{false};
		};

		// 按关键短语拆分一段文字，返回若干切片（命中关键词的切片标记为红色）
		std::vector<AgreementSeg> agreement_split_keywords(const std::wstring& text)
		{
			std::vector<AgreementSeg> segs;
			std::size_t pos = 0;
			while (pos < text.size())
			{
				std::size_t bestPos = std::wstring::npos;
				std::size_t bestLen = 0;
				for (const wchar_t* kw : AGREE_RED_KEYWORDS)
				{
					const std::size_t len = std::wcslen(kw);
					const std::size_t found = text.find(kw, pos);
					if (found != std::wstring::npos)
					{
						if (bestPos == std::wstring::npos || found < bestPos ||
						    (found == bestPos && len > bestLen))
						{
							bestPos = found;
							bestLen = len;
						}
					}
				}
				if (bestPos == std::wstring::npos)
				{
					if (pos < text.size())
					{
						segs.push_back({std::wstring_view(text).substr(pos), false});
					}
					break;
				}
				if (bestPos > pos)
				{
					segs.push_back({std::wstring_view(text).substr(pos, bestPos - pos), false});
				}
				segs.push_back({std::wstring_view(text).substr(bestPos, bestLen), true});
				pos = bestPos + bestLen;
			}
			return segs;
		}

		// 逐行渲染块：heading 标记小节标题（需加粗展示），text 为该行的文本
		struct AgreementLine
		{
			std::wstring text;
			bool heading{false};
		};

		struct AgreementDialogData
		{
			HWND hwnd{nullptr};
			HWND hTitle{nullptr};
			HWND hSub{nullptr};
			HWND hClose{nullptr};
			HFONT hFontTitle{nullptr};
			HFONT hFontHead{nullptr};
			HFONT hFontBody{nullptr};
			HFONT hFontSub{nullptr};         // 副标题（更小字号）
			int bodyLineHeight{0};           // 正文字体的单行高度（用于关键词标红的逐行排版）
			int dpi{96};                     // 窗口 DPI（布局与自绘坐标按此缩放）
			bool* pAgreed{nullptr};        // 同意后回写给激活弹窗的勾选状态
			const wchar_t* title{nullptr}; // 弹窗标题（用户协议 / 隐私协议）
			const wchar_t* text{nullptr};  // 要展示的富文本正文
			std::vector<AgreementLine> lines; // 逐行渲染块（标题加粗、正文自动换行）
			int contentHeight{0};          // 完整文本高度（像素，含换行）
			int scrollY{0};                // 当前滚动偏移（像素）
			bool scrollDragging{false};    // 是否正在拖动自绘滚动条滑块
			int dragGrabY{0};              // 按下时鼠标相对滑块顶部的偏差（像素）
			bool done{false};
		};

		// 将协议正文按“逻辑行”拆分成渲染块：”# “开头为章节标题（加粗），空行保留作为段落间距
		std::vector<AgreementLine> build_agreement_lines(const wchar_t* text)
		{
			std::vector<AgreementLine> out;
			const wchar_t* p = text;
			while (*p)
			{
				const wchar_t* e = p;
				while (*e && *e != L'\n')
				{
					++e;
				}
				std::wstring line(p, e);
				if (!line.empty() && line.back() == L'\r')
				{
					line.pop_back();
				}
				AgreementLine blk;
				if (line.rfind(L"# ", 0) == 0)
				{
					line.erase(0, 2);
					blk.heading = true;
				}
				blk.text = std::move(line);
				out.push_back(std::move(blk));
				p = (*e == L'\n') ? e + 1 : e;
			}
			return out;
		}

		constexpr int AGREE_PARAGRAPH_GAP = 10;  // 空行（段落间距）高度
		constexpr int AGREE_LINE_GAP = 3;        // 普通行之间的行距
		constexpr int AGREE_HEAD_SPACE = 9;      // 章节标题后的额外间距

		// 逐行排版一段正文（含关键词标红）：按字符宽度换行（契合中文逐字换行）。
		// draw == true 时把文字绘制出来，否则仅测量。返回本次排版消耗的总高度。
		int agreement_layout_body(HDC hdc, const AgreementDialogData* data,
		                          const std::wstring& text, int startY, bool draw)
		{
			const auto S = [dpi = data->dpi](int v) { return ui::dlg_s(v, dpi); };
			const int lineH = data->bodyLineHeight;
			const int destX = S(AGREE_BODY_LEFT);
			const int bodyW = S(AGREE_BODY_RIGHT);
			const std::vector<AgreementSeg> segs = agreement_split_keywords(text);
			int x = destX;
			int curY = startY;
			for (const auto& seg : segs)
			{
				SetTextColor(hdc, seg.red ? AGREE_RED_COLOR : AGREE_TEXT_COLOR);
				const wchar_t* run = seg.text.data();
				const int runLen = static_cast<int>(seg.text.size());
				int offset = 0;
				while (offset < runLen)
				{
					int fit = 0;
					const int maxChars = runLen - offset;
					for (int c = 1; c <= maxChars; ++c)
					{
						SIZE sz{};
						GetTextExtentPoint32W(hdc, run + offset, c, &sz);
						if (x + sz.cx <= bodyW)
						{
							fit = c;
						}
						else
						{
							break;
						}
					}
					if (fit == 0)
					{
						if (x > destX)
						{
							x = destX;
							curY += lineH;
							continue;
						}
						fit = 1; // 行首至少落一个字符，超宽部分交给裁剪处理
					}
					SIZE sz{};
					GetTextExtentPoint32W(hdc, run + offset, fit, &sz);
					if (draw)
					{
						RECT cr{x, curY, x + sz.cx, curY + lineH};
						ExtTextOutW(hdc, x, curY, ETO_CLIPPED, &cr, run + offset, fit, nullptr);
					}
					x += sz.cx;
					offset += fit;
					if (x >= bodyW && offset < runLen)
					{
						x = destX;
						curY += lineH;
					}
				}
			}
			return curY + lineH - startY;
		}

		// 逐块渲染/测量协议内容；startY 为起始 Y，draw 为 true 时绘制、false 时仅测高。
		// 返回本次内容占据的总高度（不包含 startY）。
		int agreement_render(HDC hdc, const AgreementDialogData* data,
		                     const std::vector<AgreementLine>& lines, int startY, bool draw)
		{
			const auto S = [dpi = data->dpi](int v) { return ui::dlg_s(v, dpi); };
			const int bodyW = S(AGREE_BODY_RIGHT) - S(AGREE_BODY_LEFT);
			int y = startY;
			for (const auto& blk : lines)
			{
				if (blk.text.empty())
				{
					y += S(AGREE_PARAGRAPH_GAP);
					continue;
				}
				if (blk.heading)
				{
					HFONT hOld = static_cast<HFONT>(SelectObject(hdc, data->hFontHead));
					RECT measure{0, 0, bodyW, 0};
					DrawTextW(hdc, blk.text.c_str(), -1, &measure,
					          DT_NOPREFIX | DT_LEFT | DT_SINGLELINE | DT_CALCRECT);
					const int h = measure.bottom;
					if (draw)
					{
						SetTextColor(hdc, AGREE_TITLE_COLOR);
						RECT dr{S(AGREE_BODY_LEFT), y, S(AGREE_BODY_RIGHT), y + h};
						DrawTextW(hdc, blk.text.c_str(), -1, &dr,
						          DT_NOPREFIX | DT_LEFT | DT_SINGLELINE);
					}
					SelectObject(hdc, hOld);
					y += h + S(AGREE_HEAD_SPACE);
				}
				else
				{
					HFONT hOld = static_cast<HFONT>(SelectObject(hdc, data->hFontBody));
					y += agreement_layout_body(hdc, data, blk.text, y, draw) + S(AGREE_LINE_GAP);
					SelectObject(hdc, hOld);
				}
			}
			return y - startY;
		}

		// 在裁剪区域内自绘协议文本，并按滚动偏移上下移动
		void agreement_draw_text(HDC hdc, const AgreementDialogData* data)
		{
			const auto S = [dpi = data->dpi](int v) { return ui::dlg_s(v, dpi); };
			const int save = SaveDC(hdc);
			IntersectClipRect(hdc, S(AGREE_BODY_LEFT), S(AGREE_BODY_TOP), S(AGREE_BODY_RIGHT), S(AGREE_BODY_BOTTOM));
			SetBkMode(hdc, TRANSPARENT);
			agreement_render(hdc, data, data->lines, S(AGREE_BODY_TOP) - data->scrollY, true);
			RestoreDC(hdc, save);
		}

		// 按窗口 DPI 重建协议弹窗字体（首次创建与跨屏 DPI 变化共用）
		void agreement_dlg_create_fonts(AgreementDialogData& data)
		{
			if (data.hFontTitle)
			{
				DeleteObject(data.hFontTitle);
				data.hFontTitle = nullptr;
			}
			if (data.hFontHead)
			{
				DeleteObject(data.hFontHead);
				data.hFontHead = nullptr;
			}
			if (data.hFontBody)
			{
				DeleteObject(data.hFontBody);
				data.hFontBody = nullptr;
			}
			if (data.hFontSub)
			{
				DeleteObject(data.hFontSub);
				data.hFontSub = nullptr;
			}

			const int titleSize = -MulDiv(17, data.dpi, 72);
			const int headSize = -MulDiv(14, data.dpi, 72);
			const int bodySize = -MulDiv(12, data.dpi, 72);
			const int subSize = -MulDiv(11, data.dpi, 72);
			data.hFontTitle = CreateFontW(titleSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
			data.hFontHead = CreateFontW(headSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
			data.hFontBody = CreateFontW(bodySize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
			data.hFontSub = CreateFontW(subSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");

			// 取正文字体的单行高度（逐行排版标红时使用，保证内容高度测量与绘制一致）
			HDC hMeasure = GetDC(data.hwnd);
			if (hMeasure)
			{
				HFONT hOldBody = static_cast<HFONT>(SelectObject(hMeasure, data.hFontBody));
				TEXTMETRICW tm{};
				GetTextMetricsW(hMeasure, &tm);
				data.bodyLineHeight = tm.tmHeight;
				SelectObject(hMeasure, hOldBody);
				ReleaseDC(data.hwnd, hMeasure);
			}

			if (data.hTitle)
			{
				SendMessageW(data.hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontTitle), TRUE);
			}
			if (data.hSub)
			{
				SendMessageW(data.hSub, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontSub), TRUE);
			}
			if (data.hClose)
			{
				SendMessageW(data.hClose, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontBody), TRUE);
			}
		}

		// 按窗口 DPI 摆放子控件（首次创建与跨屏 DPI 变化共用）
		void agreement_dlg_layout(AgreementDialogData& data)
		{
			const auto S = [dpi = data.dpi](int v) { return ui::dlg_s(v, dpi); };
			if (data.hTitle)
			{
				SetWindowPos(data.hTitle, nullptr, S(28), S(22), S(AGREEMENT_DLG_WIDTH - 56), S(30),
				             SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if (data.hSub)
			{
				SetWindowPos(data.hSub, nullptr, S(28), S(56), S(AGREEMENT_DLG_WIDTH - 56), S(20),
				             SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if (data.hClose)
			{
				SetWindowPos(data.hClose, nullptr,
				             S(AGREEMENT_DLG_WIDTH - 172), S(AGREEMENT_DLG_HEIGHT - 46), S(148), S(36),
				             SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}

		// 重建字体 → 重测正文高度 → 重排子控件 → 夹紧滚动偏移
		void agreement_dlg_recalc(AgreementDialogData& data)
		{
			agreement_dlg_create_fonts(data);
			if (data.text)
			{
				data.lines = build_agreement_lines(data.text);
			}
			HDC hMeasure = GetDC(data.hwnd);
			if (hMeasure)
			{
				data.contentHeight = agreement_render(hMeasure, &data, data.lines, 0, false);
				ReleaseDC(data.hwnd, hMeasure);
			}
			agreement_dlg_layout(data);

			const int bodyH = ui::dlg_s(AGREE_BODY_BOTTOM - AGREE_BODY_TOP, data.dpi);
			const int maxScroll = data.contentHeight > bodyH ? data.contentHeight - bodyH : 0;
			if (data.scrollY > maxScroll)
			{
				data.scrollY = maxScroll;
			}
			if (data.scrollY < 0)
			{
				data.scrollY = 0;
			}
		}

		LRESULT CALLBACK agreement_dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* data = reinterpret_cast<AgreementDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			switch (msg)
			{
			case WM_CREATE:
			{
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
				data = static_cast<AgreementDialogData*>(cs->lpCreateParams);
				data->hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

				const HINSTANCE hInst = GetModuleHandleW(nullptr);
				data->dpi = ui::dlg_dpi(hwnd);

				data->hTitle = CreateWindowExW(0, L"STATIC", data->title ? data->title : L"用户协议",
				                               WS_CHILD | WS_VISIBLE | SS_LEFT,
				                               0, 0, 0, 0, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(3010)),
				                               hInst, nullptr);
				data->hSub = CreateWindowExW(0, L"STATIC", L"请仔细阅读以下内容，同意后勾选“我已阅读并同意”即可继续激活",
				                             WS_CHILD | WS_VISIBLE | SS_LEFT,
				                             0, 0, 0, 0, hwnd,
				                             reinterpret_cast<HMENU>(static_cast<std::intptr_t>(3011)),
				                             hInst, nullptr);

				// 正文：改为自绘滚动文本，完全移除系统 EDIT 控件
				// （多行只读 EDIT 在滚动时因主题/自定义背景刷 + ClearType 会产生严重的重影与文字折叠）
				data->hClose = CreateWindowExW(0, L"BUTTON", L"我已知晓并同意",
				                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                               0, 0, 0, 0, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(AGREEMENT_CLOSE_ID)),
				                               hInst, nullptr);

				agreement_dlg_recalc(*data);
				return 0;
			}
			case WM_DPICHANGED:
			{
				if (data)
				{
					data->dpi = ui::dlg_on_dpi_changed(hwnd, lParam);
					agreement_dlg_recalc(*data);
					InvalidateRect(hwnd, nullptr, TRUE);
				}
				return 0;
			}
			case WM_SIZE:
				// 创建后校正客户区尺寸会触发 WM_SIZE，需按最终客户区重新摆放控件
				if (data && data->hwnd && wParam != SIZE_MINIMIZED)
				{
					agreement_dlg_layout(*data);
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				return 0;
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
			{
				if (!data)
				{
					break;
				}
				const auto S = [dpi = data->dpi](int v) { return ui::dlg_s(v, dpi); };
				PAINTSTRUCT ps{};
				HDC hdc = BeginPaint(hwnd, &ps);
				RECT rc{}; GetClientRect(hwnd, &rc);
				fill_v_gradient(hdc, rc, RGB(0xf7, 0xfa, 0xfe), RGB(0xff, 0xff, 0xff));
				// 顶部主题色条
				HPEN hPen = CreatePen(PS_SOLID, 1, CLEAN_ACCENT);
				HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
				MoveToEx(hdc, 0, 0, nullptr); LineTo(hdc, rc.right, 0);
				// 标题与正文分隔线 + 正文与操作区底部分隔线
				HPEN hLine = CreatePen(PS_SOLID, 1, RGB(0xe8, 0xee, 0xf6));
				HPEN hPrevLine = static_cast<HPEN>(SelectObject(hdc, hLine));
				MoveToEx(hdc, S(24), S(82), nullptr); LineTo(hdc, rc.right - S(24), S(82));
				MoveToEx(hdc, S(24), S(AGREEMENT_DLG_HEIGHT - 52), nullptr);
				LineTo(hdc, rc.right - S(24), S(AGREEMENT_DLG_HEIGHT - 52));
				SelectObject(hdc, hPrevLine); // 还原为 hLine
				SelectObject(hdc, hOldPen);   // 还原为原始画笔
				DeleteObject(hLine);
				DeleteObject(hPen);

				// 自绘协议正文（按滚动偏移渲染，裁剪到正文区域）
				if (!data->lines.empty())
				{
					agreement_draw_text(hdc, data);
				}

				// 自绘滚动条（仅在内容超过可视区时显示）
				const int bodyTop = S(AGREE_BODY_TOP);
				const int bodyBottom = S(AGREE_BODY_BOTTOM);
				const int bodyH = bodyBottom - bodyTop;
				if (data->contentHeight > bodyH)
				{
					const int maxScroll = data->contentHeight - bodyH;
					// 轨道背景
					RECT track{S(AGREE_SB_LEFT), bodyTop, S(AGREE_SB_LEFT + AGREE_SB_WIDTH), bodyBottom};
					draw_round_rect(hdc, track, S(4), RGB(0xec, 0xf1, 0xf8), RGB(0xe4, 0xea, 0xf2), 1);
					// 滑块
					const int trackH = bodyH;
					const int thumbH = std::max(S(30), trackH * bodyH / data->contentHeight);
					const int maxTop = trackH - thumbH;
					int thumbTop = track.top + (maxTop > 0 ? (trackH - thumbH) * data->scrollY / maxScroll : 0);
					if (thumbTop < track.top) thumbTop = track.top;
					RECT thumb{track.left, thumbTop, track.right, thumbTop + thumbH};
					draw_round_rect(hdc, thumb, S(4), RGB(0x9c, 0xb8, 0xd8), RGB(0x9c, 0xb8, 0xd8), 0);
				}
				EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_CTLCOLORSTATIC:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				const HWND hWnd = reinterpret_cast<HWND>(lParam);
				SetBkMode(hdc, TRANSPARENT);
				if (hWnd == data->hSub)
				{
					SetTextColor(hdc, CLEAN_TEXT_SUB);
				}
				else
				{
					SetTextColor(hdc, CLEAN_TEXT);
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
				if (dis->CtlID == AGREEMENT_CLOSE_ID)
				{
					const bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
					const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
					wchar_t buf[64]{};
					GetWindowTextW(dis->hwndItem, buf, 64);
					draw_modern_dlg_button(dis->hDC, dis->rcItem, true, hover, pressed, buf, false);
					return TRUE;
				}
				return FALSE;
			}
			case WM_COMMAND:
				if (LOWORD(wParam) == AGREEMENT_CLOSE_ID)
				{
					// 同意并关闭：回写激活弹窗勾选状态，并让其不显示渲染残留
					if (data->pAgreed)
					{
						*data->pAgreed = true;
					}
					if (data->hwnd)
					{
						const HWND owner = GetParent(data->hwnd);
						if (owner && IsWindow(owner))
						{
							InvalidateRect(owner, nullptr, TRUE);
						}
					}
					DestroyWindow(hwnd);
					return 0;
				}
				return 0;
			case WM_CLOSE:
				DestroyWindow(hwnd);
				return 0;
			case WM_MOUSEWHEEL:
			{
				const int bodyH = ui::dlg_s(AGREE_BODY_BOTTOM - AGREE_BODY_TOP, data->dpi);
				const int maxScroll = data->contentHeight > bodyH ? data->contentHeight - bodyH : 0;
				if (maxScroll > 0)
				{
					const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
					const int value = data->scrollY - delta / WHEEL_DELTA * ui::dlg_s(AGREE_SCROLL_STEP, data->dpi);
					data->scrollY = value < 0 ? 0 : (value > maxScroll ? maxScroll : value);
					InvalidateRect(hwnd, nullptr, TRUE);
				}
				return 0;
			}
			case WM_KEYDOWN:
			{
				const int bodyH = ui::dlg_s(AGREE_BODY_BOTTOM - AGREE_BODY_TOP, data->dpi);
				const int maxScroll = data->contentHeight > bodyH ? data->contentHeight - bodyH : 0;
				if (maxScroll > 0)
				{
					int v = data->scrollY;
					switch (LOWORD(wParam))
					{
					case VK_UP:
						v -= ui::dlg_s(AGREE_SCROLL_STEP, data->dpi);
						break;
					case VK_DOWN:
						v += ui::dlg_s(AGREE_SCROLL_STEP, data->dpi);
						break;
					case VK_PRIOR:
						v -= bodyH;
						break;
					case VK_NEXT:
						v += bodyH;
						break;
					case VK_HOME:
						v = 0;
						break;
					case VK_END:
						v = maxScroll;
						break;
					default:
						return 0;
					}
					data->scrollY = v < 0 ? 0 : (v > maxScroll ? maxScroll : v);
					InvalidateRect(hwnd, nullptr, TRUE);
				}
				return 0;
			}
			case WM_LBUTTONDOWN:
			{
				const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
				const int bodyTop = ui::dlg_s(AGREE_BODY_TOP, data->dpi);
				const int bodyBottom = ui::dlg_s(AGREE_BODY_BOTTOM, data->dpi);
				const int sbLeft = ui::dlg_s(AGREE_SB_LEFT, data->dpi);
				const int sbRight = ui::dlg_s(AGREE_SB_LEFT + AGREE_SB_WIDTH, data->dpi);
				const int bodyH = bodyBottom - bodyTop;
				const int maxScroll = data->contentHeight > bodyH ? data->contentHeight - bodyH : 0;
				if (maxScroll > 0 && pt.x >= sbLeft && pt.x <= sbRight &&
				    pt.y >= bodyTop && pt.y <= bodyBottom)
				{
					const int trackH = bodyH;
					const int thumbH = std::max(ui::dlg_s(30, data->dpi), trackH * bodyH / data->contentHeight);
					const int maxTop = trackH - thumbH;
					int curTop = bodyTop + (maxTop > 0 ? (trackH - thumbH) * data->scrollY / maxScroll : 0);
					if (curTop < bodyTop) curTop = bodyTop;
					if (pt.y >= curTop && pt.y <= curTop + thumbH)
					{
						// 按住滑块拖动
						data->scrollDragging = true;
						data->dragGrabY = pt.y - curTop;
						SetCapture(hwnd);
					}
					else if (pt.y < curTop)
					{
						// 点击滑块上方 → 向上翻页
						data->scrollY = data->scrollY - bodyH < 0 ? 0 : data->scrollY - bodyH;
						InvalidateRect(hwnd, nullptr, TRUE);
					}
					else
					{
						// 点击滑块下方 → 向下翻页
						data->scrollY = data->scrollY + bodyH > maxScroll ? maxScroll : data->scrollY + bodyH;
						InvalidateRect(hwnd, nullptr, TRUE);
					}
					return 0;
				}
				break;
			}
			case WM_MOUSEMOVE:
			{
				if (data->scrollDragging)
				{
					const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					const int bodyTop = ui::dlg_s(AGREE_BODY_TOP, data->dpi);
					const int bodyH = ui::dlg_s(AGREE_BODY_BOTTOM - AGREE_BODY_TOP, data->dpi);
					const int maxScroll = data->contentHeight > bodyH ? data->contentHeight - bodyH : 0;
					const int trackH = bodyH;
					const int thumbH = std::max(ui::dlg_s(30, data->dpi), trackH * bodyH / data->contentHeight);
					const int maxTop = trackH - thumbH;
					if (maxTop > 0 && maxScroll > 0)
					{
						int newThumbTop = pt.y - data->dragGrabY;
						if (newThumbTop < bodyTop) newThumbTop = bodyTop;
						if (newThumbTop > bodyTop + maxTop) newThumbTop = bodyTop + maxTop;
						data->scrollY = (newThumbTop - bodyTop) * maxScroll / maxTop;
						InvalidateRect(hwnd, nullptr, TRUE);
					}
				}
				return 0;
			}
			case WM_LBUTTONUP:
				if (data->scrollDragging)
				{
					data->scrollDragging = false;
					if (GetCapture() == hwnd)
					{
						ReleaseCapture();
					}
				}
				return 0;
			case WM_CAPTURECHANGED:
				data->scrollDragging = false;
				return 0;
			case WM_DESTROY:
				if (data->hFontTitle)
				{
					DeleteObject(data->hFontTitle);
				}
				if (data->hFontHead)
				{
					DeleteObject(data->hFontHead);
				}
				if (data->hFontBody)
				{
					DeleteObject(data->hFontBody);
				}
				if (data->hFontSub)
				{
					DeleteObject(data->hFontSub);
				}
				data->done = true;
				return 0;
			default:
				break;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		void show_agreement_dialog(HWND hOwner, bool* pAgreed, const wchar_t* title, const wchar_t* text)
		{
			static const bool classRegistered = []()
			{
				WNDCLASSEXW wc = {sizeof(wc)};
				wc.lpfnWndProc = agreement_dlg_proc;
				wc.hInstance = GetModuleHandleW(nullptr);
				wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
				wc.style = 0;   // 不使用 CS_DROPSHADOW，避免弹窗右侧/底部出现生硬的边框阴影
				wc.lpszClassName = AGREEMENT_DLG_CLASS;
				return RegisterClassExW(&wc) != 0;
			}();
			(void)classRegistered;

			AgreementDialogData data;
			data.pAgreed = pAgreed;
			data.title = title;
			data.text = text;
			const SIZE clientSize = ui::dlg_client_size(hOwner, AGREEMENT_DLG_WIDTH, AGREEMENT_DLG_HEIGHT);

			const HWND hDlg = CreateWindowExW(0, AGREEMENT_DLG_CLASS, (title ? title : L"用户协议"),
			                                  WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
			                                  0, 0, clientSize.cx, clientSize.cy, hOwner, nullptr,
			                                  GetModuleHandleW(nullptr), &data);
			if (!hDlg)
			{
				return;
			}
			ui::dlg_fit_client(hDlg, clientSize);
			ui::dlg_center_on(hDlg, hOwner);
			ShowWindow(hDlg, SW_SHOW);
			UpdateWindow(hDlg);
			SetFocus(hDlg); // 让弹窗接收鼠标滚轮与键盘方向键
			if (hOwner && IsWindow(hOwner))
			{
				EnableWindow(hOwner, FALSE);
			}

			MSG msg{};
			while (!data.done && GetMessageW(&msg, nullptr, 0, 0))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			if (hOwner && IsWindow(hOwner))
			{
				EnableWindow(hOwner, TRUE);
				SetFocus(hOwner);
			}
		}

		// 供首页用户须知等外部模块调用：直接打开《用户协议》/《隐私协议》弹窗（不涉及勾选回写）
		// 注意：这两个导出函数必须定义在 `namespace ui` 作用域（不能放在匿名命名空间内），
		// 否则会因内链接导致跨模块无法解析，故其定义放在本文件末尾 namespace ui 作用域处。
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

		// 按窗口 DPI 重建字体（首次创建与跨屏 DPI 变化共用）
		void activate_dlg_create_fonts(ActivateDialogData& data)
		{
			if (data.hFontTitle)
			{
				DeleteObject(data.hFontTitle);
			}
			if (data.hFontBody)
			{
				DeleteObject(data.hFontBody);
			}
			if (data.hFontSmall)
			{
				DeleteObject(data.hFontSmall);
			}
			if (data.hFontHint)
			{
				DeleteObject(data.hFontHint);
			}
			const int titleSize = -MulDiv(19, data.dpi, 72);
			const int bodySize = -MulDiv(14, data.dpi, 72);
			const int smallSize = -MulDiv(12, data.dpi, 72);
			const int hintSize = -MulDiv(9, data.dpi, 72);
			data.hFontTitle = CreateFontW(titleSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontBody = CreateFontW(bodySize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontSmall = CreateFontW(smallSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontHint = CreateFontW(hintSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			if (data.hTitle)
			{
				SendMessageW(data.hTitle, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontTitle), TRUE);
			}
			if (data.hSub)
			{
				SendMessageW(data.hSub, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontSmall), TRUE);
			}
			if (data.hEdit)
			{
				SendMessageW(data.hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontBody), TRUE);
			}
			if (data.hError)
			{
				SendMessageW(data.hError, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontHint), TRUE);
			}
			if (data.hBuy)
			{
				SendMessageW(data.hBuy, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontSmall), TRUE);
			}
			if (data.hOk)
			{
				SendMessageW(data.hOk, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontSmall), TRUE);
			}
			if (data.hCancel)
			{
				SendMessageW(data.hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontSmall), TRUE);
			}
		}

		// 按当前 DPI 摆放激活弹窗的子控件（WM_CREATE 与 WM_DPICHANGED 共用）
		void activate_dlg_layout(ActivateDialogData& data)
		{
			const auto S = [dpi = data.dpi](int v) { return ui::dlg_s(v, dpi); };
			const auto place = [](HWND h, int x, int y, int w, int hgt)
			{
				if (h)
				{
					SetWindowPos(h, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
				}
			};
			place(data.hTitle, S(58), S(22), S(ACT_DLG_WIDTH - 90), S(30));
			place(data.hSub, S(58), S(56), S(ACT_DLG_WIDTH - 90), S(20));
			place(data.hEdit, S(40), S(102), S(ACT_DLG_WIDTH - 80), S(38));
			place(data.hError, S(40), S(150), S(ACT_DLG_WIDTH - 80), S(28));
			place(data.hBuy, S(24), S(ACT_DLG_HEIGHT - 56), S(100), S(38));
			place(data.hOk, S(ACT_DLG_WIDTH - 200), S(ACT_DLG_HEIGHT - 56), S(110), S(38));
			place(data.hCancel, S(ACT_DLG_WIDTH - 88), S(ACT_DLG_HEIGHT - 56), S(74), S(38));
			activate_agree_rects(data.hwnd, data.hFontHint, data.dpi, data.rcAgreeBox, data.rcAgreeUA,
			                     data.rcAgreeConj, data.rcAgreePA);
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
				data->dpi = ui::dlg_dpi(hwnd);

				data->hTitle = CreateWindowExW(0, L"STATIC", L"软件激活",
				                               WS_CHILD | WS_VISIBLE | SS_LEFT,
				                               0, 0, 0, 0, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(3000)),
				                               hInst, nullptr);

				data->hSub = CreateWindowExW(0, L"STATIC", L"请输入激活码激活本软件",
				                             WS_CHILD | WS_VISIBLE | SS_LEFT,
				                             0, 0, 0, 0, hwnd,
				                             reinterpret_cast<HMENU>(static_cast<std::intptr_t>(3004)),
				                             hInst, nullptr);

				// 输入框：无自带边框（圆角面板由 WM_PAINT 绘制），白底深色文字
				data->hEdit = CreateWindowExW(0, L"EDIT", L"",
				                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
				                              0, 0, 0, 0, hwnd,
				                              reinterpret_cast<HMENU>(static_cast<std::intptr_t>(ACT_EDIT_ID)),
				                              hInst, nullptr);

				// 提示行：贴近输入框下方；默认灰色提示词，激活失败时切换红色错误提示（互斥显示）
				data->hError = CreateWindowExW(0, L"STATIC", ACT_HINT_TEXT.data(),
				                               WS_CHILD | WS_VISIBLE | SS_LEFT,
				                               0, 0, 0, 0, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(3005)),
				                               hInst, nullptr);
				// 提示行用不透明底刷：切换灰色提示词/红色错误提示时先清底，保证只显示当前提示词
				data->hHintBg = CreateSolidBrush(RGB(0xf7, 0xfa, 0xfe));

				// 按钮行：购买激活码（左）· 激 活 · 取 消（右）
				data->hBuy = CreateWindowExW(0, L"BUTTON", L"购买激活码",
				                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                             0, 0, 0, 0, hwnd,
				                             reinterpret_cast<HMENU>(static_cast<std::intptr_t>(ACT_BUY_ID)),
				                             hInst, nullptr);
				data->hOk = CreateWindowExW(0, L"BUTTON", L"激  活",
				                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
				                            0, 0, 0, 0, hwnd,
				                            reinterpret_cast<HMENU>(static_cast<std::intptr_t>(ACT_OK_ID)),
				                            hInst, nullptr);
				data->hCancel = CreateWindowExW(0, L"BUTTON", L"取  消",
				                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                                0, 0, 0, 0, hwnd,
				                                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(ACT_CANCEL_ID)),
				                                hInst, nullptr);

				// 统一布置子控件并缓存协议行命中区域（随 DPI 缩放）
				activate_dlg_create_fonts(*data);
				activate_dlg_layout(*data);
				return 0;
			}
			case WM_DPICHANGED:
				data->dpi = ui::dlg_on_dpi_changed(hwnd, lParam);
				activate_dlg_create_fonts(*data);
				activate_dlg_layout(*data);
				InvalidateRect(hwnd, nullptr, TRUE);
				return 0;
			case WM_SIZE:
				// 创建后校正客户区尺寸会触发 WM_SIZE，需按最终客户区重新摆放控件
				if (data && data->hwnd && wParam != SIZE_MINIMIZED)
				{
					activate_dlg_layout(*data);
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				return 0;
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				HDC hdc = BeginPaint(hwnd, &ps);
				const auto S = [dpi = data->dpi](int v) { return ui::dlg_s(v, dpi); };
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
					Ellipse(hdc, S(30), S(24), S(44), S(38));
					// 锁体（圆角矩形）
					RoundRect(hdc, S(26), S(34), S(48), S(50), S(3), S(3));
					// 锁孔
					Ellipse(hdc, S(34), S(39), S(40), S(45));
					SelectObject(hdc, hOldBr);
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
				}
				// 标题下分隔线
				{
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xe8, 0xee, 0xf6));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					MoveToEx(hdc, S(24), S(82), nullptr);
					LineTo(hdc, S(ACT_DLG_WIDTH - 24), S(82));
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
				}
				// 输入框白色圆角面板 + 主题色边框
				{
					RECT rcInput{S(24), S(92), S(ACT_DLG_WIDTH - 24), S(144)};
					draw_round_rect(hdc, rcInput, S(8), RGB(0xff, 0xff, 0xff), CLEAN_ACCENT, 1);
				}

				// 同意协议行：勾选框 + 「我已阅读并同意」 + 《用户协议》 + 和 + 《隐私协议》
				{
					const RECT& rcBox = data->rcAgreeBox;
					const RECT& rcUA = data->rcAgreeUA;
					const RECT& rcConj = data->rcAgreeConj;
					const RECT& rcPA = data->rcAgreePA;
					const bool checked = data->agreed;

					// 与「激活码作者签发…」提示行同字号
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontHint));
					SetBkMode(hdc, TRANSPARENT);

					// 勾选框：圆角方框；勾选时填充主题蓝并画白色对勾
					RECT rc = rcBox;
					const COLORREF fill = checked ? CLEAN_ACCENT : RGB(0xff, 0xff, 0xff);
					const COLORREF border = checked ? CLEAN_ACCENT : (data->boxHover ? CLEAN_ACCENT_LIGHT : RGB(0xc3, 0xcc, 0xd8));
					draw_round_rect(hdc, rc, S(4), fill, border, checked ? 0 : 1);
					if (checked)
					{
						HPEN hp = CreatePen(PS_SOLID, 2, RGB(0xff, 0xff, 0xff));
						HPEN hpo = static_cast<HPEN>(SelectObject(hdc, hp));
						MoveToEx(hdc, rc.left + S(4), rc.top + S(8), nullptr);
						LineTo(hdc, rc.left + S(7), rc.top + S(12));
						LineTo(hdc, rc.left + S(13), rc.top + S(5));
						SelectObject(hdc, hpo);
						DeleteObject(hp);
					}

					// 标签「我已阅读并同意」（灰色）
					{
						RECT rcLabel{rcBox.right + S(10), rcBox.top, rcUA.left - S(4), rcBox.bottom};
						SetTextColor(hdc, CLEAN_TEXT_SUB);
						DrawTextW(hdc, AGREE_LABEL, -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
					}

					// 链接《eBox 用户协议》（主题蓝，悬停时绘制下划线）
					{
						RECT rl = rcUA;
						SetTextColor(hdc, CLEAN_ACCENT);
						DrawTextW(hdc, AGREE_LINK_UA, -1, &rl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
						if (data->linkUAHover)
						{
							HPEN hp = CreatePen(PS_SOLID, 1, CLEAN_ACCENT);
							HPEN hpo = static_cast<HPEN>(SelectObject(hdc, hp));
							MoveToEx(hdc, rl.left, rl.bottom, nullptr);
							LineTo(hdc, rl.right, rl.bottom);
							SelectObject(hdc, hpo);
							DeleteObject(hp);
						}
					}

					// 连词「和」（灰色）
					{
						RECT rcC = rcConj;
						SetTextColor(hdc, CLEAN_TEXT_SUB);
						DrawTextW(hdc, AGREE_CONJ, -1, &rcC, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
					}

					// 链接《eBox 隐私协议》（主题蓝，悬停时绘制下划线）
					{
						RECT rl = rcPA;
						SetTextColor(hdc, CLEAN_ACCENT);
						DrawTextW(hdc, AGREE_LINK_PA, -1, &rl, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
						if (data->linkPAHover)
						{
							HPEN hp = CreatePen(PS_SOLID, 1, CLEAN_ACCENT);
							HPEN hpo = static_cast<HPEN>(SelectObject(hdc, hp));
							MoveToEx(hdc, rl.left, rl.bottom, nullptr);
							LineTo(hdc, rl.right, rl.bottom);
							SelectObject(hdc, hpo);
							DeleteObject(hp);
						}
					}

					SelectObject(hdc, hOldFont);
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
					// 提示行：不透明底（先清底再画新文字，互斥显示，防止旧提示词残留重叠）
					SetBkMode(hdc, OPAQUE);
					SetBkColor(hdc, RGB(0xf7, 0xfa, 0xfe));
					SetTextColor(hdc, data->errorMode ? CLEAN_DANGER : CLEAN_TEXT_SUB);
					return reinterpret_cast<LRESULT>(data->hHintBg);
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
			case WM_SETCURSOR:
			{
				// 悬停在同意勾选框或任一协议链接上时显示手型
				POINT ptCur{};
				GetCursorPos(&ptCur);
				ScreenToClient(hwnd, &ptCur);
				if (PtInRect(&data->rcAgreeBox, ptCur) || PtInRect(&data->rcAgreeUA, ptCur) ||
				    PtInRect(&data->rcAgreePA, ptCur))
				{
					SetCursor(LoadCursorW(nullptr, IDC_HAND));
					return TRUE;
				}
				break;
			}
			case WM_MOUSEMOVE:
			{
				const POINTS pts = MAKEPOINTS(lParam);
				const POINT pt{pts.x, pts.y};
				const bool overBox = PtInRect(&data->rcAgreeBox, pt);
				const bool overUA = PtInRect(&data->rcAgreeUA, pt);
				const bool overPA = PtInRect(&data->rcAgreePA, pt);
				if (!data->mouseTrack)
				{
					TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
					TrackMouseEvent(&tme);
					data->mouseTrack = true;
				}
				if (overBox != data->boxHover || overUA != data->linkUAHover || overPA != data->linkPAHover)
				{
					data->boxHover = overBox;
					data->linkUAHover = overUA;
					data->linkPAHover = overPA;
					RECT rcInvalid = data->rcAgreeBox;
					UnionRect(&rcInvalid, &rcInvalid, &data->rcAgreeUA);
					UnionRect(&rcInvalid, &rcInvalid, &data->rcAgreePA);
					InflateRect(&rcInvalid, 8, 8);
					InvalidateRect(hwnd, &rcInvalid, FALSE);
				}
				return 0;
			}
			case WM_MOUSELEAVE:
			{
				data->mouseTrack = false;
				if (data->boxHover || data->linkUAHover || data->linkPAHover)
				{
					data->boxHover = false;
					data->linkUAHover = false;
					data->linkPAHover = false;
					RECT rcInvalid = data->rcAgreeBox;
					UnionRect(&rcInvalid, &rcInvalid, &data->rcAgreeUA);
					UnionRect(&rcInvalid, &rcInvalid, &data->rcAgreePA);
					InflateRect(&rcInvalid, 8, 8);
					InvalidateRect(hwnd, &rcInvalid, FALSE);
				}
				return 0;
			}
			case WM_LBUTTONDOWN:
			{
				const POINTS pts = MAKEPOINTS(lParam);
				const POINT pt{pts.x, pts.y};
				if (PtInRect(&data->rcAgreeBox, pt))
				{
					data->agreed = !data->agreed;
					RECT rcInvalid = data->rcAgreeBox;
					InflateRect(&rcInvalid, 8, 8);
					InvalidateRect(hwnd, &rcInvalid, FALSE);
					return 0;
				}
				if (PtInRect(&data->rcAgreeUA, pt))
				{
					show_agreement_dialog(hwnd, &data->agreed, L"eBox 用户协议", AGREEMENT_TEXT);
					return 0;
				}
				if (PtInRect(&data->rcAgreePA, pt))
				{
					show_agreement_dialog(hwnd, &data->agreed, L"eBox 隐私协议", AGREEMENT_TEXT_PRIVACY);
					return 0;
				}
				break;
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
					// 未勾选同意《用户协议》时禁止激活（勾选后才可提交激活码）
					if (!data->agreed)
					{
						activate_dlg_show_error(*data, L"请先阅读并勾选同意《eBox 用户协议》");
						return 0;
					}
					if (data->activating)
					{
						return 0; // 校验进行中，防重入
					}
					wchar_t buf[1024]{};
					GetWindowTextW(data->hEdit, buf, 1024);
					const std::wstring code = buf;
					if (code.empty())
					{
						activate_dlg_show_error(*data, L"请输入激活码");
						return 0;
					}
					// 后台执行 tryActivate（内部可能同步联网，服务端不可达时最坏 40s，
					// 原实现会令 UI 线程未响应）→ 立即反馈"验证中"，完成后经线程消息回 UI
					data->activating = true;
					data->actShared = std::make_shared<ActShared>();
					data->errorMode = false;
					SetWindowTextW(data->hError, L"正在验证激活码，请稍候…");
					InvalidateRect(data->hError, nullptr, TRUE);
					EnableWindow(data->hOk, FALSE);
					SetWindowTextW(data->hOk, L"验证中…");
					auto shared = data->actShared; // 与工作线程共享生命周期
					const DWORD uiThreadId = GetCurrentThreadId();
					std::thread([shared, uiThreadId, code]()
					{
						shared->ok = biz::license::tryActivate(code);
						if (!shared->ok)
						{
							shared->reason = biz::license::lastActivateError();
						}
						shared->ready.store(true, std::memory_order_release);
						PostThreadMessageW(uiThreadId, ACT_WM_DONE, 0, 0);
					}).detach();
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
				if (data->hHintBg)
				{
					DeleteObject(data->hHintBg);
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
		HWND hOwner = owner ? owner->nativeHandle() : nullptr;
		const SIZE clientSize = dlg_client_size(hOwner, ACT_DLG_WIDTH, ACT_DLG_HEIGHT);
		const HWND hDlg = CreateWindowExW(0, ACTIVATE_DLG_CLASS, L"软件激活",
		                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
		                                  0, 0, clientSize.cx, clientSize.cy, hOwner, nullptr,
		                                  GetModuleHandleW(nullptr), &data);
		if (!hDlg)
		{
			return false;
		}
		dlg_fit_client(hDlg, clientSize);
		dlg_center_on(hDlg, hOwner);
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
			// 后台激活完成（线程消息，hwnd 为空）：从共享块取结果并收尾
			if (msg.message == ACT_WM_DONE && msg.hwnd == nullptr)
			{
				if (data.actShared && data.actShared->ready.load(std::memory_order_acquire))
				{
					const bool ok = data.actShared->ok;
					const std::wstring reason = data.actShared->reason;
					data.actShared.reset();
					data.activating = false;
					EnableWindow(data.hOk, TRUE);
					SetWindowTextW(data.hOk, L"激  活");
					if (ok)
					{
						data.activated = true;
						DestroyWindow(hDlg);
					}
					else
					{
						// 优先展示具体失败原因（作废/过期/已绑定其他机器/强制在线等）
						activate_dlg_show_error(data, reason.empty() ? L"激活码无效，请重新输入" : reason);
						SetFocus(data.hEdit);
						SendMessageW(data.hEdit, EM_SETSEL, 0, static_cast<LPARAM>(-1));
					}
				}
				continue;
			}
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

	// ---- 解绑成功弹窗：展示换机码 + 一键复制 + 5s 倒计时后确认关闭 ----
	namespace
	{
		constexpr wchar_t UNBIND_SUCCESS_CLASS[] = L"eBoxUnbindSuccessDialog";
		constexpr int UNBIND_TITLE_ID = 4103;
		constexpr int UNBIND_SUB_ID = 4104;
		constexpr int UNBIND_CODE_ID = 4105;
		constexpr int UNBIND_REMIND_ID = 4106;
		constexpr int UNBIND_NOTE_ID = 4107;
		constexpr int UNBIND_STATE_ID = 4108;
		constexpr int UNBIND_COPY_ID = 4102;
		constexpr int UNBIND_OK_ID = 4101;
		constexpr int UNBIND_W = 520;
		constexpr int UNBIND_H = 316;
		constexpr int UNBIND_COUNTDOWN_SEC = 5;

		struct UnbindSuccessData
		{
			HWND hwnd{nullptr};
			HWND hTitle{nullptr};
			HWND hSub{nullptr};
			HWND hCode{nullptr};
			HWND hRemind{nullptr};
			HWND hNote{nullptr};
			HWND hState{nullptr};
			HWND hCopy{nullptr};
			HWND hOk{nullptr};
			HFONT hFontTitle{nullptr};
			HFONT hFontCode{nullptr};
			HFONT hFontRemind{nullptr};
			HFONT hFontSmall{nullptr};
			HBRUSH hStateBg{nullptr};    // 状态行不透明底刷：倒计时/复制切换文案时清底，防止旧文字残留重影
			std::wstring code;
			int dpi{96};
			int remain{UNBIND_COUNTDOWN_SEC};
			bool copied{false};
			bool done{false};
			bool confirmed{false};
		};

		void copy_text_to_clipboard(HWND owner, const std::wstring& text)
		{
			if (!OpenClipboard(owner))
			{
				return;
			}
			EmptyClipboard();
			const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
			if (HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes))
			{
				if (wchar_t* p = static_cast<wchar_t*>(GlobalLock(hMem)))
				{
					memcpy(p, text.c_str(), bytes);
					GlobalUnlock(hMem);
					SetClipboardData(CF_UNICODETEXT, hMem);
				}
			}
			CloseClipboard();
		}

		// 刷新倒计时/状态：更新确定按钮的使能与文案、状态提示行
		void unbind_success_refresh(UnbindSuccessData& d, int remain)
		{
			d.remain = remain;
			if (d.hOk)
			{
				const bool enabled = (remain <= 0);
				EnableWindow(d.hOk, enabled ? TRUE : FALSE);
				SetWindowTextW(d.hOk, enabled ? L"确  定" : std::format(L"确定 ({})", remain).c_str());
				InvalidateRect(d.hOk, nullptr, TRUE);
			}
			if (d.hState)
			{
				std::wstring status;
				if (remain > 0)
				{
					status = d.copied
						? std::format(L"已复制，等待 {} 秒后可点“确定”。", remain)
						: L"倒计时中，可先点击“一键复制新换机码”。";
				}
				else
				{
					status = d.copied
						? L"已复制换机码，可点击“确定”关闭。"
						: L"请先点击“一键复制新换机码”，再点“确定”。";
				}
				SetWindowTextW(d.hState, status.c_str());
				InvalidateRect(d.hState, nullptr, TRUE);
			}
		}

		// 未点击“一键复制”就点确定/关闭时，二次确认是否已复制（按钮：已复制确认关闭 / 取消）
		bool unbind_success_confirm_close(HWND owner)
		{
			const TASKDIALOG_BUTTON yesBtn{100, L"已复制确认关闭"};
			const TASKDIALOG_BUTTON noBtn{200, L"取  消"};
			TASKDIALOG_BUTTON btns[2]{yesBtn, noBtn};
			TASKDIALOGCONFIG cfg{};
			cfg.cbSize = sizeof(cfg);
			cfg.hwndParent = owner;
			cfg.hInstance = GetModuleHandleW(nullptr);
			cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
			cfg.pszWindowTitle = L"解绑本机 · 请确认";
			cfg.pszMainIcon = TD_WARNING_ICON;
			cfg.pszMainInstruction = L"尚未点击“一键复制”，请确认是否已复制换机码？";
			cfg.pszContent = L"若此刻关闭本弹窗，将无法再次获取换机码。\n建议先点击“一键复制新换机码”，再关闭。";
			cfg.cButtons = 2;
			cfg.pButtons = btns;
			int pressed = 0;
			TaskDialogIndirect(&cfg, &pressed, nullptr, nullptr);
			return pressed == 100;
		}

		void unbind_success_dlg_create_fonts(UnbindSuccessData& d)
		{
			const auto release = [](HFONT& font)
			{
				if (font)
				{
					DeleteObject(font);
					font = nullptr;
				}
			};
			release(d.hFontTitle);
			release(d.hFontCode);
			release(d.hFontRemind);
			release(d.hFontSmall);

			d.hFontTitle = CreateFontW(-MulDiv(12, d.dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			d.hFontCode = CreateFontW(-MulDiv(10, d.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
			d.hFontRemind = CreateFontW(-MulDiv(11, d.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			d.hFontSmall = CreateFontW(-MulDiv(10, d.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

			const HWND controls[] = {d.hTitle, d.hCode, d.hRemind, d.hNote, d.hState, d.hCopy, d.hOk};
			const HFONT fonts[] = {d.hFontTitle, d.hFontCode, d.hFontRemind, d.hFontSmall,
			                       d.hFontSmall, d.hFontSmall, d.hFontSmall};
			for (int i = 0; i < static_cast<int>(sizeof(controls) / sizeof(controls[0])); ++i)
			{
				if (controls[i])
				{
					SendMessageW(controls[i], WM_SETFONT, reinterpret_cast<WPARAM>(fonts[i]), TRUE);
				}
			}
		}

		// 按窗口 DPI 摆放子控件，并按码框可用宽度重新折行换机码
		void unbind_success_dlg_layout(UnbindSuccessData& d)
		{
			if (!d.hwnd)
			{
				return;
			}
			const auto S = [dpi = d.dpi](int value) { return ui::dlg_s(value, dpi); };
			const int left = S(32);
			const int width = S(UNBIND_W) - S(64);
			const int right = left + width;

			const auto place = [](HWND hCtrl, int x, int y, int w, int h)
			{
				if (hCtrl)
				{
					SetWindowPos(hCtrl, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
				}
			};
			place(d.hTitle, left, S(14), width, S(24));
			place(d.hCode, left, S(40), width, S(100));
			place(d.hRemind, left, S(156), width, S(22));
			place(d.hNote, left, S(180), width, S(20));
			place(d.hState, left, S(204), width, S(22));
			const int btnW = S(216);
			place(d.hCopy, left, S(234), btnW, S(44));
			place(d.hOk, right - btnW, S(234), btnW, S(44));

			// 换机码：按实际码字体测量单字符宽度，用码框可用宽度计算每行字符数，
			// 使换机码横向铺满，且在高 DPI（PerMonitor）下也不会因字体变宽而溢出裁剪。
			if (d.hCode)
			{
				int charsPerLine = 44;
				const HDC hdc = GetDC(d.hwnd);
				if (hdc)
				{
					const HGDIOBJ hOld = SelectObject(hdc, d.hFontCode);
					SIZE sz{};
					GetTextExtentPoint32W(hdc, L"W", 1, &sz);
					SelectObject(hdc, hOld);
					ReleaseDC(d.hwnd, hdc);
					// 预留 8px 右余量，避免字体度量舍入误差导致最后一行溢出
					const int usable = width - S(8);
					if (sz.cx > 0)
					{
						charsPerLine = usable / sz.cx;
						if (charsPerLine < 16)
						{
							charsPerLine = 16;
						}
					}
				}
				std::wstring codeText;
				for (std::size_t i = 0; i < d.code.size(); i += static_cast<std::size_t>(charsPerLine))
				{
					if (!codeText.empty())
					{
						codeText += L"\r\n";
					}
					codeText += d.code.substr(i, static_cast<std::size_t>(charsPerLine));
				}
				SetWindowTextW(d.hCode, codeText.c_str());
			}
		}

		LRESULT CALLBACK unbind_success_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* d = reinterpret_cast<UnbindSuccessData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			switch (msg)
			{
			case WM_CREATE:
			{
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
				d = static_cast<UnbindSuccessData*>(cs->lpCreateParams);
				d->hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(d));

				const HMODULE hInst = GetModuleHandleW(nullptr);
				d->dpi = ui::dlg_dpi(hwnd);

				d->hTitle = CreateWindowExW(0, L"STATIC", L"解绑成功！已生成换机激活码，已自动复制：",
					WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd,
					reinterpret_cast<HMENU>(static_cast<std::intptr_t>(UNBIND_TITLE_ID)), hInst, nullptr);

				// 换机码：静态文本（无输入框边框、可鼠标选区复制），内容由布局函数按宽度折行填充
				d->hCode = CreateWindowExW(0, L"STATIC", L"",
					WS_CHILD | WS_VISIBLE | SS_LEFT | SS_EDITCONTROL | SS_NOPREFIX,
					0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<std::intptr_t>(UNBIND_CODE_ID)), hInst, nullptr);

				d->hRemind = CreateWindowExW(0, L"STATIC", L"请在新电脑上粘贴此激活码完成激活，剩余时长将自动继承。",
					WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd,
					reinterpret_cast<HMENU>(static_cast<std::intptr_t>(UNBIND_REMIND_ID)), hInst, nullptr);

				d->hNote = CreateWindowExW(0, L"STATIC", L"本机将退出授权，应用即将关闭。",
					WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd,
					reinterpret_cast<HMENU>(static_cast<std::intptr_t>(UNBIND_NOTE_ID)), hInst, nullptr);

				d->hState = CreateWindowExW(0, L"STATIC", L"请点击“一键复制新换机码”做好备份。",
					WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd,
					reinterpret_cast<HMENU>(static_cast<std::intptr_t>(UNBIND_STATE_ID)), hInst, nullptr);
				d->hStateBg = CreateSolidBrush(RGB(0xf4, 0xf7, 0xfb));

				d->hCopy = CreateWindowExW(0, L"BUTTON", L"一键复制新换机码", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
					0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<std::intptr_t>(UNBIND_COPY_ID)), hInst, nullptr);

				d->hOk = CreateWindowExW(0, L"BUTTON", L"确  定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
					0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<std::intptr_t>(UNBIND_OK_ID)), hInst, nullptr);

				unbind_success_dlg_create_fonts(*d);
				unbind_success_dlg_layout(*d);
				unbind_success_refresh(*d, d->remain);
				SetTimer(hwnd, 1, 1000, nullptr);
				return 0;
			}
			case WM_DPICHANGED:
			{
				if (d)
				{
					d->dpi = ui::dlg_on_dpi_changed(hwnd, lParam);
					unbind_success_dlg_create_fonts(*d);
					unbind_success_dlg_layout(*d);
					InvalidateRect(hwnd, nullptr, TRUE);
				}
				return 0;
			}
			case WM_SIZE:
				// 创建后校正客户区尺寸会触发 WM_SIZE，需按最终客户区重新摆放控件
				if (d && d->hwnd && wParam != SIZE_MINIMIZED)
				{
					unbind_success_dlg_layout(*d);
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				return 0;
			case WM_TIMER:
				if (wParam == 1 && d->remain > 0)
				{
					unbind_success_refresh(*d, d->remain - 1);
					if (d->remain <= 0)
					{
						KillTimer(hwnd, 1);
					}
				}
				return 0;
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				HDC hdc = BeginPaint(hwnd, &ps);
				RECT rcClient{};
				GetClientRect(hwnd, &rcClient);
				fill_v_gradient(hdc, rcClient, RGB(0xff, 0xff, 0xff), RGB(0xf4, 0xf7, 0xfb));
				{
					const auto S = [dpi = d->dpi](int value) { return ui::dlg_s(value, dpi); };
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xe6, 0xec, 0xf4));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					MoveToEx(hdc, S(24), S(148), nullptr);
					LineTo(hdc, rcClient.right - S(24), S(148));
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
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
				if (hWnd == d->hState)
				{
					// 状态行：不透明底（先清底再画新文字），倒计时/复制切换文案时防止旧文字残留重影
					SetBkMode(hdc, OPAQUE);
					SetBkColor(hdc, RGB(0xf4, 0xf7, 0xfb));
					SetTextColor(hdc, CLEAN_TEXT_SUB);
					return reinterpret_cast<LRESULT>(d->hStateBg ? d->hStateBg : GetStockObject(WHITE_BRUSH));
				}
				if (hWnd == d->hRemind)
				{
					SetTextColor(hdc, CLEAN_DANGER);
				}
				else if (hWnd == d->hNote)
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
				if (id == UNBIND_OK_ID || id == UNBIND_COPY_ID)
				{
					const bool primary = (id == UNBIND_OK_ID);
					const bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
					const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
					wchar_t buf[64]{};
					GetWindowTextW(dis->hwndItem, buf, 64);
					draw_modern_dlg_button(dis->hDC, dis->rcItem, primary, hover, pressed, buf);
					return TRUE;
				}
				return FALSE;
			}
			case WM_COMMAND:
			{
				const int id = LOWORD(wParam);
				if (id == UNBIND_COPY_ID)
				{
					copy_text_to_clipboard(hwnd, d->code);
					d->copied = true;
					unbind_success_refresh(*d, d->remain);
					return 0;
				}
				if (id == UNBIND_OK_ID)
				{
					if (d->remain > 0)
					{
						return 0; // 倒计时未结束，忽略提前点击
					}
					if (!d->copied && !unbind_success_confirm_close(hwnd))
					{
						// 用户选择“取消”：留在弹窗，可先复制
						if (d->hState)
						{
							SetWindowTextW(d->hState, L"请先点击“一键复制新换机码”，再点“确定”关闭。");
							InvalidateRect(d->hState, nullptr, TRUE);
						}
						return 0;
					}
					d->confirmed = true;
					DestroyWindow(hwnd);
					return 0;
				}
				return 0;
			}
			case WM_CLOSE:
				if (d->copied || unbind_success_confirm_close(hwnd))
				{
					d->confirmed = true;
					DestroyWindow(hwnd);
				}
				return 0;
			case WM_DESTROY:
				if (d->hFontTitle)
				{
					DeleteObject(d->hFontTitle);
				}
				if (d->hFontCode)
				{
					DeleteObject(d->hFontCode);
				}
				if (d->hFontRemind)
				{
					DeleteObject(d->hFontRemind);
				}
				if (d->hFontSmall)
				{
					DeleteObject(d->hFontSmall);
				}
				if (d->hStateBg)
				{
					DeleteObject(d->hStateBg);
				}
				d->done = true;
				return 0;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		bool register_unbind_success_class()
		{
			WNDCLASSEXW wc{};
			wc.cbSize = sizeof(wc);
			wc.style = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = unbind_success_proc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = nullptr;
			wc.lpszClassName = UNBIND_SUCCESS_CLASS;
			return RegisterClassExW(&wc) != 0;
		}

		// 解绑成功弹窗：展示换机码 + 一键复制 + 5s 倒计时后确认关闭；返回用户是否已确认关闭
		bool show_unbind_success_dialog(HWND owner, const std::wstring& newCode)
		{
			static const bool classRegistered = register_unbind_success_class();
			(void)classRegistered;

			UnbindSuccessData data;
			data.code = newCode;
			const SIZE clientSize = ui::dlg_client_size(owner, UNBIND_W, UNBIND_H);
			const HWND hDlg = CreateWindowExW(0, UNBIND_SUCCESS_CLASS, L"解绑本机 · 换机成功",
				WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0, clientSize.cx, clientSize.cy, owner, nullptr,
				GetModuleHandleW(nullptr), &data);
			if (!hDlg)
			{
				return false;
			}
			ui::dlg_fit_client(hDlg, clientSize);
			ui::dlg_center_on(hDlg, owner);
			if (owner && IsWindow(owner))
			{
				EnableWindow(owner, FALSE);
			}
			ShowWindow(hDlg, SW_SHOW);

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

			if (owner && IsWindow(owner))
			{
				EnableWindow(owner, TRUE);
				SetForegroundWindow(owner);
			}
			return data.confirmed;
		}
	}

	// ---- 授权信息对话框（现代化渐变风格）----
	namespace
	{
		constexpr wchar_t LICENSE_INFO_CLASS[] = L"eBoxLicenseInfoDialog";
		constexpr int INFO_REACTIVATE_ID = 4001;
		constexpr int INFO_BUY_ID = 4002;
		constexpr int INFO_SERVICE_ID = 4003;
		constexpr int INFO_CLOSE_ID = 4004;
		constexpr int INFO_UNBIND_ID = 4005;
		constexpr int INFO_COPY_CODE_ID = 4006;
		constexpr int INFO_WEBSITE_ID = 4007;       // 标题右侧"访问官网"（复制激活码下方）
		constexpr int INFO_DLG_WIDTH = 520;
		constexpr int INFO_DLG_HEIGHT = 360;
		constexpr UINT INFO_WM_UNBIND_DONE = WM_APP + 0x33; // 后台解绑完成（线程消息，结果经 UnbindShared 交接，消息不携带指针）

		// 后台解绑共享结果：工作线程与 UI 交接数据，消息本身不携带指针，杜绝悬垂/泄漏
		struct UnbindShared
		{
			biz::license::UnbindOutcome outcome;
			std::atomic_bool ready{false};
		};

		struct LicenseInfoDialogData
		{
			HWND hwnd{nullptr};
			HFONT hFontTitle{nullptr};
			HFONT hFontBody{nullptr};
			HFONT hFontSmall{nullptr};
			HFONT hFontHint{nullptr};
			HWND hUnbind{nullptr};      // 解绑按钮（后台解绑期间禁用并改文案）
			HWND hReactivate{nullptr};
			HWND hBuy{nullptr};
			HWND hService{nullptr};
			HWND hClose{nullptr};
			HWND hCopyCode{nullptr};
			HWND hWebsite{nullptr};
			int dpi{96};
			bool done{false};
			LicenseInfoResult result{LicenseInfoResult::None};
			bool activated{false};
			bool isBound{false};
			int unbindCount{0};
			int unbindMax{0};
			bool unbinding{false};      // 后台解绑进行中（防重入）
			std::shared_ptr<UnbindShared> unbindShared; // 与工作线程共享的解绑结果块
			std::wstring expireText;
			std::wstring fp;
			std::wstring version;
		};

		void info_dlg_create_fonts(LicenseInfoDialogData& data)
		{
			auto release = [](HFONT& font)
			{
				if (font)
				{
					DeleteObject(font);
					font = nullptr;
				}
			};
			release(data.hFontTitle);
			release(data.hFontBody);
			release(data.hFontSmall);
			release(data.hFontHint);
			const int dpi = data.dpi > 0 ? data.dpi : 96;
			data.hFontTitle = CreateFontW(-MulDiv(19, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontBody = CreateFontW(-MulDiv(13, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontSmall = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontHint = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		}

		// 底部按钮行 + 卡片内解绑按钮 + 标题右侧按钮，全部按 dpi 与客户区宽度自适应摆放
		void info_dlg_layout(LicenseInfoDialogData& data)
		{
			if (!data.hwnd)
			{
				return;
			}
			const auto S = [dpi = data.dpi](int value) { return ui::dlg_s(value, dpi); };
			RECT client{};
			GetClientRect(data.hwnd, &client);
			const int width = client.right;
			const int height = client.bottom;
			const auto place = [](HWND control, int x, int y, int w, int h)
			{
				if (control)
				{
					SetWindowPos(control, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
				}
			};

			// 底部按钮行：设计宽度按 472 总宽等比分配
			const int designWidths[2][4] = {{120, 110, 96, 88}, {130, 120, 100, 64}};
			const int* widths = designWidths[data.isBound ? 0 : 1];
			const int left = S(24);
			const int right = width - S(24);
			const int gap = S(16);
			const int totalDesign = 120 + 110 + 96 + 88;
			const int avail = std::max(S(240), right - left - gap * 3);
			const int btnHeight = S(38);
			const int btnTop = height - S(16) - btnHeight;
			const HWND btnRow[4] = {data.hReactivate, data.hBuy, data.hService, data.hClose};
			int x = left;
			for (int i = 0; i < 4; ++i)
			{
				const int w = i == 3 ? (right - x) : MulDiv(avail, widths[i], totalDesign);
				place(btnRow[i], x, btnTop, std::max(S(48), w), btnHeight);
				x += w + gap;
			}

			// 「解绑次数」行内按钮（贴卡片右下）
			place(data.hUnbind, width - S(124), S(227), S(88), S(28));

			// 标题右侧：复制激活码 / 访问官网
			if (data.hCopyCode)
			{
				place(data.hCopyCode, width - S(140), S(24), S(116), S(28));
			}
			place(data.hWebsite, width - S(140), S(54), S(116), S(24));
		}

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
				data->dpi = ui::dlg_dpi(hwnd);
				info_dlg_create_fonts(*data);

				// 底部按钮行：重新激活/续期 · 购买激活码 · 联系客服 · 关闭（解绑按钮移到解绑次数行内）
				auto createBtn = [&](int id, LPCWSTR text, bool defPush = false) -> HWND
				{
					return CreateWindowExW(0, L"BUTTON", text,
					                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | (defPush ? BS_DEFPUSHBUTTON : 0),
					                       0, 0, 0, 0, hwnd,
					                       reinterpret_cast<HMENU>(static_cast<std::intptr_t>(id)),
					                       hInst, nullptr);
				};
				data->hReactivate = createBtn(INFO_REACTIVATE_ID, L"重新激活/续期", true);
				data->hBuy = createBtn(INFO_BUY_ID, L"购买激活码");
				data->hService = createBtn(INFO_SERVICE_ID, L"联系客服");
				data->hClose = createBtn(INFO_CLOSE_ID, L"关闭");
				if (data->isBound)
				{
					// 解绑按钮放在"解绑次数"行内（与值文本等高）
					data->hUnbind = CreateWindowExW(0, L"BUTTON", L"解绑本机",
					                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
					                                0, 0, 0, 0, hwnd,
					                                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INFO_UNBIND_ID)),
					                                hInst, nullptr);
				}
				// 授权信息标题右侧：复制激活码按钮（仅已激活时可用）
				if (data->activated)
				{
					data->hCopyCode = CreateWindowExW(0, L"BUTTON", L"复制激活码",
					                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
					                                  0, 0, 0, 0, hwnd,
					                                  reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INFO_COPY_CODE_ID)),
					                                  hInst, nullptr);
				}
				// 复制激活码下方：访问官网按钮（浅色系，比复制激活码更低调）
				data->hWebsite = CreateWindowExW(0, L"BUTTON", L"访问官网",
				                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                                 0, 0, 0, 0, hwnd,
				                                 reinterpret_cast<HMENU>(static_cast<std::intptr_t>(INFO_WEBSITE_ID)),
				                                 hInst, nullptr);
				info_dlg_layout(*data);
				return 0;
			}
			case WM_DPICHANGED:
			{
				if (data)
				{
					data->dpi = ui::dlg_on_dpi_changed(hwnd, lParam);
					info_dlg_create_fonts(*data);
					info_dlg_layout(*data);
					InvalidateRect(hwnd, nullptr, TRUE);
				}
				return 0;
			}
			case WM_SIZE:
				// 创建后校正客户区尺寸会触发 WM_SIZE，需按最终客户区重新摆放控件
				if (data && data->hwnd && wParam != SIZE_MINIMIZED)
				{
					info_dlg_layout(*data);
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				return 0;
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				HDC hdc = BeginPaint(hwnd, &ps);
				RECT rcClient{};
				GetClientRect(hwnd, &rcClient);
				const auto S = [dpi = data->dpi](int value) { return ui::dlg_s(value, dpi); };
				const int clientRight = rcClient.right;
				// 背景：浅蓝 → 白 垂直渐变
				fill_v_gradient(hdc, rcClient, RGB(0xee, 0xf5, 0xfe), RGB(0xff, 0xff, 0xff));

				// 头部信息徽标（主题蓝圆底 + 白色 i）
				{
					HBRUSH hCircle = CreateSolidBrush(CLEAN_ACCENT);
					HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hCircle));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, GetStockObject(NULL_PEN)));
					Ellipse(hdc, S(26), S(22), S(50), S(46));
					SelectObject(hdc, hOldBr);
					DeleteObject(hCircle);
					SelectObject(hdc, hOldPen);
					SetTextColor(hdc, RGB(0xff, 0xff, 0xff));
					SetBkMode(hdc, TRANSPARENT);
					RECT rcI{S(26), S(24), S(50), S(44)};
					DrawTextW(hdc, L"i", -1, &rcI, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				}
				// 标题
				{
					SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
					SetBkMode(hdc, TRANSPARENT);
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontTitle));
					RECT rcTitle{S(58), S(22), clientRight - S(24), S(50)};
					DrawTextW(hdc, L"授权信息", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SelectObject(hdc, hOldFont);
				}
				// 副标题
				{
					SetTextColor(hdc, CLEAN_TEXT_SUB);
					SetBkMode(hdc, TRANSPARENT);
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontSmall));
					RECT rcSub{S(58), S(52), clientRight - S(24), S(72)};
					DrawTextW(hdc, L"软件授权状态与续期入口", -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SelectObject(hdc, hOldFont);
				}
				// 分隔线
				{
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xe8, 0xee, 0xf6));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					MoveToEx(hdc, S(24), S(80), nullptr);
					LineTo(hdc, clientRight - S(24), S(80));
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
				}
				// 白色信息卡片（6 行信息）
				{
					RECT rcCard{S(24), S(92), clientRight - S(24), S(260)};
					draw_round_rect(hdc, rcCard, S(10), RGB(0xff, 0xff, 0xff), RGB(0xe8, 0xee, 0xf6), 1);
					// 行分隔线
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xf0, 0xf4, 0xf8));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					for (int row = 1; row < 6; ++row)
					{
						const int y = rcCard.top + S(6) + row * S(27);
						MoveToEx(hdc, rcCard.left + S(16), y, nullptr);
						LineTo(hdc, rcCard.right - S(16), y);
					}
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);

					const wchar_t* labels[6] = {L"激活状态", L"在线状态", L"到期时间", L"当前版本", L"本机指纹", L"解绑次数"};
					const std::wstring onlineText = data->activated ? biz::license::onlineStatusText() : L"（未激活）";
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
					// 到期时间：距到期 <=7 天时附加红色"剩余 X 天"提醒（红点点击进入即见，不重复弹窗）
					const int remainDays = data->activated ? biz::license::remainingDays() : -1;
					std::wstring expireCell = data->activated && !data->expireText.empty() ? data->expireText : L"（未激活）";
					if (remainDays >= 1 && remainDays <= 7)
					{
						expireCell += std::format(L"（剩余 {} 天）", remainDays);
					}
					const std::wstring values[6] =
					{
						data->activated ? L"已激活" : L"未激活",
						onlineText,
						expireCell,
						data->version,
						data->fp,
						unbindText,
					};
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontSmall));
					SetBkMode(hdc, TRANSPARENT);
					for (int row = 0; row < 6; ++row)
					{
						const int rowTop = rcCard.top + S(6) + row * S(27);
						// 标签（浅灰）
						SetTextColor(hdc, CLEAN_TEXT_SUB);
						RECT rcLabel{rcCard.left + S(18), rowTop, rcCard.left + S(140), rowTop + S(24)};
						DrawTextW(hdc, labels[row], -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
						// 值（深色；激活状态/在线状态/解绑次数特殊着色）
						if (row == 0)
						{
							SetTextColor(hdc, data->activated ? RGB(0x16, 0xa3, 0x4a) : CLEAN_DANGER);
						}
						else if (row == 1)
						{
							// 在线状态：锁定/作废/到期→红，在线→绿，纯离线→灰
							const std::wstring st = biz::license::onlineStatusText();
							SetTextColor(hdc, st.find(L"锁定") != std::wstring::npos || st.find(L"作废") != std::wstring::npos || st.find(L"到期") != std::wstring::npos
								            ? CLEAN_DANGER
								            : (st.find(L"离线") != std::wstring::npos ? CLEAN_TEXT_SUB : RGB(0x16, 0xa3, 0x4a)));
						}
						else if (row == 5 && data->isBound && data->unbindMax >= 0 &&
						         data->unbindCount >= data->unbindMax)
						{
							// 解绑次数已用尽 → 红色警示
							SetTextColor(hdc, CLEAN_DANGER);
						}
						else if (row == 2 && remainDays >= 1 && remainDays <= 7)
						{
							// 到期临近（<=7 天）→ 红色警示
							SetTextColor(hdc, CLEAN_DANGER);
						}
						else
						{
							SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
						}
						RECT rcValue{rcCard.left + S(140), rowTop, rcCard.right - S(18), rowTop + S(24)};
						// 解绑次数行：行内有解绑按钮，值文本结尾避开按钮
						if (row == 5 && data->isBound)
						{
							rcValue.right = clientRight - S(132);
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
					RECT rcHint{S(28), S(272), clientRight - S(28), S(292)};
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
				if (id == INFO_REACTIVATE_ID || id == INFO_BUY_ID || id == INFO_SERVICE_ID || id == INFO_CLOSE_ID || id == INFO_UNBIND_ID || id == INFO_COPY_CODE_ID || id == INFO_WEBSITE_ID)
				{
					const bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
					const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
					wchar_t buf[32]{};
					GetWindowTextW(dis->hwndItem, buf, 32);
					if (id == INFO_WEBSITE_ID)
					{
						// 访问官网：浅蓝色按钮（比"复制激活码"更低调，颜色不同）
						const COLORREF fill = pressed ? RGB(0xd6, 0xe7, 0xf8) : (hover ? RGB(0xe4, 0xef, 0xfb) : RGB(0xf0, 0xf6, 0xfd));
						const COLORREF border = hover ? RGB(0xa9, 0xc9, 0xea) : RGB(0xd0, 0xdf, 0xf0);
						draw_round_rect(dis->hDC, dis->rcItem, ui::dlg_s(8, data ? data->dpi : 96), fill, border, 1);
						SetBkMode(dis->hDC, TRANSPARENT);
						SetTextColor(dis->hDC, RGB(0x2f, 0x6b, 0xa8));
						DrawTextW(dis->hDC, buf, -1, const_cast<RECT*>(&dis->rcItem), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
						return TRUE;
					}
					const bool primary = (id == INFO_REACTIVATE_ID);
					const bool danger = (id == INFO_UNBIND_ID);
					const bool success = (id == INFO_BUY_ID); // 购买激活码按钮绿色
					draw_modern_dlg_button(dis->hDC, dis->rcItem, primary, hover, pressed, buf, danger, success);
					return TRUE;
				}
				return FALSE;
			}
			case WM_COMMAND:
			{
				const int id = LOWORD(wParam);
				if (id == INFO_WEBSITE_ID)
				{
					// 访问官网：用系统默认浏览器打开官网首页
					if (!MainApp::kWebsiteUrl.empty())
					{
						ShellExecuteW(nullptr, L"open", std::wstring{MainApp::kWebsiteUrl}.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
					return 0;
				}
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
					if (data->unbinding)
					{
						return 0; // 解绑进行中，防重入
					}
					// 后台执行 unbindForSwitch（内部可能同步联网，服务端不可达时最坏 40s，
					// 原实现会令 UI 线程未响应）→ 按钮切"解绑中…"，完成后经线程消息回 UI
					data->unbinding = true;
					data->unbindShared = std::make_shared<UnbindShared>();
					if (data->hUnbind)
					{
						EnableWindow(data->hUnbind, FALSE);
						SetWindowTextW(data->hUnbind, L"解绑中…");
						InvalidateRect(data->hUnbind, nullptr, TRUE);
					}
					auto shared = data->unbindShared; // 与工作线程共享生命周期
					const DWORD uiThreadId = GetCurrentThreadId();
					std::thread([shared, uiThreadId]()
					{
						shared->outcome = biz::license::unbindForSwitch();
						shared->ready.store(true, std::memory_order_release);
						PostThreadMessageW(uiThreadId, INFO_WM_UNBIND_DONE, 0, 0);
					}).detach();
					return 0;
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
		HWND hOwner = owner ? owner->nativeHandle() : nullptr;
		const SIZE clientSize = ui::dlg_client_size(hOwner, INFO_DLG_WIDTH, INFO_DLG_HEIGHT);
		const HWND hDlg = CreateWindowExW(0, LICENSE_INFO_CLASS, L"授权信息",
		                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
		                                  0, 0, clientSize.cx, clientSize.cy, hOwner, nullptr,
		                                  GetModuleHandleW(nullptr), &data);
		if (!hDlg)
		{
			return LicenseInfoResult::None;
		}
		ui::dlg_fit_client(hDlg, clientSize);
		ui::dlg_center_on(hDlg, hOwner);
		ShowWindow(hDlg, SW_SHOW);

		if (hOwner && IsWindow(hOwner))
		{
			EnableWindow(hOwner, FALSE);
		}

		MSG msg{};
		while (!data.done && GetMessageW(&msg, nullptr, 0, 0))
		{
			// 后台解绑完成（线程消息，hwnd 为空）：从共享块取结果并按结果分支提示
			if (msg.message == INFO_WM_UNBIND_DONE && msg.hwnd == nullptr)
			{
				if (data.unbindShared && data.unbindShared->ready.load(std::memory_order_acquire))
				{
					const biz::license::UnbindOutcome outcome = data.unbindShared->outcome;
					data.unbindShared.reset();
					data.unbinding = false;
					if (data.hUnbind)
					{
						EnableWindow(data.hUnbind, TRUE);
						SetWindowTextW(data.hUnbind, L"解绑本机");
						InvalidateRect(data.hUnbind, nullptr, TRUE);
					}
					switch (outcome.result)
					{
					case biz::license::UnbindResult::Success:
						if (!outcome.newCode.empty())
						{
							// 服务端换机：展示换机码弹窗，含红色加粗提醒、5s 倒计时确定、一键复制二次确认。
							// 用户是否已确认关闭不影响后续流程（无论复制与否都视为解绑成功）。
							show_unbind_success_dialog(hDlg, outcome.newCode);
						}
						else
						{
							MessageBoxW(hDlg, L"解绑成功！本机已退出授权，应用即将关闭。", L"解绑本机", MB_OK | MB_ICONINFORMATION);
						}
						data.result = LicenseInfoResult::Unbound;
						DestroyWindow(hDlg);
						break;
					case biz::license::UnbindResult::OtherInstancesRunning:
						MessageBoxW(hDlg, L"检测到其他 eBox 进程正在运行。\n请先关闭所有 eBox 窗口和进程后再解绑。", L"解绑本机", MB_OK | MB_ICONWARNING);
						break;
					case biz::license::UnbindResult::ExceededLimit:
						MessageBoxW(hDlg,
							(data.unbindMax == 0
								? L"该激活码已设置禁止解绑。"
								: std::format(L"本月解绑次数已达上限（{} 次），请下月再试。", data.unbindMax).c_str()),
							L"解绑本机", MB_OK | MB_ICONWARNING);
						break;
					default:
						break;
					}
				}
				continue;
			}
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

	// ---- 常见问题对话框：左侧分类（常见问题 / 妙用小技巧），右侧内容展示 ----
	namespace
	{
		constexpr wchar_t FAQ_DLG_CLASS[] = L"eBoxFaqDialog";
		constexpr int FAQ_CAT_LIST_ID = 5001;       // 左侧大类列表
		constexpr int FAQ_CLOSE_ID = 5002;          // 底部"关闭"
		constexpr int FAQ_SERVICE_ID = 5003;        // 底部"联系客服"
		constexpr int FAQ_GUIDE_ID = 5004;          // 左侧"详细使用指南"
		constexpr int FAQ_WEBSITE_ID = 5005;        // 底部"访问官网"
		constexpr int FAQ_DLG_WIDTH = 760;
		constexpr int FAQ_DLG_HEIGHT = 540;
		constexpr wchar_t FAQ_GUIDE_URL[] = L"https://uac8b85dxgk.feishu.cn/wiki/RmH4wnRcAi15e6kdds6cxR3MnJP";
		// 右侧问题面板（手风琴式自绘）
		constexpr int FAQ_PANEL_X = 228;
		constexpr int FAQ_PANEL_Y = 98;
		constexpr int FAQ_PANEL_W = 502;
		constexpr int FAQ_PANEL_H = 364;
		constexpr int FAQ_PAD_X = 10;    // 面板左右内边距
		constexpr int FAQ_TITLE_H = 30;  // 每个问题标题行高

		struct FaqEntry
		{
			const wchar_t* title;
			const wchar_t* content;
		};

		struct FaqCategory
		{
			const wchar_t* name;
			const FaqEntry* entries;
			int count;
		};

		constexpr FaqEntry FAQ_QUESTIONS[] =
		{
			{L"Q1：同时最多能开几个企业微信？",
			 L"数量取决于电脑内存与磁盘空间，每个环境独立运行互不干扰；建议内存占用较高时用顶部看板观察，及时清理缓存。"},
			{L"Q2：为什么某个环境启动后没反应？",
			 L"展开该环境查看日志与进程记录，通常为首启加载较慢，稍等片刻；若日志出现“失败/错误”，复制日志发给客服。"},
			{L"Q3：换电脑后怎么继续用？",
			 L"先在旧电脑上【解绑本机】获取换机码 → 在新电脑安装 eBox → 用换机码激活即可（注意每月解绑次数限制）。"},
			{L"Q4：清理聊天记录会影响登录状态吗？",
			 L"不会，登录状态保留；但聊天记录本身会永久删除，请谨慎操作。"},
			{L"Q5：授权到期了还能打开软件吗？",
			 L"可以打开查看界面，但无法再启动环境、新增环境；需购买新码并在【授权】中续期。"},
			{L"Q6：断网了还能用吗？",
			 L"在线激活码在离线宽限期内可正常使用；激活后即使暂时离线，也能用完完整的授权期限。"},
			{L"Q7：全盘垃圾清理会删掉我的聊天记录吗？",
			 L"会。微信/企业微信/QQ/钉钉 的聊天图片、视频、文件在清理范围内（默认选中）。如不想删除，请在清理前手动备份，或仅使用“环境卡片 → 仅缓存”。"},
			{L"Q8：全盘清理会影响 eBox 环境的登录状态吗？",
			 L"不会。各环境的登录状态、注册表 hive、企业配置、设备指纹（machine_id / qimei）被白名单保护，绝不删除；只清理缓存目录与聊天消息库。清理后环境内重新启动企业微信即可正常使用。"},
			{L"Q9：清理时为什么要求关闭程序并需要管理员权限？",
			 L"关闭浏览器/聊天软件是为了释放正在被占用的缓存文件（否则删不掉）；管理员权限用于清理系统目录（Windows 更新缓存、错误报告、回收站等），清理过程会弹出 UAC 授权窗口，属正常现象。"},
			{L"Q10：易歪歪发送含图片的话术，图片发不出去？",
			 L"这是易歪歪安装位置导致的。如果易歪歪安装在 C 盘（尤其是“文档”等系统用户目录下），会被 eBox 的环境隔离重定向，多开的企业微信读取不到图片文件，所以只发出文字。\r\n\r\n"
			  L"解决办法：把易歪歪卸载后安装到 D 盘（或其他非 C 盘），重新打开即可正常发送图片。"},
		};

		constexpr FaqEntry FAQ_TIPS[] =
		{
			{L"快速定位账号",
			 L"当企业微信多个账号，想快速找到这个号时，从应用的卡片对应微信点【启动】，可快速弹出对应微信到前置窗口。"},
			{L"自动登录免扫码",
			 L"登录企业微信后，勾选自动登录，下次电脑重启或者退出登录等，只需点卡片的【启动】按钮，可快速启动并自动登录企业微信，无需扫码和验证码。"},
			{L"磁盘清理建议",
			 L"硬盘卡片饼状图如果超过 85% 以上建议清理，全盘清理或者单独个号清理，释放缓存空间，更流畅。"},
			{L"关闭指定企业微信",
			 L"想关闭指定企业微信，点【关闭】的结束进程即可。"},
			{L"卡片排序",
			 L"左侧微信号卡片支持点击长按鼠标左键，上下移动卡片排序。"},
			{L"列表视图",
			 L"微信号过多，可以在左侧切换列表视图，看起来更紧凑，看到更多环境。"},
			{L"及时更新",
			 L"有新版本，建议更新，以获得更好的使用体验，数据不会丢失，登录持久有效，放心更新。"},
		};

		constexpr FaqCategory FAQ_CATEGORIES[] =
		{
			{L"常见问题", FAQ_QUESTIONS, static_cast<int>(std::size(FAQ_QUESTIONS))},
			{L"妙用小技巧", FAQ_TIPS, static_cast<int>(std::size(FAQ_TIPS))},
		};

		struct FaqDialogData
		{
			HWND hwnd{nullptr};
			HWND hCatList{nullptr};       // 左侧大类列表
			HWND hGuide{nullptr};         // 左侧"详细使用指南"
			HWND hWebsite{nullptr};       // 底部"访问官网"
			HWND hService{nullptr};       // 底部"联系客服"
			HWND hClose{nullptr};         // 底部"关闭"
			HFONT hFontTitle{nullptr};    // 弹窗标题（19px 半粗）
			HFONT hFontSmall{nullptr};    // 副标题/底部提示（11px）
			HFONT hFontList{nullptr};     // 左侧大类列表（12px）
			HFONT hFontQTitle{nullptr};   // 问题标题（13px 半粗）
			HFONT hFontQContent{nullptr}; // 问题内容（12px 常规）
			HFONT hFontLink{nullptr};     // 底部《eBox 使用手册》超链接（11px 下划线）
			std::vector<int> contentH;    // 当前分类下每条展开内容的高度（px）
			int expandedIdx{0};           // 当前展开的问题索引（-1=全部收起）
			int scrollOffset{0};          // 面板垂直滚动偏移
			bool draggingThumb{false};    // 拖动滚动条中
			int dragStartY{0};
			int dragStartScroll{0};
			int dpi{96};
			// 布局度量（按 dpi 与客户区尺寸计算，见 faq_dlg_layout）
			int cardTop{0};
			int cardBottom{0};
			int panelX{0};
			int panelY{0};
			int panelW{0};
			int panelH{0};
			int padX{0};
			int titleH{0};
			int footerTop{0};
			RECT rcList{};
			RECT rcGuide{};
			RECT rcWebsite{};
			RECT rcService{};
			RECT rcClose{};
			bool done{false};
		};

		int faq_current_category(const FaqDialogData* data)
		{
			if (!data->hCatList)
			{
				return 0;
			}
			const int cat = static_cast<int>(SendMessageW(data->hCatList, LB_GETCURSEL, 0, 0));
			return (cat >= 0 && cat < static_cast<int>(std::size(FAQ_CATEGORIES))) ? cat : 0;
		}

		// 测量当前分类下每条问题展开后的内容高度（自动换行）
		void faq_measure_content(FaqDialogData* data)
		{
			if (!data->hwnd || !data->hFontQContent)
			{
				return;
			}
			const FaqCategory& c = FAQ_CATEGORIES[faq_current_category(data)];
			HDC hdc = GetDC(data->hwnd);
			HFONT hOld = static_cast<HFONT>(SelectObject(hdc, data->hFontQContent));
			data->contentH.clear();
			data->contentH.reserve(static_cast<std::size_t>(c.count));
			for (int i = 0; i < c.count; ++i)
			{
				// 与 faq_draw_panel 绘制内容区等宽，避免测量偏宽导致最后一行被裁剪
				RECT rc{0, 0, data->panelW - data->padX * 2 - ui::dlg_s(12, data->dpi), 0};
				DrawTextW(hdc, c.entries[i].content, -1, &rc, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
				data->contentH.push_back(rc.bottom - rc.top + ui::dlg_s(2, data->dpi));
			}
			SelectObject(hdc, hOld);
			ReleaseDC(data->hwnd, hdc);
		}

		int faq_panel_total_height(const FaqDialogData* data)
		{
			const FaqCategory& c = FAQ_CATEGORIES[faq_current_category(data)];
			const int gap = ui::dlg_s(10, data->dpi);
			int total = 0;
			for (int i = 0; i < c.count; ++i)
			{
				total += data->titleH + 1;
				if (i == data->expandedIdx && static_cast<std::size_t>(i) < data->contentH.size())
				{
					total += data->contentH[static_cast<std::size_t>(i)] + gap;
				}
			}
			return total;
		}

		int faq_panel_max_scroll(const FaqDialogData* data)
		{
			return std::max(faq_panel_total_height(data) - data->panelH, 0);
		}

		// 命中测试：点击坐标 → 问题索引（标题行内），-1 表示空白/内容区
		int faq_panel_hit_test(const FaqDialogData* data, POINT pt)
		{
			if (pt.x < data->panelX || pt.x >= data->panelX + data->panelW ||
			    pt.y < data->panelY || pt.y >= data->panelY + data->panelH)
			{
				return -1;
			}
			const FaqCategory& c = FAQ_CATEGORIES[faq_current_category(data)];
			const int gap = ui::dlg_s(10, data->dpi);
			int y = data->panelY + ui::dlg_s(6, data->dpi) - data->scrollOffset;
			for (int i = 0; i < c.count; ++i)
			{
				if (pt.y >= y && pt.y < y + data->titleH)
				{
					return i;
				}
				y += data->titleH + 1;
				if (i == data->expandedIdx && static_cast<std::size_t>(i) < data->contentH.size())
				{
					y += data->contentH[static_cast<std::size_t>(i)] + gap;
				}
			}
			return -1;
		}

		// 右侧滚动条 thumb 矩形（内容超出可视时才显示）
		bool faq_panel_thumb_rect(const FaqDialogData* data, RECT& rcOut)
		{
			const int total = faq_panel_total_height(data);
			const int maxScroll = faq_panel_max_scroll(data);
			if (total <= data->panelH)
			{
				return false;
			}
			const auto S = [dpi = data->dpi](int value) { return ui::dlg_s(value, dpi); };
			const int thumbH = std::max(data->panelH * data->panelH / total, S(20));
			const int trackH = data->panelH - thumbH;
			const int thumbY = data->panelY + (maxScroll > 0 ? trackH * data->scrollOffset / maxScroll : 0);
			rcOut = RECT{data->panelX + data->panelW - S(6), thumbY, data->panelX + data->panelW - S(2), thumbY + thumbH};
			return true;
		}

		// 底部《eBox 使用手册》超链接命中区域
		RECT faq_hint_link_rect(const FaqDialogData* data)
		{
			constexpr wchar_t PREFIX[] = L"更多问题请查看";
			constexpr wchar_t LINK[] = L"《eBox 使用手册》";
			const auto S = [dpi = data->dpi](int value) { return ui::dlg_s(value, dpi); };
			HDC hdc = GetDC(data->hwnd);
			HFONT hOld = static_cast<HFONT>(SelectObject(hdc, data->hFontSmall));
			SIZE s1{};
			GetTextExtentPoint32W(hdc, PREFIX, static_cast<int>(std::wcslen(PREFIX)), &s1);
			SIZE s2{};
			GetTextExtentPoint32W(hdc, LINK, static_cast<int>(std::wcslen(LINK)), &s2);
			SelectObject(hdc, hOld);
			ReleaseDC(data->hwnd, hdc);
			return RECT{S(28) + s1.cx, data->footerTop + S(4), S(28) + s1.cx + s2.cx, data->footerTop + S(26)};
		}

		// 浅色系按钮：自定义填充/描边/文字色
		void faq_draw_light_button(HDC hdc, const RECT& rc, bool hover, bool pressed, LPCWSTR text,
		                           COLORREF fill, COLORREF hoverFill, COLORREF pressedFill, COLORREF border, COLORREF textColor,
		                           int radius = 8)
		{
			const COLORREF f = pressed ? pressedFill : (hover ? hoverFill : fill);
			draw_round_rect(hdc, rc, radius, f, border, 1);
			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, textColor);
			DrawTextW(hdc, text, -1, const_cast<RECT*>(&rc), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		}

		// 右侧问题面板：手风琴式（标题行 + 点击展开内容 + 三角箭头 + 分割线）
		void faq_draw_panel(HDC hdc, const FaqDialogData* data)
		{
			const FaqCategory& c = FAQ_CATEGORIES[faq_current_category(data)];
			const auto S = [dpi = data->dpi](int value) { return ui::dlg_s(value, dpi); };
			const int panelX = data->panelX;
			const int panelY = data->panelY;
			const int panelW = data->panelW;
			const int panelH = data->panelH;
			const int padX = data->padX;
			const int titleH = data->titleH;
			const int gap = S(10);

			const int saved = SaveDC(hdc);
			IntersectClipRect(hdc, panelX, panelY, panelX + panelW, panelY + panelH);

			int y = panelY + S(6) - data->scrollOffset;
			const COLORREF accent = RGB(0x00, 0x78, 0xd4);
			for (int i = 0; i < c.count; ++i)
			{
				const FaqEntry& e = c.entries[i];
				const bool expanded = (i == data->expandedIdx);
				const int titleTop = y;
				const int titleBottom = y + titleH;

				if (titleTop < panelY + panelH && titleBottom > panelY)
				{
					// 三角箭头（最右侧）：展开 ▾ / 收起 ▸
					HFONT hOld = static_cast<HFONT>(SelectObject(hdc, data->hFontQContent));
					SetBkMode(hdc, TRANSPARENT);
					SetTextColor(hdc, expanded ? accent : RGB(0x9a, 0xa3, 0xaf));
					RECT rcArrow{panelX + panelW - padX - S(18), titleTop, panelX + panelW - padX, titleBottom};
					DrawTextW(hdc, expanded ? L"▾" : L"▸", -1, &rcArrow, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

					// 标题：展开时主题蓝、收起时深色；半粗，字号更大
					SelectObject(hdc, data->hFontQTitle);
					SetTextColor(hdc, expanded ? accent : RGB(0x1f, 0x29, 0x37));
					RECT rcTitle{panelX + padX, titleTop, panelX + panelW - padX - S(22), titleBottom};
					DrawTextW(hdc, e.title, -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
					SelectObject(hdc, hOld);

					// 分割线（标题行下方）
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xe8, 0xee, 0xf6));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					MoveToEx(hdc, panelX + padX, y + titleH, nullptr);
					LineTo(hdc, panelX + panelW - padX, y + titleH);
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
				}
				y += titleH + 1;

				if (expanded && static_cast<std::size_t>(i) < data->contentH.size())
				{
					const int contentH = data->contentH[static_cast<std::size_t>(i)];
					if (y < panelY + panelH)
					{
						HFONT hOld = static_cast<HFONT>(SelectObject(hdc, data->hFontQContent));
						SetBkMode(hdc, TRANSPARENT);
						SetTextColor(hdc, RGB(0x5a, 0x64, 0x72));
						RECT rcContent{panelX + padX + S(6), y, panelX + panelW - padX - S(6), y + contentH};
						DrawTextW(hdc, e.content, -1, &rcContent, DT_LEFT | DT_WORDBREAK);
						SelectObject(hdc, hOld);
					}
					y += contentH + gap;
				}
			}

			RestoreDC(hdc, saved);

			// 滚动条 thumb
			RECT rcThumb{};
			if (faq_panel_thumb_rect(data, rcThumb))
			{
				HBRUSH hBrush = CreateSolidBrush(RGB(0xcf, 0xd6, 0xe0));
				HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hBrush));
				HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, GetStockObject(NULL_PEN)));
				RoundRect(hdc, rcThumb.left, rcThumb.top, rcThumb.right, rcThumb.bottom, S(4), S(4));
				SelectObject(hdc, hOldBr);
				SelectObject(hdc, hOldPen);
				DeleteObject(hBrush);
			}
		}

		// 切换大类：重置展开状态与滚动，并重新测量内容高度
		void faq_apply_category(FaqDialogData* data, int cat)
		{
			if (cat < 0 || cat >= static_cast<int>(std::size(FAQ_CATEGORIES)))
			{
				return;
			}
			data->expandedIdx = 0;
			data->scrollOffset = 0;
			faq_measure_content(data);
			if (data->hwnd)
			{
				InvalidateRect(data->hwnd, nullptr, FALSE);
			}
		}

		void faq_dlg_create_fonts(FaqDialogData& data)
		{
			auto release = [](HFONT& font)
			{
				if (font)
				{
					DeleteObject(font);
					font = nullptr;
				}
			};
			release(data.hFontTitle);
			release(data.hFontSmall);
			release(data.hFontList);
			release(data.hFontQTitle);
			release(data.hFontQContent);
			release(data.hFontLink);
			const int dpi = data.dpi > 0 ? data.dpi : 96;
			data.hFontTitle = CreateFontW(-MulDiv(19, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontSmall = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontList = CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontQTitle = CreateFontW(-MulDiv(13, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontQContent = CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                                 CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontLink = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			if (data.hCatList)
			{
				SendMessageW(data.hCatList, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontList), TRUE);
			}
		}

		// 全部子控件与自绘面板的度量按 dpi 与客户区尺寸计算
		void faq_dlg_layout(FaqDialogData& data)
		{
			if (!data.hwnd)
			{
				return;
			}
			const auto S = [dpi = data.dpi](int value) { return ui::dlg_s(value, dpi); };
			RECT client{};
			GetClientRect(data.hwnd, &client);
			const int width = client.right;
			const int height = client.bottom;
			const int left = S(24);
			const int right = width - S(24);
			const auto place = [](HWND control, const RECT& rc)
			{
				if (control)
				{
					SetWindowPos(control, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
					             SWP_NOZORDER | SWP_NOACTIVATE);
				}
			};

			// 左右白卡片区域
			data.cardTop = S(92);
			data.cardBottom = height - S(70);
			const int catCardRight = left + S(186);

			// 左侧大类列表 + 详细使用指南按钮（贴卡片底部）
			data.rcList = RECT{left + S(6), data.cardTop + S(8), catCardRight - S(6), data.cardBottom - S(56)};
			data.rcGuide = RECT{left + S(6), data.cardBottom - S(46), catCardRight - S(6), data.cardBottom - S(10)};
			place(data.hCatList, data.rcList);

			// 右侧问题面板
			data.panelX = catCardRight + S(18);
			data.panelW = std::max(S(160), right - S(6) - data.panelX);
			data.panelY = data.cardTop + S(6);
			data.panelH = std::max(S(120), data.cardBottom - S(8) - data.panelY);
			data.padX = S(10);
			data.titleH = S(30);

			// 底部按钮行（右对齐）
			const int btnH = S(36);
			const int btnTop = height - S(18) - btnH;
			data.footerTop = btnTop;
			const int closeW = S(104);
			const int sideW = S(96);
			data.rcClose = RECT{right - closeW, btnTop, right, btnTop + btnH};
			data.rcService = RECT{data.rcClose.left - S(12) - sideW, btnTop, data.rcClose.left - S(12), btnTop + btnH};
			data.rcWebsite = RECT{data.rcService.left - S(12) - sideW, btnTop, data.rcService.left - S(12), btnTop + btnH};
			place(data.hGuide, data.rcGuide);
			place(data.hWebsite, data.rcWebsite);
			place(data.hService, data.rcService);
			place(data.hClose, data.rcClose);

			data.scrollOffset = std::clamp(data.scrollOffset, 0, faq_panel_max_scroll(&data));
		}

		LRESULT CALLBACK faq_dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* data = reinterpret_cast<FaqDialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			switch (msg)
			{
			case WM_CREATE:
			{
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
				data = static_cast<FaqDialogData*>(cs->lpCreateParams);
				data->hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

				const HMODULE hInst = GetModuleHandleW(nullptr);
				data->dpi = ui::dlg_dpi(hwnd);
				faq_dlg_create_fonts(*data);

				// 左侧大类列表（常见问题 / 妙用小技巧）
				data->hCatList = CreateWindowExW(0, WC_LISTBOXW, L"",
				                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
				                                 0, 0, 0, 0, hwnd,
				                                 reinterpret_cast<HMENU>(static_cast<std::intptr_t>(FAQ_CAT_LIST_ID)), hInst, nullptr);
				SendMessageW(data->hCatList, WM_SETFONT, reinterpret_cast<WPARAM>(data->hFontList), TRUE);
				for (int c = 0; c < static_cast<int>(std::size(FAQ_CATEGORIES)); ++c)
				{
					SendMessageW(data->hCatList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(FAQ_CATEGORIES[c].name));
				}
				SendMessageW(data->hCatList, LB_SETCURSEL, 0, 0);

				// 左侧"详细使用指南"按钮：打开飞书文档
				data->hGuide = CreateWindowExW(0, L"BUTTON", L"详细使用指南",
				                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                               0, 0, 0, 0, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(FAQ_GUIDE_ID)), hInst, nullptr);

				// 底部"访问官网"按钮：打开官网首页
				data->hWebsite = CreateWindowExW(0, L"BUTTON", L"访问官网",
				                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                                 0, 0, 0, 0, hwnd,
				                                 reinterpret_cast<HMENU>(static_cast<std::intptr_t>(FAQ_WEBSITE_ID)), hInst, nullptr);
				// 底部"联系客服"按钮（与授权页同一微信客服链接）
				data->hService = CreateWindowExW(0, L"BUTTON", L"联系客服",
				                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
				                                 0, 0, 0, 0, hwnd,
				                                 reinterpret_cast<HMENU>(static_cast<std::intptr_t>(FAQ_SERVICE_ID)), hInst, nullptr);
				// 底部"关闭"按钮
				data->hClose = CreateWindowExW(0, L"BUTTON", L"关闭",
				                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
				                               0, 0, 0, 0, hwnd,
				                               reinterpret_cast<HMENU>(static_cast<std::intptr_t>(FAQ_CLOSE_ID)), hInst, nullptr);
				faq_dlg_layout(*data);
				return 0;
			}
			case WM_DPICHANGED:
			{
				if (data)
				{
					data->dpi = ui::dlg_on_dpi_changed(hwnd, lParam);
					faq_dlg_create_fonts(*data);
					faq_dlg_layout(*data);
					faq_measure_content(data);
					InvalidateRect(hwnd, nullptr, TRUE);
				}
				return 0;
			}
			case WM_SIZE:
				// 创建后校正客户区尺寸会触发 WM_SIZE，需按最终客户区重新摆放控件
				if (data && data->hwnd && wParam != SIZE_MINIMIZED)
				{
					faq_dlg_layout(*data);
					faq_measure_content(data);
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				return 0;
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				HDC hdc = BeginPaint(hwnd, &ps);
				RECT rcClient{};
				GetClientRect(hwnd, &rcClient);
				const auto S = [dpi = data->dpi](int value) { return ui::dlg_s(value, dpi); };
				const int clientRight = rcClient.right;
				// 背景：浅蓝 → 白 垂直渐变
				fill_v_gradient(hdc, rcClient, RGB(0xee, 0xf5, 0xfe), RGB(0xff, 0xff, 0xff));

				// 标题
				{
					SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
					SetBkMode(hdc, TRANSPARENT);
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontTitle));
					RECT rcTitle{S(24), S(22), clientRight - S(24), S(50)};
					DrawTextW(hdc, L"常见问题", -1, &rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SelectObject(hdc, hOldFont);
				}
				// 副标题
				{
					SetTextColor(hdc, CLEAN_TEXT_SUB);
					SetBkMode(hdc, TRANSPARENT);
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontSmall));
					RECT rcSub{S(24), S(52), clientRight - S(24), S(72)};
					DrawTextW(hdc, L"使用帮助 · 常见问题 · 妙用小技巧", -1, &rcSub, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SelectObject(hdc, hOldFont);
				}
				// 分隔线
				{
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xe8, 0xee, 0xf6));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					MoveToEx(hdc, S(24), S(80), nullptr);
					LineTo(hdc, clientRight - S(24), S(80));
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
				}
				// 左侧大类白卡片 + 右侧问题白卡片
				{
					RECT rcCatCard{S(24), data->cardTop, S(24) + S(186), data->cardBottom};
					draw_round_rect(hdc, rcCatCard, S(10), RGB(0xff, 0xff, 0xff), RGB(0xe8, 0xee, 0xf6), 1);
					RECT rcEditCard{data->panelX - S(6), data->cardTop, data->panelX + data->panelW + S(6), data->cardBottom};
					draw_round_rect(hdc, rcEditCard, S(10), RGB(0xff, 0xff, 0xff), RGB(0xe8, 0xee, 0xf6), 1);
				}
				// 右侧手风琴问题面板
				faq_draw_panel(hdc, data);
				// 底部提示（《eBox 使用手册》为蓝色超链接，点击打开飞书指南文档）
				{
					SetTextColor(hdc, CLEAN_TEXT_SUB);
					SetBkMode(hdc, TRANSPARENT);
					HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontSmall));
					RECT rcPrefix{S(28), data->footerTop + S(4), S(420), data->footerTop + S(26)};
					DrawTextW(hdc, L"更多问题请查看", -1, &rcPrefix, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SelectObject(hdc, data->hFontLink);
					SetTextColor(hdc, RGB(0x00, 0x78, 0xd4));
					RECT rcLink = faq_hint_link_rect(data);
					DrawTextW(hdc, L"《eBox 使用手册》", -1, &rcLink, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SetTextColor(hdc, CLEAN_TEXT_SUB);
					SelectObject(hdc, data->hFontSmall);
					const RECT rcTail = RECT{rcLink.right, data->footerTop + S(4), S(420) + rcLink.right, data->footerTop + S(26)};
					DrawTextW(hdc, L" 或联系客服", -1, const_cast<RECT*>(&rcTail), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SelectObject(hdc, hOldFont);
				}
				EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_CTLCOLORSTATIC:
			case WM_CTLCOLOREDIT:
			case WM_CTLCOLORLISTBOX:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetBkColor(hdc, RGB(0xff, 0xff, 0xff));
				SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
				return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
			}
			case WM_DRAWITEM:
			{
				const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
				const int id = dis->CtlID;
				if (id == FAQ_CLOSE_ID || id == FAQ_SERVICE_ID || id == FAQ_GUIDE_ID || id == FAQ_WEBSITE_ID)
				{
					const bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
					const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
					wchar_t buf[64]{};
					GetWindowTextW(dis->hwndItem, buf, 64);
					if (id == FAQ_GUIDE_ID)
					{
						// 详细使用指南：浅色系（白底浅灰描边）
						faq_draw_light_button(dis->hDC, dis->rcItem, hover, pressed, buf,
						                      RGB(0xff, 0xff, 0xff), RGB(0xf2, 0xf6, 0xfb), RGB(0xe6, 0xee, 0xf7),
						                      RGB(0xcf, 0xd8, 0xe3), RGB(0x3a, 0x46, 0x55), ui::dlg_s(8, data->dpi));
					}
					else if (id == FAQ_WEBSITE_ID)
					{
						// 访问官网：浅蓝色按钮
						faq_draw_light_button(dis->hDC, dis->rcItem, hover, pressed, buf,
						                      RGB(0xec, 0xf4, 0xfd), RGB(0xdd, 0xec, 0xfb), RGB(0xcd, 0xe3, 0xf8),
						                      RGB(0x9c, 0xc8, 0xee), RGB(0x0a, 0x6c, 0xbd), ui::dlg_s(8, data->dpi));
					}
					else if (id == FAQ_SERVICE_ID)
					{
						// 联系客服：浅绿色按钮
						faq_draw_light_button(dis->hDC, dis->rcItem, hover, pressed, buf,
						                      RGB(0xe8, 0xf7, 0xee), RGB(0xd7, 0xf0, 0xe2), RGB(0xc5, 0xe9, 0xd4),
						                      RGB(0x8f, 0xd0, 0xac), RGB(0x18, 0x8a, 0x4f), ui::dlg_s(8, data->dpi));
					}
					else
					{
						// 关闭按钮为主按钮（蓝）
						draw_modern_dlg_button(dis->hDC, dis->rcItem, true, hover, pressed, buf);
					}
					return TRUE;
				}
				return FALSE;
			}
			case WM_COMMAND:
			{
				const int id = LOWORD(wParam);
				const int code = HIWORD(wParam);
				if (id == FAQ_CLOSE_ID)
				{
					DestroyWindow(hwnd);
					return 0;
				}
				if (id == FAQ_SERVICE_ID)
				{
					// 与授权信息页"联系客服"同一微信客服链接
					if (!MainApp::kServiceUrl.empty())
					{
						ShellExecuteW(nullptr, L"open", std::wstring{MainApp::kServiceUrl}.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
					return 0;
				}
				if (id == FAQ_GUIDE_ID)
				{
					// 详细使用指南：飞书文档，用系统默认浏览器打开
					ShellExecuteW(nullptr, L"open", FAQ_GUIDE_URL, nullptr, nullptr, SW_SHOWNORMAL);
					return 0;
				}
				if (id == FAQ_WEBSITE_ID)
				{
					// 访问官网：用系统默认浏览器打开官网首页
					if (!MainApp::kWebsiteUrl.empty())
					{
						ShellExecuteW(nullptr, L"open", std::wstring{MainApp::kWebsiteUrl}.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
					}
					return 0;
				}
				if (code == LBN_SELCHANGE)
				{
					if (id == FAQ_CAT_LIST_ID)
					{
						const int cat = static_cast<int>(SendMessageW(data->hCatList, LB_GETCURSEL, 0, 0));
						if (cat >= 0)
						{
							faq_apply_category(data, cat);
						}
					}
				}
				return 0;
			}
			case WM_LBUTTONDOWN:
			{
				const POINT pt{static_cast<LONG>(LOWORD(lParam)), static_cast<LONG>(HIWORD(lParam))};
				// 底部《eBox 使用手册》超链接 → 打开飞书指南文档
				const RECT rcLink = faq_hint_link_rect(data);
				if (pt.x >= rcLink.left && pt.x <= rcLink.right && pt.y >= rcLink.top && pt.y <= rcLink.bottom)
				{
					ShellExecuteW(hwnd, L"open", FAQ_GUIDE_URL, nullptr, nullptr, SW_SHOWNORMAL);
					return 0;
				}
				RECT rcThumb{};
				if (faq_panel_thumb_rect(data, rcThumb))
				{
					if (pt.x >= rcThumb.left - 4 && pt.x <= rcThumb.right + 4 &&
					    pt.y >= rcThumb.top && pt.y <= rcThumb.bottom)
					{
						data->draggingThumb = true;
						data->dragStartY = pt.y;
						data->dragStartScroll = data->scrollOffset;
						SetCapture(hwnd);
						return 0;
					}
				}
				const int idx = faq_panel_hit_test(data, pt);
				if (idx >= 0)
				{
					// 再点一次已展开的问题则收起，否则展开该问题
					data->expandedIdx = (data->expandedIdx == idx) ? -1 : idx;
					// 展开/收起后收紧滚动位置，避免停留在空区造成跳动感
					data->scrollOffset = std::clamp(data->scrollOffset, 0, faq_panel_max_scroll(data));
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				return 0;
			}
			case WM_SETCURSOR:
			{
				if (LOWORD(lParam) == HTCLIENT)
				{
					POINT pt{};
					GetCursorPos(&pt);
					ScreenToClient(hwnd, &pt);
					const RECT rcLink = faq_hint_link_rect(data);
					if (pt.x >= rcLink.left && pt.x <= rcLink.right && pt.y >= rcLink.top && pt.y <= rcLink.bottom)
					{
						SetCursor(LoadCursorW(nullptr, IDC_HAND));
						return TRUE;
					}
				}
				return DefWindowProcW(hwnd, msg, wParam, lParam);
			}
			case WM_LBUTTONUP:
				if (data->draggingThumb)
				{
					data->draggingThumb = false;
					ReleaseCapture();
				}
				return 0;
			case WM_MOUSEMOVE:
				if (data->draggingThumb)
				{
					const int y = static_cast<int>(HIWORD(lParam));
					const int total = faq_panel_total_height(data);
					const int maxScroll = faq_panel_max_scroll(data);
					if (maxScroll > 0)
					{
						const int thumbH = std::max(data->panelH * data->panelH / total, ui::dlg_s(20, data->dpi));
						const int trackH = data->panelH - thumbH;
						const int newScroll = data->dragStartScroll +
						                      (trackH > 0 ? (y - data->dragStartY) * maxScroll / trackH : 0);
						data->scrollOffset = std::clamp(newScroll, 0, maxScroll);
						InvalidateRect(hwnd, nullptr, FALSE);
					}
				}
				return 0;
			case WM_MOUSEWHEEL:
			{
				const int maxScroll = faq_panel_max_scroll(data);
				if (maxScroll > 0)
				{
					const int delta = static_cast<int>(GET_WHEEL_DELTA_WPARAM(wParam));
					// 平滑滚动：每格滚一屏约 1/4，并限制在有效范围内
					const int step = std::max(ui::dlg_s(24, data->dpi), data->panelH / 4);
					int newOffset = data->scrollOffset - (delta > 0 ? step : -step);
					newOffset = std::clamp(newOffset, 0, maxScroll);
					if (newOffset != data->scrollOffset)
					{
						data->scrollOffset = newOffset;
						// 只重绘面板区域，避免整窗重绘造成抖动闪烁
						RECT rcPanel{data->panelX, data->panelY, data->panelX + data->panelW, data->panelY + data->panelH};
						InvalidateRect(hwnd, &rcPanel, FALSE);
					}
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
				if (data->hFontSmall)
				{
					DeleteObject(data->hFontSmall);
				}
				if (data->hFontList)
				{
					DeleteObject(data->hFontList);
				}
				if (data->hFontQTitle)
				{
					DeleteObject(data->hFontQTitle);
				}
				if (data->hFontQContent)
				{
					DeleteObject(data->hFontQContent);
				}
				if (data->hFontLink)
				{
					DeleteObject(data->hFontLink);
				}
				data->done = true;
				return 0;
			default:
				break;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}

	void faq_dialog(const WindowBase* owner)
	{
		static const bool classRegistered = []()
		{
			WNDCLASSEXW wc = {sizeof(wc)};
			wc.lpfnWndProc = faq_dlg_proc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
			wc.lpszClassName = FAQ_DLG_CLASS;
			return RegisterClassExW(&wc) != 0;
		}();
		(void)classRegistered;

		FaqDialogData data;
		HWND hOwner = owner ? owner->nativeHandle() : nullptr;
		const SIZE clientSize = ui::dlg_client_size(hOwner, FAQ_DLG_WIDTH, FAQ_DLG_HEIGHT);
		const HWND hDlg = CreateWindowExW(0, FAQ_DLG_CLASS, L"常见问题",
		                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
		                                  0, 0, clientSize.cx, clientSize.cy, hOwner, nullptr,
		                                  GetModuleHandleW(nullptr), &data);
		if (!hDlg)
		{
			return;
		}
		ui::dlg_fit_client(hDlg, clientSize);
		ui::dlg_center_on(hDlg, hOwner);
		ShowWindow(hDlg, SW_SHOW);
		// 默认展示"常见问题"分类，展开第一条
		faq_apply_category(&data, 0);

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
	}

	// ---- 选择要启动的应用对话框：扫描系统应用 → 列表（序号/名称/路径/选择），分页，记忆上次选择 ----
	namespace
	{
		constexpr wchar_t APP_DLG_CLASS[] = L"eBoxAppPickDialog";
		constexpr int APP_PREV_ID = 6002;    // 上一页
		constexpr int APP_NEXT_ID = 6003;     // 下一页
		constexpr int APP_MANUAL_ID = 6004;   // 手动选择文件
		constexpr int APP_CANCEL_ID = 6005;   // 取消
		constexpr int APP_REFRESH_ID = 6006;  // 重新扫描应用
		constexpr int APP_SEARCH_ID = 6007;   // 综合搜索框（EDIT）
		constexpr int APP_SEARCH_HINT_ID = 6008; // 搜索框占位提示（STATIC）
		constexpr UINT_PTR APP_REFRESH_TIMER = 1; // 刷新动画定时器
		constexpr UINT APP_WM_SCAN_DONE = WM_APP + 0x31; // 后台扫描完成（线程消息，结果经 AppScanShared 交接，消息不携带指针）
		constexpr int APP_DLG_WIDTH = 700;
		constexpr int APP_DLG_HEIGHT = 560;
		constexpr int APP_PAGE_SIZE = 6;      // 每页 6 条
		constexpr int APP_ROW_H = 40;
		constexpr int APP_LIST_LEFT = 24;
		constexpr int APP_LIST_RIGHT = APP_DLG_WIDTH - 24;
		constexpr int APP_ROWS_TOP = 154;
		constexpr int APP_BTN_Y = 496;
		constexpr int APP_BTN_H = 32;
		constexpr std::wstring_view APP_REG_SUBKEY{L"Software\\eBox"};
		constexpr std::wstring_view APP_REG_LASTAPP{L"LastAppPath"};
		constexpr std::wstring_view APP_REG_PINS{L"PinnedApps"};
		constexpr int APP_MAX_PINS = 3;       // 最多置顶 3 个应用

		struct AppEntry
		{
			std::wstring name;
			std::wstring path;
			bool im = false;     // IM/通讯类应用（企业微信/微信/钉钉…）优先展示
			bool pinned = false; // 用户置顶
			int prio = 2;        // 0=企业微信（固定第 1），1=微信（固定第 2），2=其他
		};

		// 企业微信 / 微信 固定排在列表最前（第 1、2 位），不被任何置顶/IM 排序影响
		int app_prio(const std::wstring& nameLower, const std::wstring& fileLower)
		{
			if (nameLower.find(L"企业微信") != std::wstring::npos ||
			    nameLower.find(L"wechatwork") != std::wstring::npos ||
			    nameLower.find(L"wecom") != std::wstring::npos ||
			    fileLower.find(L"wxwork") != std::wstring::npos)
			{
				return 0;
			}
			if (nameLower.find(L"微信") != std::wstring::npos ||
			    nameLower.find(L"wechat") != std::wstring::npos ||
			    fileLower.find(L"wechat") != std::wstring::npos)
			{
				return 1;
			}
			return 2;
		}

		std::wstring to_lower(std::wstring s)
		{
			std::transform(s.begin(), s.end(), s.begin(),
			               [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			return s;
		}

		bool app_path_exists(const std::wstring& p)
		{
			std::error_code ec;
			return !p.empty() && std::filesystem::exists(std::filesystem::path{p}, ec) && !ec;
		}

		// 辅助进程/安装器，不列入应用列表
		bool is_helper_exe(const std::wstring& nameLower)
		{
			static constexpr std::wstring_view bad[] = {
				L"unins", L"uninstall", L"installer", L"setup", L"update", L"updater",
				L"crash", L"helper", L"repair", L"wizard", L"redist",
			};
			for (const auto b : bad)
			{
				if (nameLower.find(b) != std::wstring::npos)
				{
					return true;
				}
			}
			return false;
		}

		bool is_im_app(const std::wstring& nameLower)
		{
			static constexpr std::wstring_view im[] = {
				L"企业微信", L"wechatwork", L"wxwork", L"微信", L"wechat", L"钉钉", L"dingtalk",
				L"qq", L"tim", L"飞书", L"feishu", L"lark", L"whatsapp", L"telegram",
			};
			for (const auto s : im)
			{
				if (nameLower.find(s) != std::wstring::npos)
				{
					return true;
				}
			}
			return false;
		}

		// 开发/运行环境类程序（node.js、Python、Git、SDK 等），不属于"普通应用程序"，从列表排除
		bool is_tool_exe(const std::wstring& stemLower)
		{
			static constexpr std::wstring_view tools[] = {
				L"node", L"nodejs", L"npm", L"npx", L"yarn", L"pnpm", L"cnpm", L"bun", L"deno",
				L"python", L"pythonw", L"pip", L"pip3", L"conda", L"jupyter", L"ipython",
				L"git", L"git-bash", L"git-cmd", L"gitk", L"bash", L"sh", L"cmd", L"pwsh", L"powershell",
				L"msbuild", L"cmake", L"ninja", L"make", L"gcc", L"g++", L"clang", L"clang++", L"gdb",
				L"java", L"javaw", L"javac", L"mvn", L"gradle", L"kotlin", L"scala", L"go", L"gopls",
				L"php", L"composer", L"ruby", L"gem", L"perl", L"dotnet", L"flutter", L"dart",
				L"adb", L"fastboot", L"wsl", L"msys", L"vcpkg", L"conan", L"erl", L"elixir",
				// 控制台版压缩工具（GUI 版如 WinRAR.exe/7zFM.exe 不受影响）
				L"7z", L"7za", L"rar", L"unrar",
			};
			for (const auto t : tools)
			{
				if (stemLower == t)
				{
					return true;
				}
			}
			// 版本化命名：python3.11 / pip3.x / node20 等
			if (stemLower.starts_with(L"python") || stemLower.starts_with(L"pip"))
			{
				return true;
			}
			return false;
		}

		bool is_tool_display_name(const std::wstring& nameLower)
		{
			static constexpr std::wstring_view badNames[] = {
				L"node.js", L"python", L"anaconda", L"miniconda", L"jdk", L"openjdk", L"jre", L"java",
				L"sdk", L"redistributable", L"visual c++", L"mingw", L"msys", L"cmake",
				L"build tools", L"powershell", L"dotnet", L"vcpkg", L"conan", L"wsl", L"git for windows",
			};
			for (const auto b : badNames)
			{
				if (nameLower.find(b) != std::wstring::npos)
				{
					return true;
				}
			}
			// "Git" 单独精确词匹配（避免误伤 GitHub / GitKraken）
			std::size_t pos = 0;
			while (pos <= nameLower.size())
			{
				const std::size_t end = nameLower.find_first_of(L" .,-_()[]{}", pos);
				const std::size_t len = (end == std::wstring::npos) ? nameLower.size() - pos : end - pos;
				if (len == 3 && nameLower.compare(pos, 3, L"git") == 0)
				{
					return true;
				}
				if (end == std::wstring::npos)
				{
					break;
				}
				pos = end + 1;
			}
			return false;
		}

		// 系统组件/过时组件/系统小工具（Edge WebView2、Flash、IE、放大镜、数学输入面板、
		// 内存诊断、管理工具、Visual Basic/Studio 等）不属于"普通应用程序"，从列表排除
		bool is_junk_app(const std::wstring& nameLower, const std::wstring& stemLower)
		{
			static constexpr std::wstring_view badNames[] = {
				L"adobe flash", L"internet explorer", L"microsoft edge", L"webview2",
				L"magnify", L"放大镜", L"math input panel", L"数学输入面板",
				L"memory diagnostic", L"内存诊断", L"administrative tools", L"管理工具",
				L"visual basic", L"visual studio",
				L"resource monitor", L"snipping tool", L"speech recognition", L"steps recorder",
				L"system configuration", L"system information", L"windows fax and scan",
				L"windows media player", L"iscsi initiator", L"soda downloader",
				L"sandboxie", L"开始菜单",
				// Windows 自带系统工具（开始菜单快捷方式友好名）
				L"disk cleanup", L"磁盘清理", L"odbc data sources", L"on-screen keyboard", L"屏幕键盘",
				L"quick assist", L"快速助手", L"recovery drive", L"恢复驱动器",
				L"registry editor", L"注册表编辑器", L"remote desktop connection", L"远程桌面连接",
				L"narrator", L"讲述人", L"character map", L"字符映射表", L"windows easy transfer",
			};
			for (const auto b : badNames)
			{
				if (nameLower.find(b) != std::wstring::npos)
				{
					return true;
				}
			}
			static constexpr std::wstring_view badStems[] = {
				L"msedge", L"iexplore", L"magnify", L"mip", L"mdsched", L"flashplayer",
				L"snippingtool", L"psr", L"msconfig", L"msinfo32", L"wfs", L"wmplayer",
				L"iscsicpl", L"sodadownloader", L"mpcmdrun", L"wordpad", L"wmlaunch",
				// Windows 自带系统工具 / Defender / IE 残留 / Sandboxie 服务
				L"cleanmgr", L"narrator", L"odbcad32", L"osk", L"quickassist", L"recoverydrive",
				L"regedit", L"mstsc", L"msmpeng", L"mssense", L"nissrv", L"sbiesvc", L"edrservice",
				L"kmdutil", L"dumpuper", L"extexport", L"iediagcmd", L"ieinstal", L"ielowutil",
				// Firefox 自带辅助进程（主程序 firefox.exe 保留）
				L"default-browser-agent", L"maintenanceservice", L"maintenanceservice_installer",
				L"nmhproxy", L"pingsender", L"plugin-container", L"private_browsing",
			};
			for (const auto s : badStems)
			{
				if (stemLower == s)
				{
					return true;
				}
			}
			// Windows Media Player 系列系统工具（wmlaunch/wmpconfig/wmpnetwk/wmpnscfg/wmprph/wmpshare 等）
			if (stemLower.starts_with(L"wmp"))
			{
				return true;
			}
			// Windows Defender ATP 全家（SenseAP/SenseCM/SenseNdr/SenseSampleUpload 等）
			if (stemLower.starts_with(L"sense"))
			{
				return true;
			}
			// 前缀匹配：filequarant* 等
			if (stemLower.starts_with(L"filequarant"))
			{
				return true;
			}
			return false;
		}

		void app_try_add(std::vector<AppEntry>& out, std::wstring name, std::wstring path)
		{
			if (name.empty() || path.empty())
			{
				return;
			}
			// 只收 exe；按完整路径去重（大小写不敏感）
			const std::wstring fileLower = to_lower(path.substr(path.find_last_of(L'\\') + 1));
			if (!fileLower.ends_with(L".exe") || is_helper_exe(fileLower))
			{
				return;
			}
			// 展开环境变量（如 %windir%…）后校验路径确实存在，过滤失效/系统工具快捷方式
			std::wstring expanded = path;
			if (expanded.find(L'%') != std::wstring::npos)
			{
				wchar_t buf[MAX_PATH * 2]{};
				const DWORD n = ExpandEnvironmentStringsW(expanded.c_str(), buf, MAX_PATH * 2);
				if (n > 0 && n < MAX_PATH * 2)
				{
					expanded = buf;
				}
			}
			if (!app_path_exists(expanded))
			{
				return;
			}
			// 排除开发/环境类工具（node.js、Python、Git、SDK 等）
			const std::wstring stemLower = fileLower.substr(0, fileLower.size() - 4);
			if (is_tool_exe(stemLower))
			{
				return;
			}
			const std::wstring nLower = to_lower(name);
			// 排除系统组件/过时组件/系统小工具
			if (is_tool_display_name(nLower) || is_junk_app(nLower, stemLower))
			{
				return;
			}
			const std::wstring pathLower = to_lower(path);
			for (auto& e : out)
			{
				if (to_lower(e.path) == pathLower)
				{
					// 同一 exe 多来源（注册表/开始菜单/目录）时保留更友好的短名，
					// 如开始菜单"Firefox"优先于注册表"Mozilla Firefox (x64 zh-CN)"
					if (name.size() < e.name.size())
					{
						e.name = std::move(name);
					}
					return;
				}
			}
			out.push_back({std::move(name), std::move(path), is_im_app(nLower), false, app_prio(nLower, fileLower)});
		}

		// 1) 注册表卸载项：DisplayName + DisplayIcon
		void scan_registry_apps(std::vector<AppEntry>& out)
		{
			constexpr std::wstring_view subKey{L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"};
			const HKEY hRoots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
			const REGSAM views[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
			for (const HKEY hRoot : hRoots)
			{
				for (const REGSAM view : views)
				{
					HKEY hKey = nullptr;
					if (RegOpenKeyExW(hRoot, subKey.data(), 0, KEY_READ | view, &hKey) != ERROR_SUCCESS)
					{
						continue;
					}
					wchar_t subName[256]{};
					for (DWORD i = 0;; ++i)
					{
						DWORD subLen = 256;
						if (RegEnumKeyExW(hKey, i, subName, &subLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
						{
							break;
						}
						HKEY hSub = nullptr;
						if (RegOpenKeyExW(hKey, subName, 0, KEY_READ, &hSub) != ERROR_SUCCESS)
						{
							continue;
						}
						const auto readStr = [&](const wchar_t* value, std::wstring& outStr) -> bool
						{
							DWORD type = 0;
							DWORD size = 0;
							if (RegQueryValueExW(hSub, value, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || size == 0)
							{
								return false;
							}
							std::wstring s(size / sizeof(wchar_t) + 1, L'\0');
							DWORD got = size;
							if (RegQueryValueExW(hSub, value, nullptr, &type, reinterpret_cast<LPBYTE>(s.data()), &got) != ERROR_SUCCESS)
							{
								return false;
							}
							if (s[0] == L'\0')
							{
								return false;
							}
							outStr = s.c_str();
							return true;
						};
						std::wstring displayName, displayIcon;
						readStr(L"DisplayName", displayName);
						readStr(L"DisplayIcon", displayIcon);
						RegCloseKey(hSub);
						if (displayName.empty() || displayIcon.empty())
						{
							continue;
						}
						// DisplayIcon 形如 "C:\...\WXWork.exe,0" 或带引号 → 清洗出 exe 路径
						std::wstring exePath = displayIcon;
						const std::size_t comma = exePath.find_last_of(L',');
						if (comma != std::wstring::npos &&
						    exePath.substr(comma + 1).find_first_not_of(L"0123456789") == std::wstring::npos)
						{
							exePath = exePath.substr(0, comma);
						}
						if (exePath.size() >= 2 && exePath.front() == L'"' && exePath.back() == L'"')
						{
							exePath = exePath.substr(1, exePath.size() - 2);
						}
						app_try_add(out, displayName, exePath);
					}
					RegCloseKey(hKey);
				}
			}
		}

		// 2) 开始菜单快捷方式：解析 .lnk 目标 exe
		void scan_start_menu_lnk(std::vector<AppEntry>& out)
		{
			std::vector<std::wstring> roots;
			auto getEnvDir = [](const wchar_t* env, std::wstring& v) -> bool
			{
				wchar_t buf[MAX_PATH]{};
				const DWORD n = GetEnvironmentVariableW(env, buf, MAX_PATH);
				if (n == 0 || n >= MAX_PATH)
				{
					return false;
				}
				v = buf;
				return true;
			};
			std::wstring programData, appData, publicDesktop, userDesktop;
			if (getEnvDir(L"ProgramData", programData))
			{
				roots.push_back(programData + L"\\Microsoft\\Windows\\Start Menu\\Programs");
			}
			if (getEnvDir(L"APPDATA", appData))
			{
				roots.push_back(appData + L"\\Microsoft\\Windows\\Start Menu\\Programs");
			}
			// 桌面快捷方式：部分软件只在桌面创建图标，同样解析 .lnk 目标
			if (getEnvDir(L"PUBLIC", publicDesktop))
			{
				roots.push_back(publicDesktop + L"\\Desktop");
			}
			if (getEnvDir(L"USERPROFILE", userDesktop))
			{
				roots.push_back(userDesktop + L"\\Desktop");
			}
			std::vector<std::wstring> lnkPaths;
			for (const auto& root : roots)
			{
				std::vector<std::wstring> scanDirs{root};
				for (std::size_t d = 0; d < scanDirs.size(); ++d)
				{
					std::error_code ec;
					for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path{scanDirs[d]}, ec))
					{
						if (ec)
						{
							break;
						}
						std::error_code ec2;
						if (entry.is_directory(ec2))
						{
							// 最多 3 层（根/一层/二层），上限 128：目录多时防止"钉钉"等按字母序靠后的
							// 应用文件夹被截断漏扫（实测 Programs 下可达 30+ 个子目录）
							if (d <= 1 && scanDirs.size() < 128)
							{
								scanDirs.push_back(entry.path().wstring());
							}
						}
						else if (entry.path().extension().wstring() == L".lnk")
						{
							lnkPaths.push_back(entry.path().wstring());
						}
					}
				}
			}
			// 解析快捷方式目标
			UniqueComPtr<IShellLinkW> shellLink;
			UniqueComPtr<IPersistFile> persist;
			if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
			                               reinterpret_cast<void**>(&shellLink))))
			{
				shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist));
			}
			if (!persist)
			{
				return;
			}
			for (const auto& linkPath : lnkPaths)
			{
				if (FAILED(persist->Load(linkPath.c_str(), STGM_READ)))
				{
					continue;
				}
				shellLink->Resolve(nullptr, SLR_NO_UI | SLR_NOSEARCH);
				wchar_t target[MAX_PATH]{};
				if (FAILED(shellLink->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH)) || target[0] == L'\0')
				{
					continue;
				}
				const std::wstring name = std::filesystem::path{linkPath}.stem().wstring();
				app_try_add(out, name, target);
			}
		}

		// 版本号目录名（如 "5.10.7600.6676"）解析为数字段；非纯数字返回空
		std::vector<unsigned long long> parse_version_segments(const std::wstring& s)
		{
			std::vector<unsigned long long> nums;
			unsigned long long cur = 0;
			bool hasDigit = false;
			for (const wchar_t c : s)
			{
				if (c >= L'0' && c <= L'9')
				{
					cur = cur * 10 + static_cast<unsigned long long>(c - L'0');
					hasDigit = true;
				}
				else if (c == L'.')
				{
					if (!hasDigit)
					{
						return {};
					}
					nums.push_back(cur);
					cur = 0;
					hasDigit = false;
				}
				else
				{
					return {};
				}
			}
			if (!hasDigit)
			{
				return {};
			}
			nums.push_back(cur);
			return nums;
		}

		bool version_greater(const std::vector<unsigned long long>& a, const std::vector<unsigned long long>& b)
		{
			const std::size_t n = (std::max)(a.size(), b.size());
			for (std::size_t i = 0; i < n; ++i)
			{
				const unsigned long long x = (i < a.size()) ? a[i] : 0;
				const unsigned long long y = (i < b.size()) ? b[i] : 0;
				if (x != y)
				{
					return x > y;
				}
			}
			return false;
		}

		// 3) 特例：腾讯应用宝（Androws 内核）
		// 卸载项的 DisplayIcon 指向 Uninstall.exe（被 is_helper_exe 过滤），安装目录也不在
		// 常规扫描根下，默认扫描必然漏掉。这里从卸载项反推安装目录，取版本号最大的
		// 子目录里的 AndrowsStore.exe —— 实测该入口可正常启动且支持多开
		// （AndrowsLauncher.exe 直接启动无效，Androws.exe 启动后很快自行退出）。
		void scan_tencent_yyb(std::vector<AppEntry>& out)
		{
			constexpr std::wstring_view subKey{L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"};
			const HKEY hRoots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
			const REGSAM views[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
			std::filesystem::path appDir;
			const auto readStr = [](HKEY hSub, const wchar_t* value, std::wstring& outStr) -> bool
			{
				wchar_t buf[MAX_PATH * 2]{};
				DWORD size = sizeof(buf) - sizeof(wchar_t);
				if (RegQueryValueExW(hSub, value, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &size) != ERROR_SUCCESS)
				{
					return false;
				}
				outStr = buf;
				return !outStr.empty();
			};
			for (const HKEY hRoot : hRoots)
			{
				for (const REGSAM view : views)
				{
					HKEY hKey = nullptr;
					if (RegOpenKeyExW(hRoot, subKey.data(), 0, KEY_READ | view, &hKey) != ERROR_SUCCESS)
					{
						continue;
					}
					wchar_t subName[256]{};
					for (DWORD i = 0;; ++i)
					{
						DWORD subLen = 256;
						if (RegEnumKeyExW(hKey, i, subName, &subLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
						{
							break;
						}
						HKEY hSub = nullptr;
						if (RegOpenKeyExW(hKey, subName, 0, KEY_READ, &hSub) != ERROR_SUCCESS)
						{
							continue;
						}
						std::wstring displayName, displayIcon;
						const bool hasName = readStr(hSub, L"DisplayName", displayName);
						const bool hasIcon = readStr(hSub, L"DisplayIcon", displayIcon);
						RegCloseKey(hSub);
						if (!hasName || !hasIcon || displayName.find(L"应用宝") == std::wstring::npos)
						{
							continue;
						}
						// DisplayIcon 形如 "D:\...\Application\Uninstall.exe,0"
						std::wstring exePath = displayIcon;
						const std::size_t comma = exePath.find_last_of(L',');
						if (comma != std::wstring::npos &&
						    exePath.substr(comma + 1).find_first_not_of(L"0123456789") == std::wstring::npos)
						{
							exePath = exePath.substr(0, comma);
						}
						if (exePath.size() >= 2 && exePath.front() == L'"' && exePath.back() == L'"')
						{
							exePath = exePath.substr(1, exePath.size() - 2);
						}
						appDir = std::filesystem::path{exePath}.parent_path();
						break;
					}
					RegCloseKey(hKey);
					if (!appDir.empty())
					{
						break;
					}
				}
				if (!appDir.empty())
				{
					break;
				}
			}
			if (appDir.empty())
			{
				return;
			}
			std::filesystem::path storeExe = appDir / L"AndrowsStore.exe";
			if (!app_path_exists(storeExe.wstring()))
			{
				// Application 目录本身没有主程序：遍历版本号子目录，取版本最大的
				std::filesystem::path best;
				std::vector<unsigned long long> bestVer;
				std::error_code ec;
				for (const auto& entry : std::filesystem::directory_iterator(appDir, ec))
				{
					if (ec)
					{
						break;
					}
					std::error_code ec2;
					if (!entry.is_directory(ec2))
					{
						continue;
					}
					const auto ver = parse_version_segments(entry.path().filename().wstring());
					if (ver.empty())
					{
						continue;
					}
					const std::filesystem::path candidate = entry.path() / L"AndrowsStore.exe";
					if (!app_path_exists(candidate.wstring()))
					{
						continue;
					}
					if (best.empty() || version_greater(ver, bestVer))
					{
						best = candidate;
						bestVer = ver;
					}
				}
				storeExe = best;
			}
			if (app_path_exists(storeExe.wstring()))
			{
				app_try_add(out, L"腾讯应用宝", storeExe.wstring());
			}
		}

		// 4) 常见安装目录：根层 + 一层子目录下的 exe
		void scan_common_dirs(std::vector<AppEntry>& out)
		{
			std::vector<std::wstring> roots;
			auto getEnvDir = [](const wchar_t* env, std::wstring& v) -> bool
			{
				wchar_t buf[MAX_PATH]{};
				const DWORD n = GetEnvironmentVariableW(env, buf, MAX_PATH);
				if (n == 0 || n >= MAX_PATH)
				{
					return false;
				}
				v = buf;
				return true;
			};
			std::wstring v;
			if (getEnvDir(L"ProgramFiles", v))
			{
				roots.push_back(v);
			}
			if (getEnvDir(L"ProgramFiles(x86)", v))
			{
				roots.push_back(v);
			}
			if (getEnvDir(L"LOCALAPPDATA", v))
			{
				roots.push_back(v + L"\\Programs");
			}
			std::vector<std::wstring> exePaths;
			for (const auto& root : roots)
			{
				std::error_code ec;
				if (!std::filesystem::exists(std::filesystem::path{root}, ec) || ec)
				{
					continue;
				}
				std::vector<std::wstring> scanDirs{root};
				for (std::size_t d = 0; d < scanDirs.size(); ++d)
				{
					for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path{scanDirs[d]}, ec))
					{
						if (ec)
						{
							break;
						}
						std::error_code ec2;
						if (entry.is_directory(ec2))
						{
							// 上限提到 64：Program Files 子目录通常 20+，防止靠后目录漏扫
							if (d == 0 && scanDirs.size() < 64)
							{
								scanDirs.push_back(entry.path().wstring());
							}
						}
						else if (entry.path().extension().wstring() == L".exe")
						{
							exePaths.push_back(entry.path().wstring());
						}
					}
				}
			}
			for (const auto& p : exePaths)
			{
				const std::wstring name = std::filesystem::path{p}.stem().wstring();
				app_try_add(out, name, p);
			}
		}

		// ---- 置顶应用记忆（注册表 REG_MULTI_SZ，最多 3 个，按顺序显示在最前）----
		std::vector<std::wstring> load_pinned_apps()
		{
			std::vector<std::wstring> pins;
			HKEY hKey = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_REG_SUBKEY.data(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
			{
				return pins;
			}
			DWORD type = 0;
			DWORD size = 0;
			if (RegQueryValueExW(hKey, APP_REG_PINS.data(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS || size == 0)
			{
				RegCloseKey(hKey);
				return pins;
			}
			std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, L'\0');
			DWORD got = size;
			if (RegQueryValueExW(hKey, APP_REG_PINS.data(), nullptr, &type,
			                     reinterpret_cast<LPBYTE>(buf.data()), &got) == ERROR_SUCCESS)
			{
				// MULTI_SZ：连续 \0 结尾字符串，最后以空串（双 \0）结束
				std::size_t pos = 0;
				while (pos < buf.size() && buf[pos] != L'\0')
				{
					pins.push_back(std::wstring(buf.data() + pos));
					pos += pins.back().size() + 1;
				}
			}
			RegCloseKey(hKey);
			return pins;
		}

		void save_pinned_apps(const std::vector<std::wstring>& pins)
		{
			HKEY hKey = nullptr;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_REG_SUBKEY.data(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
			{
				return;
			}
			if (pins.empty())
			{
				RegDeleteValueW(hKey, APP_REG_PINS.data());
			}
			else
			{
				// 构建 MULTI_SZ：每个字符串以 \0 结尾，末尾再补一个 \0
				std::vector<wchar_t> buf;
				for (const auto& p : pins)
				{
					buf.insert(buf.end(), p.begin(), p.end());
					buf.push_back(L'\0');
				}
				buf.push_back(L'\0');
				RegSetValueExW(hKey, APP_REG_PINS.data(), 0, REG_MULTI_SZ,
				               reinterpret_cast<const BYTE*>(buf.data()),
				               static_cast<DWORD>(buf.size() * sizeof(wchar_t)));
			}
			RegCloseKey(hKey);
		}

		// 名称是否含中文字符（企业微信/微信之后的"中文应用优先"排序依据）
		bool is_cjk_name(const std::wstring& name)
		{
			for (const wchar_t c : name)
			{
				if (c >= 0x4E00 && c <= 0x9FFF)
				{
					return true;
				}
			}
			return false;
		}

		// 由当前 pinned 标记重建置顶顺序并落盘（含失效项清理），再按 置顶→IM→名称 排序
		void app_resort(std::vector<AppEntry>& apps)
		{
			std::vector<std::wstring> pins;
			for (const auto& e : apps)
			{
				if (e.pinned)
				{
					pins.push_back(e.path);
				}
			}
			save_pinned_apps(pins);
			std::unordered_map<std::wstring, int> pinOrder;
			for (std::size_t pi = 0; pi < pins.size(); ++pi)
			{
				pinOrder[to_lower(pins[pi])] = static_cast<int>(pi);
			}
			std::sort(apps.begin(), apps.end(), [&](const AppEntry& a, const AppEntry& b)
			{
				// 企业微信 / 微信 固定最前（第 1、2 位）
				if (a.prio != b.prio)
				{
					return a.prio < b.prio;
				}
				if (a.pinned != b.pinned)
				{
					return a.pinned;
				}
				if (a.pinned && b.pinned)
				{
					return pinOrder.at(to_lower(a.path)) < pinOrder.at(to_lower(b.path));
				}
				// 其余默认：中文名应用优先，再 IM，再按名称
				const bool aCjk = is_cjk_name(a.name);
				const bool bCjk = is_cjk_name(b.name);
				if (aCjk != bCjk)
				{
					return aCjk;
				}
				if (a.im != b.im)
				{
					return a.im;
				}
				return a.name < b.name;
			});
		}

		std::vector<AppEntry> scan_system_apps()
		{
			std::vector<AppEntry> apps;
			scan_registry_apps(apps);
			scan_tencent_yyb(apps);
			scan_start_menu_lnk(apps);
			scan_common_dirs(apps);
			// 应用置顶记忆：标记已置顶项
			const std::vector<std::wstring> pins = load_pinned_apps();
			for (auto& e : apps)
			{
				const std::wstring key = to_lower(e.path);
				for (const auto& p : pins)
				{
					if (to_lower(p) == key)
					{
						e.pinned = true;
						break;
					}
				}
			}
			// 排序 + 清理失效置顶
			app_resort(apps);
			return apps;
		}

		// ---- 上次应用记忆（注册表 HKCU\Software\eBox\LastAppPath）----
		std::wstring load_last_app_path()
		{
			HKEY hKey = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_REG_SUBKEY.data(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
			{
				return {};
			}
			wchar_t buf[MAX_PATH]{};
			DWORD size = sizeof(buf);
			const LSTATUS st = RegQueryValueExW(hKey, APP_REG_LASTAPP.data(), nullptr, nullptr,
			                                    reinterpret_cast<LPBYTE>(buf), &size);
			RegCloseKey(hKey);
			if (st != ERROR_SUCCESS || buf[0] == L'\0')
			{
				return {};
			}
			return buf;
		}

		void save_last_app_path(std::wstring_view path)
		{
			HKEY hKey = nullptr;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_REG_SUBKEY.data(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
			{
				return;
			}
			if (path.empty())
			{
				RegDeleteValueW(hKey, APP_REG_LASTAPP.data());
			}
			else
			{
				RegSetValueExW(hKey, APP_REG_LASTAPP.data(), 0, REG_SZ,
				               reinterpret_cast<const BYTE*>(path.data()),
				               static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
			}
			RegCloseKey(hKey);
		}

		int app_page_count(const std::vector<AppEntry>& apps, int pageSize)
		{
			if (pageSize < 1)
			{
				pageSize = 1;
			}
			return (static_cast<int>(apps.size()) + pageSize - 1) / pageSize;
		}

		RECT app_row_rect(int rowIndex, int dpi, int listRight)
		{
			const int top = ui::dlg_s(APP_ROWS_TOP + rowIndex * APP_ROW_H, dpi);
			return RECT{ui::dlg_s(APP_LIST_LEFT, dpi), top, listRight, top + ui::dlg_s(APP_ROW_H, dpi)};
		}

		RECT app_pin_btn_rect(const RECT& row, int dpi)
		{
			return RECT{row.right - ui::dlg_s(116, dpi), row.top + ui::dlg_s(5, dpi),
			            row.right - ui::dlg_s(60, dpi), row.bottom - ui::dlg_s(5, dpi)};
		}

		RECT app_select_btn_rect(const RECT& row, int dpi)
		{
			return RECT{row.right - ui::dlg_s(56, dpi), row.top + ui::dlg_s(5, dpi),
			            row.right - ui::dlg_s(4, dpi), row.bottom - ui::dlg_s(5, dpi)};
		}

		// 刷新动画：围绕圆心的 8 个圆点按 tick 依次点亮（0x0078d4 主题蓝 / 浅灰）
		void faq_draw_spinner(HDC hdc, int cx, int cy, int r, int tick)
		{
			constexpr int DOT_R = 3;
			HBRUSH hOn = CreateSolidBrush(RGB(0x00, 0x78, 0xd4));
			HBRUSH hOff = CreateSolidBrush(RGB(0xcf, 0xe0, 0xf4));
			for (int i = 0; i < 8; ++i)
			{
				const double ang = (i * 45.0 - 90.0) * 3.14159265358979323846 / 180.0;
				const int dx = static_cast<int>(std::round(r * std::cos(ang)));
				const int dy = static_cast<int>(std::round(r * std::sin(ang)));
				const RECT rc{cx + dx - DOT_R, cy + dy - DOT_R, cx + dx + DOT_R, cy + dy + DOT_R};
				FillRect(hdc, &rc, (i <= (tick % 8)) ? hOn : hOff);
			}
			DeleteObject(hOn);
			DeleteObject(hOff);
		}

		// 后台扫描共享结果：工作线程与 UI 通过它交接数据，消息本身不携带指针，杜绝悬垂/泄漏
		struct AppScanShared
		{
			std::vector<AppEntry> apps;
			std::atomic_bool ready{false};
		};

		struct AppPickData
		{
			HWND hwnd{nullptr};
			std::vector<AppEntry> allApps;   // 扫描到的完整列表（含排序/置顶）
			std::vector<AppEntry> apps;      // 当前显示列表（受搜索过滤）
			int page{0};
			int pageSize{APP_PAGE_SIZE};     // 每页行数（按窗口高度自适应）
			int dpi{96};                     // 窗口 DPI（布局与自绘坐标按此缩放）
			bool refreshing{false};  // 正在刷新（显示动画）
			int refreshTick{0};      // 动画帧计数
			bool scanning{false};    // 后台扫描进行中（防重入）
			std::shared_ptr<AppScanShared> scanResult; // 与工作线程共享的扫描结果块
			std::wstring searchText; // 搜索关键字
			HWND hSearch{nullptr};   // 搜索框
			HWND hSearchHint{nullptr}; // 搜索框占位提示
			HWND hRefresh{nullptr};
			HWND hPrev{nullptr};
			HWND hNext{nullptr};
			HWND hManual{nullptr};
			HWND hCancel{nullptr};
			RECT rcRows{};           // 列表区（仅用于右边界与命中）
			RECT rcSearchPanel{};    // 搜索框白色圆角面板
			std::wstring result;
			bool done{false};
			bool cancelled{true};
			HFONT hFontTitle{nullptr};
			HFONT hFontSmall{nullptr};
			HFONT hFontName{nullptr};
			HFONT hFontPath{nullptr};
		};

		// 按搜索关键字过滤完整列表 → 当前显示列表；关键字为空则显示全部
		void app_apply_search(AppPickData& data)
		{
			std::wstring q = to_lower(data.searchText);
			const std::size_t b = q.find_first_not_of(L" \t");
			const std::size_t e = q.find_last_not_of(L" \t");
			q = (b == std::wstring::npos) ? L"" : q.substr(b, e - b + 1);
			if (q.empty())
			{
				data.apps = data.allApps;
			}
			else
			{
				data.apps.clear();
				for (const auto& a : data.allApps)
				{
					if (to_lower(a.name).find(q) != std::wstring::npos ||
					    to_lower(a.path).find(q) != std::wstring::npos)
					{
						data.apps.push_back(a);
					}
				}
			}
			const int pc = app_page_count(data.apps, data.pageSize);
			if (data.page >= pc)
			{
				data.page = std::max(0, pc - 1);
			}
		}

		// 启动后台扫描系统应用：立即显示 spinner 动画，扫描在工作线程执行，
		// 完成后以线程消息通知 UI（消息不携带指针，结果经 AppScanShared 共享块交接），
		// 全程不阻塞 UI 线程（修复开窗/刷新时列表长时间冻结）
		void app_start_scan(HWND hwnd, AppPickData* data)
		{
			if (data->scanning)
			{
				return; // 已在扫描，防重入
			}
			data->scanning = true;
			data->scanResult = std::make_shared<AppScanShared>();
			data->refreshing = true;
			data->refreshTick = 0;
			SetTimer(hwnd, APP_REFRESH_TIMER, 100, nullptr);
			InvalidateRect(hwnd, nullptr, FALSE);

			auto shared = data->scanResult; // 与工作线程共享生命周期
			const DWORD uiThreadId = GetCurrentThreadId();
			std::thread([shared, uiThreadId]()
			{
				try
				{
					shared->apps = scan_system_apps();
				}
				catch (...)
				{
					shared->apps.clear(); // 扫描异常按空列表处理，不影响 UI
				}
				shared->ready.store(true, std::memory_order_release);
				PostThreadMessageW(uiThreadId, APP_WM_SCAN_DONE, 0, 0);
			}).detach();
		}

		// 按窗口 DPI 重建选择器字体（首次创建与跨屏 DPI 变化共用）
		void app_dlg_create_fonts(AppPickData& data)
		{
			if (data.hFontTitle)
			{
				DeleteObject(data.hFontTitle);
				data.hFontTitle = nullptr;
			}
			if (data.hFontSmall)
			{
				DeleteObject(data.hFontSmall);
				data.hFontSmall = nullptr;
			}
			if (data.hFontName)
			{
				DeleteObject(data.hFontName);
				data.hFontName = nullptr;
			}
			if (data.hFontPath)
			{
				DeleteObject(data.hFontPath);
				data.hFontPath = nullptr;
			}

			data.hFontTitle = CreateFontW(-MulDiv(19, data.dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontSmall = CreateFontW(-MulDiv(11, data.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontName = CreateFontW(-MulDiv(12, data.dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontPath = CreateFontW(-MulDiv(11, data.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

			if (data.hSearch)
			{
				SendMessageW(data.hSearch, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontSmall), TRUE);
			}
			if (data.hSearchHint)
			{
				SendMessageW(data.hSearchHint, WM_SETFONT, reinterpret_cast<WPARAM>(data.hFontSmall), TRUE);
			}
		}

		// 按窗口 DPI 与真实客户区摆放子控件，并按可用高度决定每页行数
		void app_dlg_layout(AppPickData& data)
		{
			if (!data.hwnd)
			{
				return;
			}
			const auto S = [dpi = data.dpi](int v) { return ui::dlg_s(v, dpi); };
			RECT rcClient{};
			GetClientRect(data.hwnd, &rcClient);
			const int W = rcClient.right;
			const int H = rcClient.bottom;
			const int margin = S(24);

			// 搜索面板 + 刷新按钮：贴右对齐
			const int refreshW = S(80);
			const int refreshH = S(38);
			const int refreshTop = S(76);
			const int refreshLeft = W - margin - refreshW;
			const int panelRight = refreshLeft - S(8);
			int panelLeft = panelRight - S(270);
			if (panelLeft < S(300))
			{
				panelLeft = S(300);
			}
			if (panelLeft > panelRight - S(120))
			{
				panelLeft = panelRight - S(120);
			}
			data.rcSearchPanel = RECT{panelLeft, S(76), panelRight, S(114)};
			const RECT rcEdit{panelLeft + S(12), S(80), panelRight - S(12), S(110)};
			if (data.hSearch)
			{
				SetWindowPos(data.hSearch, nullptr, rcEdit.left, rcEdit.top,
				             rcEdit.right - rcEdit.left, rcEdit.bottom - rcEdit.top,
				             SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if (data.hSearchHint)
			{
				SetWindowPos(data.hSearchHint, nullptr, rcEdit.left, rcEdit.top,
				             rcEdit.right - rcEdit.left, rcEdit.bottom - rcEdit.top,
				             SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if (data.hRefresh)
			{
				SetWindowPos(data.hRefresh, nullptr, refreshLeft, refreshTop, refreshW, refreshH,
				             SWP_NOZORDER | SWP_NOACTIVATE);
			}

			// 列表区：底部按钮之上，行数按可用高度收缩（不低于 1 行）
			const int rowsTop = S(APP_ROWS_TOP);
			const int footerTop = H - S(64);
			int rows = APP_PAGE_SIZE;
			while (rows > 1 && rowsTop + rows * S(APP_ROW_H) > footerTop - S(10))
			{
				--rows;
			}
			data.pageSize = rows;
			const int listRight = W - margin;
			data.rcRows = RECT{S(APP_LIST_LEFT), rowsTop, listRight, rowsTop + rows * S(APP_ROW_H)};

			// 底部按钮行：上一页 / 下一页 居左，手动选择 / 取消 贴右
			const int btnH = S(APP_BTN_H);
			const int navW = S(80);
			const int prevLeft = margin;
			const int cancelW = S(104);
			const int manualW = S(132);
			const int cancelLeft = W - margin - cancelW;
			const int manualLeft = cancelLeft - margin - manualW;
			int nextLeft = prevLeft + navW + S(112);
			if (nextLeft + navW > manualLeft - S(12))
			{
				nextLeft = manualLeft - S(12) - navW;
			}
			if (nextLeft < prevLeft + navW + S(8))
			{
				nextLeft = prevLeft + navW + S(8);
			}
			if (data.hPrev)
			{
				SetWindowPos(data.hPrev, nullptr, prevLeft, footerTop, navW, btnH,
				             SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if (data.hNext)
			{
				SetWindowPos(data.hNext, nullptr, nextLeft, footerTop, navW, btnH,
				             SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if (data.hManual)
			{
				SetWindowPos(data.hManual, nullptr, manualLeft, footerTop, manualW, btnH,
				             SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if (data.hCancel)
			{
				SetWindowPos(data.hCancel, nullptr, cancelLeft, footerTop, cancelW, btnH,
				             SWP_NOZORDER | SWP_NOACTIVATE);
			}

			// 每页行数变化后重新夹紧页码
			const int pc = app_page_count(data.apps, data.pageSize);
			if (data.page >= pc)
			{
				data.page = std::max(0, pc - 1);
			}
		}

		LRESULT CALLBACK app_pick_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* data = reinterpret_cast<AppPickData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			switch (msg)
			{
			case WM_CREATE:
			{
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
				data = static_cast<AppPickData*>(cs->lpCreateParams);
				data->hwnd = hwnd;
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));

				const HMODULE hInst = GetModuleHandleW(nullptr);
				data->dpi = ui::dlg_dpi(hwnd);

				// 综合搜索框：字段标题行上方、副标题之下，与刷新按钮贴右对齐；输入即过滤下方列表
				data->hSearch = CreateWindowExW(0, L"EDIT", L"",
				                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
				                                0, 0, 0, 0, hwnd,
				                                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(APP_SEARCH_ID)),
				                                hInst, nullptr);
				// 占位提示：覆盖在搜索框上（后创建=上层），禁用=不拦截点击；未输入时显示灰色提示
				data->hSearchHint = CreateWindowExW(0, L"STATIC", L"搜索应用名称或路径…",
				                                    WS_CHILD | WS_VISIBLE | WS_DISABLED | SS_LEFT,
				                                    0, 0, 0, 0, hwnd,
				                                    reinterpret_cast<HMENU>(static_cast<std::intptr_t>(APP_SEARCH_HINT_ID)),
				                                    hInst, nullptr);

				constexpr DWORD btnStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
				// 刷新按钮：紧挨搜索框右侧并排（贴右对齐），高度与搜索面板对齐（与搜索框同款白色圆角+主题描边）
				data->hRefresh = CreateWindowExW(0, L"BUTTON", L"刷新",
				                                 btnStyle, 0, 0, 0, 0, hwnd,
				                                 reinterpret_cast<HMENU>(static_cast<std::intptr_t>(APP_REFRESH_ID)), hInst, nullptr);
				// 底部：上一页 / 下一页 / 手动选择文件 / 取消
				data->hPrev = CreateWindowExW(0, L"BUTTON", L"上一页", btnStyle, 0, 0, 0, 0, hwnd,
				                              reinterpret_cast<HMENU>(static_cast<std::intptr_t>(APP_PREV_ID)), hInst, nullptr);
				data->hNext = CreateWindowExW(0, L"BUTTON", L"下一页", btnStyle, 0, 0, 0, 0, hwnd,
				                              reinterpret_cast<HMENU>(static_cast<std::intptr_t>(APP_NEXT_ID)), hInst, nullptr);
				data->hManual = CreateWindowExW(0, L"BUTTON", L"手动选择文件", btnStyle, 0, 0, 0, 0, hwnd,
				                                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(APP_MANUAL_ID)), hInst, nullptr);
				data->hCancel = CreateWindowExW(0, L"BUTTON", L"取消", btnStyle, 0, 0, 0, 0, hwnd,
				                                reinterpret_cast<HMENU>(static_cast<std::intptr_t>(APP_CANCEL_ID)), hInst, nullptr);

				app_dlg_create_fonts(*data);
				app_dlg_layout(*data);
				return 0;
			}
			case WM_DPICHANGED:
			{
				if (data)
				{
					data->dpi = ui::dlg_on_dpi_changed(hwnd, lParam);
					app_dlg_create_fonts(*data);
					app_dlg_layout(*data);
					InvalidateRect(hwnd, nullptr, TRUE);
				}
				return 0;
			}
			case WM_SIZE:
			{
				if (data)
				{
					app_dlg_layout(*data);
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				return 0;
			}
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
			{
				if (!data)
				{
					break;
				}
				const auto S = [dpi = data->dpi](int v) { return ui::dlg_s(v, dpi); };
				PAINTSTRUCT ps{};
				HDC hdc = BeginPaint(hwnd, &ps);
				RECT rcClient{};
				GetClientRect(hwnd, &rcClient);
				fill_v_gradient(hdc, rcClient, RGB(0xee, 0xf5, 0xfe), RGB(0xff, 0xff, 0xff));

				// 搜索框白色圆角面板（含主题色边框；EDIT 白底与之融合）；与刷新按钮贴右对齐，避开副标题
				{
					RECT rcSearch = data->rcSearchPanel;
					draw_round_rect(hdc, rcSearch, S(8), RGB(0xff, 0xff, 0xff), RGB(0x9c, 0xb8, 0xd8), 1);
				}

				// 标题 / 副标题
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
				HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, data->hFontTitle));
				RECT rcTitle{S(24), S(18), rcClient.right - S(24), S(48)};
				DrawTextW(hdc, L"选择要启动的应用", -1, &rcTitle,
				          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				SelectObject(hdc, data->hFontSmall);
				SetTextColor(hdc, CLEAN_TEXT_SUB);
				// 矩形给足全宽，避免高 DPI 下文字被截断
				RECT rcSub{S(24), S(48), rcClient.right - S(24), S(68)};
				DrawTextW(hdc, L"自动扫描已装应用，选择后默认启动", -1, &rcSub,
				          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				SelectObject(hdc, hOldFont);

				// 表头
				const int rowsTop = S(APP_ROWS_TOP);
				const int headerBottom = rowsTop - S(4);
				const int headerTop = headerBottom - S(28);
				const int listLeft = S(APP_LIST_LEFT);
				const int listRight = data->rcRows.right;
				const RECT row0 = app_row_rect(0, data->dpi, listRight);
				SelectObject(hdc, data->hFontSmall);
				SetTextColor(hdc, RGB(0x8a, 0x91, 0x9c));
				constexpr wchar_t COL_NO[] = L"序号";
				constexpr wchar_t COL_NAME[] = L"应用名称";
				constexpr wchar_t COL_PATH[] = L"所在路径";
				constexpr wchar_t COL_OP[] = L"操作";
				RECT rcNo{listLeft, headerTop, listLeft + S(44), headerBottom};
				DrawTextW(hdc, COL_NO, -1, &rcNo, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
				RECT rcNameH{row0.left + S(48), headerTop, row0.left + S(220), headerBottom};
				DrawTextW(hdc, COL_NAME, -1, &rcNameH, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
				RECT rcPathH{row0.left + S(224), headerTop, row0.right - S(124), headerBottom};
				DrawTextW(hdc, COL_PATH, -1, &rcPathH, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
				RECT rcOp{row0.right - S(120), headerTop, row0.right, headerBottom};
				DrawTextW(hdc, COL_OP, -1, &rcOp, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
				{
					HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xe8, 0xee, 0xf6));
					HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
					MoveToEx(hdc, listLeft, headerBottom, nullptr);
					LineTo(hdc, listRight, headerBottom);
					SelectObject(hdc, hOldPen);
					DeleteObject(hPen);
				}

				// 列表行
				if (data->apps.empty())
				{
					SelectObject(hdc, data->hFontName);
					SetTextColor(hdc, CLEAN_TEXT_SUB);
					RECT rcEmpty{listLeft, rowsTop + S(40), listRight, rowsTop + S(90)};
					DrawTextW(hdc, data->scanning ? L"正在扫描系统应用…" : L"未扫描到可用应用，请点击右下角[手动选择文件]选择程序",
					          -1, &rcEmpty, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				}
				else
				{
					const int pc = app_page_count(data->apps, data->pageSize);
					const int start = data->page * data->pageSize;
					for (int i = 0; i < data->pageSize; ++i)
					{
						const int idx = start + i;
						if (idx >= static_cast<int>(data->apps.size()))
						{
							break;
						}
						const AppEntry& e = data->apps[static_cast<std::size_t>(idx)];
						const RECT row = app_row_rect(i, data->dpi, listRight);
						// 序号
						SelectObject(hdc, data->hFontSmall);
						SetTextColor(hdc, CLEAN_TEXT_SUB);
						const std::wstring no = std::to_wstring(idx + 1);
						RECT rcNoCell{row.left, row.top, row.left + S(44), row.bottom};
						DrawTextW(hdc, no.c_str(), -1, &rcNoCell, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
						// 名称
						SelectObject(hdc, data->hFontName);
						SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
						RECT rcNameCell{row.left + S(48), row.top, row.left + S(220), row.bottom};
						DrawTextW(hdc, e.name.c_str(), -1, &rcNameCell, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
						// 路径
						SelectObject(hdc, data->hFontPath);
						SetTextColor(hdc, CLEAN_TEXT_SUB);
						RECT rcPathCell{row.left + S(224), row.top, row.right - S(124), row.bottom};
						DrawTextW(hdc, e.path.c_str(), -1, &rcPathCell, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
						// 置顶按钮（最多置顶 3 个，置顶后常驻列表最前）
						{
							const RECT rcPin = app_pin_btn_rect(row, data->dpi);
							if (e.pinned)
							{
								faq_draw_light_button(hdc, rcPin, false, false, L"已置顶",
								                      RGB(0xe8, 0xf7, 0xee), RGB(0xd7, 0xf0, 0xe2), RGB(0xc5, 0xe9, 0xd4),
								                      RGB(0x8f, 0xd0, 0xac), RGB(0x18, 0x8a, 0x4f));
							}
							else
							{
								faq_draw_light_button(hdc, rcPin, false, false, L"置顶",
								                      RGB(0xff, 0xff, 0xff), RGB(0xe8, 0xf1, 0xfa), RGB(0xd0, 0xe4, 0xf7),
								                      RGB(0x9c, 0xb8, 0xd8), RGB(0x00, 0x78, 0xd4));
							}
						}
						// 选择按钮（浅蓝描边 + 蓝字）
						const RECT rcBtn = app_select_btn_rect(row, data->dpi);
						faq_draw_light_button(hdc, rcBtn, false, false, L"选择",
						                      RGB(0xff, 0xff, 0xff), RGB(0xe8, 0xf1, 0xfa), RGB(0xd0, 0xe4, 0xf7),
						                      RGB(0x9c, 0xb8, 0xd8), RGB(0x00, 0x78, 0xd4));
						// 行分割线
						HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0xf0, 0xf3, 0xf8));
						HPEN hOldPen = static_cast<HPEN>(SelectObject(hdc, hPen));
						MoveToEx(hdc, row.left, row.bottom, nullptr);
						LineTo(hdc, row.right, row.bottom);
						SelectObject(hdc, hOldPen);
						DeleteObject(hPen);
					}
					// 页码指示：位于上一页/下一页按钮之间
					SelectObject(hdc, data->hFontSmall);
					SetTextColor(hdc, CLEAN_TEXT_SUB);
					const std::wstring pageText = std::format(L"第 {} / {} 页", data->page + 1, pc);
					RECT rcPrevBtn{}, rcNextBtn{};
					if (data->hPrev)
					{
						GetWindowRect(data->hPrev, &rcPrevBtn);
						MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rcPrevBtn), 2);
					}
					if (data->hNext)
					{
						GetWindowRect(data->hNext, &rcNextBtn);
						MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&rcNextBtn), 2);
					}
					RECT rcPage{rcPrevBtn.right, rcPrevBtn.top, rcNextBtn.left, rcPrevBtn.bottom};
					if (rcPage.right <= rcPage.left)
					{
						rcPage = RECT{listLeft, rcClient.bottom - S(64), listRight, rcClient.bottom - S(64) + S(32)};
					}
					DrawTextW(hdc, pageText.c_str(), -1, &rcPage, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				}
				// 刷新动画：列表区中央"正在刷新"指示（spinner 逐帧点亮 + 文案）
				if (data->refreshing)
				{
					const RECT rcOverlay{listLeft, rowsTop - S(6), listRight, data->rcRows.bottom + S(6)};
					draw_round_rect(hdc, rcOverlay, S(10), RGB(0xf5, 0xf9, 0xfe), RGB(0xdf, 0xed, 0xfb), 1);
					const int cx = (rcOverlay.left + rcOverlay.right) / 2 - S(70);
					const int cy = (rcOverlay.top + rcOverlay.bottom) / 2;
					faq_draw_spinner(hdc, cx, cy, S(12), data->refreshTick);
					SelectObject(hdc, data->hFontName);
					SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
					RECT rcTxt{cx + S(30), cy - S(18), rcOverlay.right - S(60), cy + S(18)};
					DrawTextW(hdc, L"正在刷新应用列表…", -1, &rcTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
				}
				SelectObject(hdc, hOldFont);
				EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_CTLCOLOREDIT:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
				return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
			}
			case WM_CTLCOLORSTATIC:
			{
				HDC hdc = reinterpret_cast<HDC>(wParam);
				const HWND hWnd = reinterpret_cast<HWND>(lParam);
				SetBkMode(hdc, TRANSPARENT);
				if (hWnd == data->hSearchHint)
				{
					SetTextColor(hdc, CLEAN_TEXT_SUB); // 占位提示：灰色
				}
				else
				{
					SetTextColor(hdc, RGB(0x1f, 0x29, 0x37));
				}
				return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
			}
			case WM_DRAWITEM:
			{
				const DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
				const int id = dis->CtlID;
				if (id == APP_PREV_ID || id == APP_NEXT_ID ||
				    id == APP_MANUAL_ID || id == APP_CANCEL_ID || id == APP_REFRESH_ID)
				{
					const bool hover = (dis->itemState & ODS_HOTLIGHT) != 0;
					const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
					wchar_t buf[64]{};
					GetWindowTextW(dis->hwndItem, buf, 64);
					if (id == APP_PREV_ID || id == APP_NEXT_ID)
					{
						// 上一页/下一页：浅色小按钮，到边界置灰
						const int pc = app_page_count(data->apps, data->pageSize);
						const bool enabled = (id == APP_PREV_ID) ? (data->page > 0) : (data->page < pc - 1);
						faq_draw_light_button(dis->hDC, dis->rcItem, hover && enabled, pressed && enabled, buf,
						                      RGB(0xff, 0xff, 0xff), RGB(0xf2, 0xf6, 0xfb), RGB(0xe6, 0xee, 0xf7),
						                      enabled ? RGB(0xcf, 0xd8, 0xe3) : RGB(0xe6, 0xea, 0xf0),
						                      enabled ? RGB(0x3a, 0x46, 0x55) : RGB(0x9a, 0xa3, 0xaf));
					}
					else if (id == APP_MANUAL_ID || id == APP_REFRESH_ID)
					{
						// 手动选择文件 / 刷新：浅蓝描边 + 蓝字
						faq_draw_light_button(dis->hDC, dis->rcItem, hover, pressed, buf,
						                      RGB(0xff, 0xff, 0xff), RGB(0xe8, 0xf1, 0xfa), RGB(0xd0, 0xe4, 0xf7),
						                      RGB(0x9c, 0xb8, 0xd8), RGB(0x00, 0x78, 0xd4));
					}
					else
					{
						// 取消：中性浅色
						faq_draw_light_button(dis->hDC, dis->rcItem, hover, pressed, buf,
						                      RGB(0xff, 0xff, 0xff), RGB(0xf2, 0xf6, 0xfb), RGB(0xe6, 0xee, 0xf7),
						                      RGB(0xcf, 0xd8, 0xe3), RGB(0x3a, 0x46, 0x55));
					}
					return TRUE;
				}
				return FALSE;
			}
			case WM_COMMAND:
			{
				const int id = LOWORD(wParam);
				// 搜索框：输入实时过滤下方列表；获得/失去焦点时切换占位提示
				if (id == APP_SEARCH_ID)
				{
					if (HIWORD(wParam) == EN_CHANGE)
					{
						wchar_t buf[256]{};
						GetWindowTextW(data->hSearch, buf, 256);
						data->searchText = buf;
						app_apply_search(*data);
						if (data->hSearchHint)
						{
							ShowWindow(data->hSearchHint, data->searchText.empty() ? SW_SHOW : SW_HIDE);
						}
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					else if (HIWORD(wParam) == EN_SETFOCUS)
					{
						if (data->hSearchHint)
						{
							ShowWindow(data->hSearchHint, SW_HIDE);
						}
					}
					else if (HIWORD(wParam) == EN_KILLFOCUS)
					{
						if (data->hSearchHint)
						{
							ShowWindow(data->hSearchHint, data->searchText.empty() ? SW_SHOW : SW_HIDE);
						}
					}
					return 0;
				}
				if (id == APP_PREV_ID)
				{
					if (data->page > 0)
					{
						--data->page;
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				}
				if (id == APP_NEXT_ID)
				{
					if (data->page < app_page_count(data->apps, data->pageSize) - 1)
					{
						++data->page;
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				}
				if (id == APP_MANUAL_ID)
				{
					// 手动选择文件（与"启动新进程"原逻辑一致）
					if (const std::optional<std::wstring> path = run_file_open_dialog(hwnd))
					{
						data->result = *path;
						data->cancelled = false;
						save_last_app_path(*path);
						DestroyWindow(hwnd);
					}
					return 0;
				}
				if (id == APP_CANCEL_ID)
				{
					data->cancelled = true;
					DestroyWindow(hwnd);
					return 0;
				}
				if (id == APP_REFRESH_ID)
				{
					app_start_scan(hwnd, data); // 后台扫描（内部防重入），动画立即反馈，UI 不冻结
					return 0;
				}
				return 0;
			}
			case WM_TIMER:
				if (wParam == APP_REFRESH_TIMER)
				{
					++data->refreshTick; // 仅推进动画帧；扫描收尾由 APP_WM_SCAN_DONE 处理
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
				return 0;
			case WM_LBUTTONDOWN:
			{
				const POINT pt{static_cast<LONG>(LOWORD(lParam)), static_cast<LONG>(HIWORD(lParam))};
				const int rowsTop = ui::dlg_s(APP_ROWS_TOP, data->dpi);
				const int rowH = ui::dlg_s(APP_ROW_H, data->dpi);
				if (pt.y >= rowsTop && !data->apps.empty())
				{
					const int rowIndex = (pt.y - rowsTop) / rowH;
					if (rowIndex >= 0 && rowIndex < data->pageSize)
					{
						const int idx = data->page * data->pageSize + rowIndex;
						if (idx >= 0 && idx < static_cast<int>(data->apps.size()))
						{
							const RECT row = app_row_rect(rowIndex, data->dpi, data->rcRows.right);
							AppEntry& e = data->apps[static_cast<std::size_t>(idx)];
							// 置顶按钮：切换置顶状态（最多 3 个）。在完整列表上操作，
							// 避免搜索过滤后丢置顶记忆；再重排序并重新应用搜索过滤
							const RECT rcPin = app_pin_btn_rect(row, data->dpi);
							if (pt.x >= rcPin.left && pt.x <= rcPin.right && pt.y >= rcPin.top && pt.y <= rcPin.bottom)
							{
								const std::wstring key = to_lower(e.path);
								for (auto& a : data->allApps)
								{
									if (to_lower(a.path) != key)
									{
										continue;
									}
									if (a.pinned)
									{
										a.pinned = false;
									}
									else
									{
										int pinnedCount = 0;
										for (const auto& all : data->allApps)
										{
											if (all.pinned)
											{
												++pinnedCount;
											}
										}
										if (pinnedCount >= APP_MAX_PINS)
										{
											MessageBoxW(hwnd, L"最多可置顶 3 个应用，请先取消其他置顶。",
											            MainApp::appName.data(), MB_OK | MB_ICONINFORMATION | MB_TASKMODAL);
											return 0;
										}
										a.pinned = true;
									}
									break;
								}
								app_resort(data->allApps);
								app_apply_search(*data);
								data->page = 0; // 置顶后回到第一页，让置顶项立即可见
								InvalidateRect(hwnd, nullptr, FALSE);
								return 0;
							}
							// 其余区域（含"选择"按钮）→ 选中该应用
							if (pt.x >= row.left && pt.x <= row.right)
							{
								data->result = e.path;
								data->cancelled = false;
								save_last_app_path(data->result);
								DestroyWindow(hwnd);
								return 0;
							}
						}
					}
				}
				return 0;
			}
			case WM_CLOSE:
				data->cancelled = true;
				DestroyWindow(hwnd);
				return 0;
			case WM_DESTROY:
				KillTimer(hwnd, APP_REFRESH_TIMER);
				if (data->hFontTitle)
				{
					DeleteObject(data->hFontTitle);
				}
				if (data->hFontSmall)
				{
					DeleteObject(data->hFontSmall);
				}
				if (data->hFontName)
				{
					DeleteObject(data->hFontName);
				}
				if (data->hFontPath)
				{
					DeleteObject(data->hFontPath);
				}
				data->done = true;
				return 0;
			default:
				break;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}

	std::wstring get_last_app_path()
	{
		return load_last_app_path();
	}

	void clear_last_app_path()
	{
		save_last_app_path(L"");
	}

	// exe 路径（小写）→ 应用名称 的两级缓存，名称来源与“选择要启动的应用”列表一致：
	// 1) 注册表卸载项 DisplayIcon 指向该 exe 的 DisplayName（如 WXWork → 企业微信）
	// 2) 开始菜单/桌面快捷方式文件名（如 豆包.lnk → D:\RJ\Doubao\Doubao.exe）
	// 两者均按需扫描一次并缓存，避免环境卡片每次重建标识都重新扫描系统。
	namespace
	{
		const std::unordered_map<std::wstring, std::wstring>& registry_app_names()
		{
			static const std::unordered_map<std::wstring, std::wstring> map = []()
			{
				std::vector<AppEntry> apps;
				scan_registry_apps(apps);
				std::unordered_map<std::wstring, std::wstring> names;
				names.reserve(apps.size());
				for (const auto& e : apps)
				{
					names.emplace(to_lower(e.path), e.name);
				}
				return names;
			}();
			return map;
		}

		const std::unordered_map<std::wstring, std::wstring>& shortcut_app_names()
		{
			static const std::unordered_map<std::wstring, std::wstring> map = []()
			{
				std::vector<AppEntry> apps;
				scan_start_menu_lnk(apps);
				std::unordered_map<std::wstring, std::wstring> names;
				names.reserve(apps.size());
				for (const auto& e : apps)
				{
					names.emplace(to_lower(e.path), e.name);
				}
				return names;
			}();
			return map;
		}
	}

	std::wstring resolve_app_display_name(std::wstring_view exePath)
	{
		if (exePath.empty())
		{
			return {};
		}
		const std::wstring path(exePath);
		const std::wstring key = to_lower(path);
		// 注册表命中（如企业微信）直接返回，避免首次解析就去扫开始菜单
		const auto& regNames = registry_app_names();
		if (const auto it = regNames.find(key); it != regNames.end() && !it->second.empty())
		{
			return it->second;
		}
		const auto& lnkNames = shortcut_app_names();
		if (const auto it = lnkNames.find(key); it != lnkNames.end() && !it->second.empty())
		{
			return it->second;
		}
		return std::filesystem::path{path}.stem().wstring();
	}

	std::optional<std::wstring> select_app_dialog(const WindowBase* owner)
	{
		static const bool classRegistered = []()
		{
			WNDCLASSEXW wc = {sizeof(wc)};
			wc.lpfnWndProc = app_pick_proc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
			wc.lpszClassName = APP_DLG_CLASS;
			return RegisterClassExW(&wc) != 0;
		}();
		(void)classRegistered;

		AppPickData data; // 列表由 app_start_scan 后台填充，弹窗立即可交互

		HWND hOwner = owner ? owner->nativeHandle() : nullptr;
		const SIZE clientSize = ui::dlg_client_size(hOwner, APP_DLG_WIDTH, APP_DLG_HEIGHT);
		const HWND hDlg = CreateWindowExW(0, APP_DLG_CLASS, L"选择要启动的应用",
		                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
		                                  0, 0, clientSize.cx, clientSize.cy, hOwner, nullptr,
		                                  GetModuleHandleW(nullptr), &data);
		if (!hDlg)
		{
			return std::nullopt;
		}
		ui::dlg_fit_client(hDlg, clientSize);
		ui::dlg_center_on(hDlg, hOwner);
		ShowWindow(hDlg, SW_SHOW);
		app_start_scan(hDlg, &data); // 后台扫描系统应用（spinner 提示加载中），UI 立即可交互

		if (hOwner && IsWindow(hOwner))
		{
			EnableWindow(hOwner, FALSE);
		}

		MSG msg{};
		while (!data.done && GetMessageW(&msg, nullptr, 0, 0))
		{
			// 后台扫描完成（线程消息，hwnd 为空）：从共享块取结果并收尾刷新
			if (msg.message == APP_WM_SCAN_DONE && msg.hwnd == nullptr)
			{
				if (data.scanResult && data.scanResult->ready.load(std::memory_order_acquire))
				{
					data.allApps = std::move(data.scanResult->apps);
					data.scanResult.reset();
					data.page = 0;
					data.scanning = false;
					data.refreshing = false;
					KillTimer(hDlg, APP_REFRESH_TIMER);
					app_apply_search(data); // 保持当前搜索过滤
					InvalidateRect(hDlg, nullptr, FALSE);
				}
				continue;
			}
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

		return data.cancelled ? std::nullopt : std::optional<std::wstring>{data.result};
	}

	// ---- 跨模块导出的协议弹窗入口（定义在 namespace ui 作用域，保证外部链接可解析）----
	// 供首页用户须知等外部模块调用：直接打开《用户协议》/《隐私协议》弹窗（不涉及勾选回写）。
	// 内部复用上面匿名命名空间中的 show_agreement_dialog（本文件内可见）。
	void show_user_agreement_dialog(HWND hOwner)
	{
		show_agreement_dialog(hOwner, nullptr, L"eBox 用户协议", AGREEMENT_TEXT);
	}

	void show_privacy_agreement_dialog(HWND hOwner)
	{
		show_agreement_dialog(hOwner, nullptr, L"eBox 隐私协议", AGREEMENT_TEXT_PRIVACY);
	}

	// ---- 易歪歪安装位置检测：命中被重定向的系统用户目录 → 提示改装到非系统盘 ----
	// 背景：易歪歪若安装在"文档"/AppData 等被 eBox 环境隔离重定向的系统用户目录内，
	// 多开的企业微信可能读取不到其图片文件，导致话术只发文字。此处仅做"检测 + 提示"，
	// 不改动任何隔离/重定向核心逻辑，用户可选择"不再提示"（持久化到 HKCU\Software\eBox）。
	namespace
	{
		// 与 MemoryDll GlobalData::initializeKnownFolderPath 保持一致的被重定向已知文件夹。
		// 注意：Program Files / 其他盘普通目录不在其中，装到那里不受影响。
		const KNOWNFOLDERID* const kRedirectedFolderIds[] = {
			&FOLDERID_LocalAppData,
			&FOLDERID_LocalAppDataLow,
			&FOLDERID_RoamingAppData,
			&FOLDERID_SavedGames,
			&FOLDERID_ProgramData,
			&FOLDERID_Documents,
		};

		std::wstring normalize_path_lower(std::wstring p)
		{
			std::transform(p.begin(), p.end(), p.begin(),
			               [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/'))
			{
				p.pop_back();
			}
			return p;
		}

		// dir 是否位于任一被重定向的已知文件夹内（含等于该目录）
		bool path_in_redirected_folder(const std::wstring& dir)
		{
			const std::wstring d = normalize_path_lower(dir);
			if (d.empty())
			{
				return false;
			}
			for (const KNOWNFOLDERID* id : kRedirectedFolderIds)
			{
				PWSTR raw = nullptr;
				if (FAILED(SHGetKnownFolderPath(*id, 0, nullptr, &raw)) || !raw)
				{
					continue;
				}
				const std::wstring folder = normalize_path_lower(raw);
				CoTaskMemFree(raw);
				if (!folder.empty() && (d == folder || d.starts_with(folder + L"\\")))
				{
					return true;
				}
			}
			return false;
		}

		bool is_yiwaiwai_name(std::wstring name)
		{
			std::transform(name.begin(), name.end(), name.begin(),
			               [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
			return name.find(L"易歪歪") != std::wstring::npos ||
			       name.find(L"yiwaiwai") != std::wstring::npos;
		}

		std::wstring parent_dir_of(const std::wstring& filePath)
		{
			const std::size_t pos = filePath.find_last_of(L"\\/");
			return pos == std::wstring::npos ? std::wstring{} : filePath.substr(0, pos);
		}

		// 注册表卸载项：DisplayName 命中"易歪歪"时，取安装目录 / DisplayIcon 所在目录
		void collect_yiwaiwai_from_registry(std::vector<std::wstring>& out)
		{
			constexpr std::wstring_view subKey{L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall"};
			const HKEY hRoots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
			const REGSAM views[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
			for (const HKEY hRoot : hRoots)
			{
				for (const REGSAM view : views)
				{
					HKEY hKey = nullptr;
					if (RegOpenKeyExW(hRoot, subKey.data(), 0, KEY_READ | view, &hKey) != ERROR_SUCCESS)
					{
						continue;
					}
					wchar_t subName[256]{};
					for (DWORD i = 0;; ++i)
					{
						DWORD subLen = 256;
						if (RegEnumKeyExW(hKey, i, subName, &subLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
						{
							break;
						}
						HKEY hSub = nullptr;
						if (RegOpenKeyExW(hKey, subName, 0, KEY_READ, &hSub) != ERROR_SUCCESS)
						{
							continue;
						}
						const auto readStr = [&](const wchar_t* value, std::wstring& outStr) -> bool
						{
							DWORD type = 0;
							DWORD size = 0;
							if (RegQueryValueExW(hSub, value, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || size == 0)
							{
								return false;
							}
							std::wstring s(size / sizeof(wchar_t) + 1, L'\0');
							DWORD got = size;
							if (RegQueryValueExW(hSub, value, nullptr, &type, reinterpret_cast<LPBYTE>(s.data()), &got) != ERROR_SUCCESS)
							{
								return false;
							}
							if (s[0] == L'\0')
							{
								return false;
							}
							outStr = s.c_str();
							return true;
						};
						std::wstring displayName, installLoc, displayIcon;
						readStr(L"DisplayName", displayName);
						readStr(L"InstallLocation", installLoc);
						readStr(L"DisplayIcon", displayIcon);
						RegCloseKey(hSub);

						if (!is_yiwaiwai_name(displayName) && !is_yiwaiwai_name(installLoc))
						{
							continue;
						}
						if (!installLoc.empty())
						{
							out.push_back(installLoc);
						}
						if (!displayIcon.empty())
						{
							// DisplayIcon 形如 "C:\...\yiwaiwai.exe,0" 或带引号 → 清洗成目录
							std::wstring exe = displayIcon;
							const std::size_t comma = exe.find_last_of(L',');
							if (comma != std::wstring::npos &&
							    exe.substr(comma + 1).find_first_not_of(L"0123456789") == std::wstring::npos)
							{
								exe = exe.substr(0, comma);
							}
							if (exe.size() >= 2 && exe.front() == L'"' && exe.back() == L'"')
							{
								exe = exe.substr(1, exe.size() - 2);
							}
							out.push_back(parent_dir_of(exe));
						}
					}
					RegCloseKey(hKey);
				}
			}
		}

		// 运行中的进程：进程名命中"易歪歪"时取其 exe 所在目录
		void collect_yiwaiwai_from_processes(std::vector<std::wstring>& out)
		{
			const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if (snap == INVALID_HANDLE_VALUE)
			{
				return;
			}
			PROCESSENTRY32W pe{};
			pe.dwSize = sizeof(pe);
			if (Process32FirstW(snap, &pe))
			{
				do
				{
					if (!is_yiwaiwai_name(pe.szExeFile))
					{
						continue;
					}
					const HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
					if (!hProc)
					{
						continue;
					}
					wchar_t buf[MAX_PATH * 2]{};
					DWORD n = static_cast<DWORD>(std::size(buf));
					if (QueryFullProcessImageNameW(hProc, 0, buf, &n))
					{
						out.push_back(parent_dir_of(buf));
					}
					CloseHandle(hProc);
				} while (Process32NextW(snap, &pe));
			}
			CloseHandle(snap);
		}

		// 兜底：常见安装根目录下直接叫"易歪歪"的一级子目录
		void collect_yiwaiwai_from_common_dirs(std::vector<std::wstring>& out)
		{
			const wchar_t* envs[] = {L"LOCALAPPDATA", L"ProgramFiles", L"ProgramFiles(x86)", L"USERPROFILE"};
			for (const wchar_t* env : envs)
			{
				wchar_t buf[MAX_PATH]{};
				const DWORD n = GetEnvironmentVariableW(env, buf, MAX_PATH);
				if (n == 0 || n >= MAX_PATH)
				{
					continue;
				}
				std::error_code ec;
				const std::filesystem::path base{buf};
				if (!std::filesystem::exists(base, ec) || ec)
				{
					continue;
				}
				for (const auto& entry : std::filesystem::directory_iterator(base, ec))
				{
					if (ec)
					{
						break;
					}
					std::error_code ec2;
					if (entry.is_directory(ec2) && is_yiwaiwai_name(entry.path().filename().wstring()))
					{
						out.push_back(entry.path().wstring());
					}
				}
			}
		}

		constexpr wchar_t kYiWaiWaiTipRegValue[] = L"YiWaiWaiPathTipDismissed";

		bool yiwaiwai_tip_dismissed()
		{
			HKEY hKey = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\eBox", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
			{
				return false;
			}
			DWORD type = 0;
			DWORD value = 0;
			DWORD size = sizeof(value);
			const LONG r = RegQueryValueExW(hKey, kYiWaiWaiTipRegValue, nullptr, &type,
			                                reinterpret_cast<LPBYTE>(&value), &size);
			RegCloseKey(hKey);
			return r == ERROR_SUCCESS && type == REG_DWORD && value != 0;
		}

		void set_yiwaiwai_tip_dismissed()
		{
			HKEY hKey = nullptr;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\eBox", 0, nullptr,
			                    REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr) != ERROR_SUCCESS)
			{
				return;
			}
			const DWORD value = 1;
			RegSetValueExW(hKey, kYiWaiWaiTipRegValue, 0, REG_DWORD,
			               reinterpret_cast<const BYTE*>(&value), sizeof(value));
			RegCloseKey(hKey);
		}

		// 找到一个"易歪歪已安装在重定向目录内"的目录路径；未命中返回空
		std::wstring find_yiwaiwai_redirected_dir()
		{
			std::vector<std::wstring> candidates;
			collect_yiwaiwai_from_registry(candidates);
			collect_yiwaiwai_from_processes(candidates);

			for (const auto& c : candidates)
			{
				if (!c.empty() && path_in_redirected_folder(c))
				{
					return c;
				}
			}

			std::vector<std::wstring> dirs;
			collect_yiwaiwai_from_common_dirs(dirs);
			for (const auto& d : dirs)
			{
				if (path_in_redirected_folder(d))
				{
					return d;
				}
			}
			return {};
		}
	}

	void maybe_show_yiwaiwai_path_tip(const WindowBase* owner)
	{
		if (yiwaiwai_tip_dismissed())
		{
			return;
		}
		const std::wstring hitDir = find_yiwaiwai_redirected_dir();
		if (hitDir.empty())
		{
			return; // 未检测到易歪歪，或未落在被重定向的用户目录内 → 不打扰
		}

		TASKDIALOGCONFIG cfg{};
		cfg.cbSize = sizeof(cfg);
		cfg.hwndParent = owner ? owner->nativeHandle() : nullptr;
		cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
		cfg.pszWindowTitle = MainApp::appName.data();
		cfg.pszMainIcon = TD_WARNING_ICON;
		cfg.pszMainInstruction = L"检测到易歪歪安装在系统用户目录，可能导致图片发送失败";
		const std::wstring content =
			std::wstring{L"易歪歪当前位置：\n"} + hitDir +
			L"\n\n该目录属于 eBox 环境隔离重定向的系统用户目录（如“文档”/AppData/ProgramData）。"
			L"多开环境下企业微信可能读取不到此处的图片，导致话术只发出文字、图片发不出去。\n\n"
			L"建议：将易歪歪卸载后重新安装到一个非系统目录即可正常发送图片，例如：\n"
			L"  • D 盘（或其他非系统盘），如 D:\\易歪歪；\n"
			L"  • 若没有 D 盘，也可装到 C 盘的非系统目录，如 C:\\易歪歪。\n"
			L"只要不落在系统用户目录（如 文档/AppData/ProgramData/Saved Games 等），发送图片就不受影响。";
		cfg.pszContent = content.c_str();
		cfg.pszVerificationText = L"不再提示";
		static constexpr TASKDIALOG_BUTTON buttons[] = {
			{100, L"我知道了"},
		};
		cfg.cButtons = ARRAYSIZE(buttons);
		cfg.pButtons = buttons;
		cfg.nDefaultButton = 100;

		BOOL verificationChecked = FALSE;
		if (SUCCEEDED(TaskDialogIndirect(&cfg, nullptr, nullptr, &verificationChecked)) && verificationChecked)
		{
			set_yiwaiwai_tip_dismissed();
		}
	}

	// ---- 设置对话框：一键切换环境数据根目录（Env\ 与 bin\ 整体迁移）----
	// 迁移动作由 MainApp 侧在"重启后的启动期"完成：本函数只负责选择目录、校验与重启接力。
	namespace
	{
		std::wstring format_bytes(std::uint64_t bytes)
		{
			constexpr double KB = 1024.0;
			constexpr double MB = KB * 1024.0;
			constexpr double GB = MB * 1024.0;
			const double value = static_cast<double>(bytes);
			if (value >= GB)
			{
				return std::format(L"{:.2f} GB", value / GB);
			}
			if (value >= MB)
			{
				return std::format(L"{:.1f} MB", value / MB);
			}
			if (value >= KB)
			{
				return std::format(L"{:.0f} KB", value / KB);
			}
			return std::format(L"{} B", bytes);
		}

		// 规整路径并去掉尾部路径分隔符（保留 "D:\" 这类盘符根）
		std::wstring normalized_dir(const std::filesystem::path& dir)
		{
			std::error_code ec;
			std::wstring result = std::filesystem::weakly_canonical(dir, ec).wstring();
			if (ec || result.empty())
			{
				result = dir.wstring();
			}
			while (result.size() > 3 && (result.back() == L'\\' || result.back() == L'/'))
			{
				result.pop_back();
			}
			return result;
		}

		bool same_dir(const std::wstring& a, const std::wstring& b)
		{
			return _wcsicmp(a.c_str(), b.c_str()) == 0;
		}

		// child 是否严格位于 parent 之内（大小写不敏感）
		bool dir_within(const std::wstring& child, const std::wstring& parent)
		{
			if (parent.size() >= child.size() || _wcsnicmp(parent.c_str(), child.c_str(), parent.size()) != 0)
			{
				return false;
			}
			if (parent.back() == L'\\' || parent.back() == L'/')
			{
				return true;
			}
			const wchar_t next = child[parent.size()];
			return next == L'\\' || next == L'/';
		}

		std::uint64_t dir_size_bytes(const std::filesystem::path& dir)
		{
			std::error_code ec;
			if (!std::filesystem::is_directory(dir, ec) || ec)
			{
				return 0;
			}
			std::uint64_t total = 0;
			std::filesystem::recursive_directory_iterator it{dir, std::filesystem::directory_options::skip_permission_denied, ec};
			const std::filesystem::recursive_directory_iterator end{};
			while (!ec && it != end)
			{
				std::error_code entryEc;
				if (it->is_regular_file(entryEc) && !entryEc)
				{
					const auto size = it->file_size(entryEc);
					if (!entryEc)
					{
						total += size;
					}
				}
				entryEc.clear();
				it.increment(entryEc);
				if (entryEc)
				{
					break;
				}
			}
			return total;
		}

		struct DiskSpaceInfo
		{
			std::uint64_t freeBytes{0};
			std::uint64_t totalBytes{0};
		};

		DiskSpaceInfo query_disk_space(const std::wstring& dir)
		{
			DiskSpaceInfo info;
			if (dir.empty())
			{
				return info;
			}
			ULARGE_INTEGER freeBytes{};
			ULARGE_INTEGER totalBytes{};
			if (GetDiskFreeSpaceExW(dir.c_str(), nullptr, &totalBytes, &freeBytes))
			{
				info.freeBytes = freeBytes.QuadPart;
				info.totalBytes = totalBytes.QuadPart;
			}
			return info;
		}

		bool ensure_dir_writable(const std::filesystem::path& dir, std::wstring& error)
		{
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			if (ec)
			{
				error = std::format(L"无法创建目标目录：{}", dir.wstring());
				return false;
			}
			const std::wstring probe = (dir / L".ebox_write_test.tmp").wstring();
			const HANDLE handle = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
			                                  FILE_ATTRIBUTE_TEMPORARY, nullptr);
			if (handle == INVALID_HANDLE_VALUE)
			{
				error = std::format(L"目标目录不可写入：{}", dir.wstring());
				return false;
			}
			CloseHandle(handle);
			DeleteFileW(probe.c_str());
			return true;
		}

		// ---- 磁盘扫描：直接列本机固定/可移动磁盘，按容量条选择安装盘 ----
		// 说明：不做文件夹选择。每个盘固定使用 <盘符>\eBoxData 作为数据目录（eBox 默认布局），
		// 扫描阶段只读（不创建目录），可写性与空间校验留到用户确认时再做。
		struct DriveEntry
		{
			std::wstring root;          // 盘符根，如 "D:\"
			std::wstring volumeName;    // 卷标
			std::wstring targetDir;     // 目标数据目录，如 "D:\eBoxData"
			std::uint64_t freeBytes{0};
			std::uint64_t totalBytes{0};
			bool isSystem{false};       // 是否系统盘
			std::wstring blockedReason; // 非空 = 该盘不可选
		};

		std::vector<DriveEntry> scan_drives(const std::wstring& currentDir, std::uint64_t neededBytes)
		{
			std::vector<DriveEntry> drives;

			// 系统盘：Windows 目录所在盘的盘符
			wchar_t winDir[MAX_PATH]{};
			std::wstring systemRoot;
			if (GetWindowsDirectoryW(winDir, MAX_PATH) >= 2)
			{
				systemRoot.assign(winDir, 2);
				systemRoot += L'\\';
			}

			wchar_t buffer[512]{};
			const DWORD length = GetLogicalDriveStringsW(ARRAYSIZE(buffer) - 1, buffer);
			if (length == 0 || length >= ARRAYSIZE(buffer))
			{
				return drives;
			}
			const std::wstring currentNorm = normalized_dir(currentDir);
			for (const wchar_t* p = buffer; *p != L'\0'; p += wcslen(p) + 1)
			{
				const std::wstring root{p};
				const UINT type = GetDriveTypeW(root.c_str());
				if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
				{
					continue; // 光驱/网络盘不参与
				}
				ULARGE_INTEGER freeBytes{};
				ULARGE_INTEGER totalBytes{};
				if (!GetDiskFreeSpaceExW(root.c_str(), &freeBytes, &totalBytes, nullptr) || totalBytes.QuadPart == 0)
				{
					continue;
				}

				DriveEntry entry;
				entry.root = root;
				entry.freeBytes = freeBytes.QuadPart;
				entry.totalBytes = totalBytes.QuadPart;
				entry.isSystem = !systemRoot.empty() && _wcsicmp(root.c_str(), systemRoot.c_str()) == 0;
				wchar_t volumeName[MAX_PATH]{};
				if (GetVolumeInformationW(root.c_str(), volumeName, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0))
				{
					entry.volumeName = volumeName;
				}
				std::filesystem::path target{root};
				target /= L"eBoxData";
				entry.targetDir = target.wstring();

				const std::wstring targetNorm = normalized_dir(target);
				if (same_dir(targetNorm, currentNorm) || dir_within(currentNorm, targetNorm) ||
				    dir_within(targetNorm, currentNorm))
				{
					entry.blockedReason = L"当前数据就在此盘，无需切换";
				}
				else
				{
					std::error_code ec;
					const std::filesystem::path envDir = target / L"Env";
					const bool hasEnv = std::filesystem::is_directory(envDir, ec) && !ec &&
					                    !std::filesystem::is_empty(envDir, ec) && !ec;
					ec.clear();
					const std::filesystem::path binDir = target / L"bin";
					const bool hasBin = std::filesystem::is_directory(binDir, ec) && !ec &&
					                    !std::filesystem::is_empty(binDir, ec) && !ec;
					if (hasEnv || hasBin)
					{
						entry.blockedReason = L"该盘 eBoxData 目录中已有数据";
					}
					else if (entry.freeBytes < neededBytes)
					{
						entry.blockedReason = std::format(L"可用空间不足，至少需要 {}", format_bytes(neededBytes));
					}
				}
				drives.push_back(std::move(entry));
			}
			return drives;
		}

		// ---- 自绘磁盘选择窗口（容量条为蓝色，空间紧张/不可用时为红色）----
		namespace
		{
			constexpr wchar_t DRIVE_DLG_CLASS[] = L"eBoxDrivePickDialog";
			constexpr int DRIVE_DLG_WIDTH = 600; // 设计尺寸（96 DPI）
			constexpr int DRIVE_ROW_HEIGHT = 88;
			constexpr int DRIVE_ROW_GAP = 10;
			constexpr int DRIVE_LIST_TOP = 96;
			constexpr int DRIVE_FOOTER_HEIGHT = 66;
			constexpr int DRIVE_MAX_VISIBLE_ROWS = 4;
			constexpr int DRIVE_MIN_VISIBLE_ROWS = 3; // 矮屏也至少露出 3 个盘，其余用滚轮查看
			constexpr int DRIVE_ROW_HEIGHT_MIN = 64;  // 行高压缩下限（设计尺寸），再小文字会挤在一起

			struct DrivePickData
			{
				HWND hwnd{nullptr};
				std::vector<DriveEntry> drives;
				int selected{-1};
				int hoverRow{-1};
				int hoverBtn{-1}; // 0=切换 1=取消
				int pressBtn{-1};
				int scrollTop{0};
				int visibleRows{1};
				int dpi{96};
				int rowHeight{0};
				int rowHeightInit{0}; // 创建窗口前按屏幕高度算好的实际行高（像素），0=用标准行高
				int rowGap{0};
				RECT rcList{};
				RECT rcOk{};
				RECT rcCancel{};
				HFONT hFontTitle{nullptr};
				HFONT hFontBold{nullptr};
				HFONT hFontBody{nullptr};
				HFONT hFontSmall{nullptr};
				bool done{false};
				bool cancelled{true};
				std::wstring result;
			};

			RECT drive_row_rect(const DrivePickData& data, int index)
			{
				const int top = data.rcList.top + (index - data.scrollTop) * (data.rowHeight + data.rowGap);
				RECT rc{data.rcList.left, top, data.rcList.right, top + data.rowHeight};
				return rc;
			}

			int drive_hit_row(const DrivePickData& data, POINT pt)
			{
				if (!PtInRect(&data.rcList, pt))
				{
					return -1;
				}
				for (int i = data.scrollTop;
				     i < static_cast<int>(data.drives.size()) && i < data.scrollTop + data.visibleRows; ++i)
				{
					const RECT rc = drive_row_rect(data, i);
					if (PtInRect(&rc, pt))
					{
						return i;
					}
				}
				return -1;
			}

			int drive_hit_button(const DrivePickData& data, POINT pt)
			{
				const bool okEnabled = data.selected >= 0 &&
				                       data.drives[static_cast<std::size_t>(data.selected)].blockedReason.empty();
				if (okEnabled && PtInRect(&data.rcOk, pt))
				{
					return 0;
				}
				if (PtInRect(&data.rcCancel, pt))
				{
					return 1;
				}
				return -1;
			}

			void drive_ensure_visible(DrivePickData& data, int index)
			{
				if (index < data.scrollTop)
				{
					data.scrollTop = index;
				}
				else if (index >= data.scrollTop + data.visibleRows)
				{
					data.scrollTop = index - data.visibleRows + 1;
				}
				const int maxTop = std::max(0, static_cast<int>(data.drives.size()) - data.visibleRows);
				data.scrollTop = std::clamp(data.scrollTop, 0, maxTop);
			}

			void drive_accept(DrivePickData& data)
			{
				if (data.selected < 0)
				{
					return;
				}
				const DriveEntry& entry = data.drives[static_cast<std::size_t>(data.selected)];
				if (!entry.blockedReason.empty())
				{
					return;
				}
				data.result = entry.targetDir;
				data.cancelled = false;
				DestroyWindow(data.hwnd);
			}

			void draw_text_line(HDC hdc, HFONT font, COLORREF color, const std::wstring& text, RECT rc, UINT flags)
			{
				const HGDIOBJ oldFont = SelectObject(hdc, font);
				SetBkMode(hdc, TRANSPARENT);
				SetTextColor(hdc, color);
				DrawTextW(hdc, text.c_str(), -1, &rc, flags);
				SelectObject(hdc, oldFont);
			}

			void draw_radio_dot(HDC hdc, int cx, int cy, int radius, bool checked, bool enabled)
			{
				const COLORREF ring = checked ? CLEAN_ACCENT : (enabled ? RGB(0xb4, 0xbf, 0xcc) : RGB(0xd6, 0xdc, 0xe4));
				HBRUSH hBrush = CreateSolidBrush(checked ? CLEAN_ACCENT : RGB(0xff, 0xff, 0xff));
				HPEN hPen = CreatePen(PS_SOLID, checked ? 1 : 2, ring);
				const HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
				const HGDIOBJ oldPen = SelectObject(hdc, hPen);
				Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
				if (checked)
				{
					const int inner = radius / 2;
					HBRUSH hInner = CreateSolidBrush(RGB(0xff, 0xff, 0xff));
					HPEN hInnerPen = CreatePen(PS_SOLID, 1, RGB(0xff, 0xff, 0xff));
					SelectObject(hdc, hInner);
					SelectObject(hdc, hInnerPen);
					Ellipse(hdc, cx - inner, cy - inner, cx + inner, cy + inner);
					SelectObject(hdc, oldBrush);
					SelectObject(hdc, oldPen);
					DeleteObject(hInner);
					DeleteObject(hInnerPen);
				}
				else
				{
					SelectObject(hdc, oldBrush);
					SelectObject(hdc, oldPen);
				}
				DeleteObject(hBrush);
				DeleteObject(hPen);
			}

			void drive_draw(HDC hdc, DrivePickData& data, const RECT& client)
			{
				const int dpi = data.dpi > 0 ? data.dpi : 96;
				const auto S = [dpi](int value) { return MulDiv(value, dpi, 96); };

				fill_v_gradient(hdc, client, RGB(0xfa, 0xfb, 0xfd), RGB(0xf1, 0xf5, 0xfb));

				RECT rcTitle{S(28), S(18), client.right - S(28), S(50)};
				draw_text_line(hdc, data.hFontTitle, CLEAN_TEXT, L"选择数据存放的磁盘", rcTitle,
				               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
				RECT rcSub{S(28), S(52), client.right - S(28), S(78)};
				draw_text_line(hdc, data.hFontBody, CLEAN_TEXT_SUB,
				               L"环境数据与运行组件会整体迁移到所选磁盘的 eBoxData 目录。", rcSub,
				               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

				// 磁盘列表（超出可见行数时滚动）
				const int saved = SaveDC(hdc);
				IntersectClipRect(hdc, data.rcList.left - S(6), data.rcList.top - S(4),
				                  data.rcList.right + S(6), data.rcList.bottom + S(4));
				for (int i = data.scrollTop;
				     i < static_cast<int>(data.drives.size()) && i < data.scrollTop + data.visibleRows; ++i)
				{
					const RECT row = drive_row_rect(data, i);
					// 行内元素按实际行高等比缩放：矮屏压缩行高时不会重叠或溢出
					const int baseRow = S(DRIVE_ROW_HEIGHT);
					const auto ry = [&](int design)
					{ return row.top + (baseRow > 0 ? MulDiv(design, data.rowHeight, baseRow) : S(design)); };
					const int dotRadius = baseRow > 0 ? MulDiv(S(7), data.rowHeight, baseRow) : S(7);
					const DriveEntry& entry = data.drives[static_cast<std::size_t>(i)];
					const bool enabled = entry.blockedReason.empty();
					const bool selected = (i == data.selected);
					const bool hovered = (i == data.hoverRow) && enabled;

					COLORREF cardFill = RGB(0xff, 0xff, 0xff);
					COLORREF cardBorder = CLEAN_BORDER;
					if (selected && enabled)
					{
						cardFill = RGB(0xec, 0xf4, 0xfe);
						cardBorder = CLEAN_ACCENT;
					}
					else if (hovered)
					{
						cardFill = RGB(0xf7, 0xfa, 0xfe);
						cardBorder = RGB(0xb9, 0xd4, 0xf2);
					}
					draw_round_rect(hdc, row, S(10), cardFill, cardBorder, (selected && enabled) ? 2 : 1);

					const int padLeft = row.left + S(18);
					draw_radio_dot(hdc, padLeft + S(7), ry(26), dotRadius, selected, enabled);

					// 盘符（粗体）+ 卷标 + 系统盘标记
					const int textLeft = padLeft + S(22);
					const std::wstring letter = entry.root;
					HGDIOBJ oldFont = SelectObject(hdc, data.hFontBold);
					SetBkMode(hdc, TRANSPARENT);
					SetTextColor(hdc, enabled ? CLEAN_TEXT : CLEAN_TEXT_SUB);
					SIZE letterSize{};
					GetTextExtentPoint32W(hdc, letter.c_str(), static_cast<int>(letter.size()), &letterSize);
					RECT rcLetter{textLeft, ry(12), textLeft + letterSize.cx + S(4), ry(40)};
					DrawTextW(hdc, letter.c_str(), -1, &rcLetter, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
					SelectObject(hdc, oldFont);

					int x = textLeft + letterSize.cx + S(8);
					std::wstring tail = entry.volumeName;
					if (entry.isSystem)
					{
						tail += tail.empty() ? L"系统盘" : L"（系统盘）";
					}
					if (!tail.empty())
					{
						RECT rcTail{x, ry(12), row.right - S(20), ry(40)};
						draw_text_line(hdc, data.hFontSmall, enabled ? CLEAN_TEXT_SUB : RGB(0xb0, 0xb8, 0xc2), tail,
						               rcTail, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
					}

					// 右侧：可用 / 总容量
					const std::wstring capacity =
						std::format(L"可用 {} / 共 {}", format_bytes(entry.freeBytes), format_bytes(entry.totalBytes));
					RECT rcCap{row.right - S(260), ry(12), row.right - S(20), ry(40)};
					draw_text_line(hdc, data.hFontSmall, enabled ? CLEAN_TEXT_SUB : RGB(0xb0, 0xb8, 0xc2), capacity,
					               rcCap, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

					// 容量条：蓝=正常，红=空间紧张/不可用
					const double usedRatio = entry.totalBytes > 0
						? static_cast<double>(entry.totalBytes - entry.freeBytes) / static_cast<double>(entry.totalBytes)
						: 0.0;
					const bool danger = entry.freeBytes < 1'000'000'000ULL || usedRatio >= 0.9 ||
					                    entry.blockedReason.find(L"空间不足") != std::wstring::npos;
					const COLORREF barColor = danger ? CLEAN_DANGER : RGB(0x2f, 0x80, 0xed);
					RECT rcBar{padLeft, ry(48), row.right - S(20), ry(56)};
					draw_round_rect(hdc, rcBar, S(4), RGB(0xe9, 0xee, 0xf5), RGB(0xe9, 0xee, 0xf5), 1);
					const int barWidth = rcBar.right - rcBar.left;
					const int usedWidth = static_cast<int>(barWidth * std::clamp(usedRatio, 0.0, 1.0));
					if (usedWidth > 0)
					{
						const int barSaved = SaveDC(hdc);
						IntersectClipRect(hdc, rcBar.left, rcBar.top, rcBar.left + usedWidth, rcBar.bottom);
						draw_round_rect(hdc, rcBar, S(4), barColor, barColor, 1);
						RestoreDC(hdc, barSaved);
					}

					// 底行：数据目录说明 / 不可选原因 + 已用百分比
					const std::wstring info = enabled
						? std::format(L"数据目录：{}（自动创建）", entry.targetDir)
						: entry.blockedReason;
					RECT rcInfo{padLeft, ry(60), row.right - S(90), ry(80)};
					draw_text_line(hdc, data.hFontSmall, enabled ? CLEAN_TEXT_SUB : CLEAN_DANGER, info, rcInfo,
					               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
					const std::wstring percent = std::format(L"已用 {:.0f}%", usedRatio * 100.0);
					RECT rcPct{row.right - S(86), ry(60), row.right - S(20), ry(80)};
					draw_text_line(hdc, data.hFontSmall, enabled ? CLEAN_TEXT_SUB : CLEAN_DANGER, percent, rcPct,
					               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
				}
				RestoreDC(hdc, saved);

				// 磁盘数超过可见行数时画一条滚动条，提示还可以往下滚
				const int driveCount = static_cast<int>(data.drives.size());
				if (driveCount > data.visibleRows && data.visibleRows > 0)
				{
					const int trackW = S(4);
					const int trackLeft = data.rcList.right + S(8);
					RECT rcTrack{trackLeft, data.rcList.top, trackLeft + trackW, data.rcList.bottom};
					draw_round_rect(hdc, rcTrack, trackW / 2, RGB(0xe4, 0xea, 0xf2), RGB(0xe4, 0xea, 0xf2), 1);
					const int trackH = rcTrack.bottom - rcTrack.top;
					const int maxTop = driveCount - data.visibleRows;
					const int thumbH = std::max(S(24), trackH * data.visibleRows / driveCount);
					const int thumbY = rcTrack.top + (maxTop > 0 ? (trackH - thumbH) * data.scrollTop / maxTop : 0);
					RECT rcThumb{trackLeft, thumbY, trackLeft + trackW, thumbY + thumbH};
					draw_round_rect(hdc, rcThumb, trackW / 2, RGB(0xb4, 0xbf, 0xcc), RGB(0xb4, 0xbf, 0xcc), 1);
				}

				// 底部：分隔线 + 提示 + 按钮
				const int footerTop = client.bottom - S(DRIVE_FOOTER_HEIGHT);
				HPEN hLine = CreatePen(PS_SOLID, 1, CLEAN_BORDER);
				const HGDIOBJ oldPen = SelectObject(hdc, hLine);
				MoveToEx(hdc, S(28), footerTop, nullptr);
				LineTo(hdc, client.right - S(28), footerTop);
				SelectObject(hdc, oldPen);
				DeleteObject(hLine);

				RECT rcHint{S(28), footerTop + S(6), data.rcCancel.left - S(16), footerTop + S(30)};
				draw_text_line(hdc, data.hFontSmall, CLEAN_TEXT_SUB, L"迁移期间请勿关闭电脑或强制结束程序", rcHint,
				               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

				const bool okEnabled = data.selected >= 0 &&
				                       data.drives[static_cast<std::size_t>(data.selected)].blockedReason.empty();
				draw_modern_dlg_button(hdc, data.rcOk, true, data.hoverBtn == 0 && okEnabled, data.pressBtn == 0,
				                       L"切换到此盘");
				if (!okEnabled)
				{
					// 无可选磁盘：主按钮画成灰底，避免误以为可点击
					draw_round_rect(hdc, data.rcOk, S(8), RGB(0xef, 0xf2, 0xf6), RGB(0xef, 0xf2, 0xf6), 1);
					draw_text_line(hdc, data.hFontBody, CLEAN_TEXT_SUB, L"切换到此盘", data.rcOk,
					               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
				}
				draw_modern_dlg_button(hdc, data.rcCancel, false, data.hoverBtn == 1, data.pressBtn == 1, L"取消");
			}

			void drive_dlg_create_fonts(DrivePickData& data)
			{
				if (data.hFontTitle)
				{
					DeleteObject(data.hFontTitle);
					data.hFontTitle = nullptr;
				}
				if (data.hFontBold)
				{
					DeleteObject(data.hFontBold);
					data.hFontBold = nullptr;
				}
				if (data.hFontBody)
				{
					DeleteObject(data.hFontBody);
					data.hFontBody = nullptr;
				}
				if (data.hFontSmall)
				{
					DeleteObject(data.hFontSmall);
					data.hFontSmall = nullptr;
				}
				data.hFontTitle = CreateFontW(-MulDiv(16, data.dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
				                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
				data.hFontBold = CreateFontW(-MulDiv(13, data.dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
				                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
				data.hFontBody = CreateFontW(-MulDiv(12, data.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
				data.hFontSmall = CreateFontW(-MulDiv(11, data.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			}

			void drive_dlg_layout(DrivePickData& data)
			{
				if (!data.hwnd)
				{
					return;
				}
				const auto S = [dpi = data.dpi](int value) { return ui::dlg_s(value, dpi); };
				RECT client{};
				GetClientRect(data.hwnd, &client);
				data.rowHeight = data.rowHeightInit > 0 ? data.rowHeightInit : S(DRIVE_ROW_HEIGHT);
				data.rowGap = S(DRIVE_ROW_GAP);
				data.rcList.left = S(28);
				data.rcList.right = client.right - S(28);
				data.rcList.top = S(DRIVE_LIST_TOP);
				data.rcList.bottom = client.bottom - S(DRIVE_FOOTER_HEIGHT);
				const int listHeight = data.rcList.bottom - data.rcList.top;
				data.visibleRows = std::max(1, (listHeight + data.rowGap) / (data.rowHeight + data.rowGap));

				const int btnHeight = S(34);
				const int btnTop = client.bottom - S(16) - btnHeight;
				data.rcOk = {client.right - S(28) - S(128), btnTop, client.right - S(28), btnTop + btnHeight};
				data.rcCancel = {data.rcOk.left - S(12) - S(88), btnTop, data.rcOk.left - S(12),
				                 btnTop + btnHeight};
				drive_ensure_visible(data, std::max(0, data.selected));
			}

			LRESULT CALLBACK drive_pick_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
			{
				auto* data = reinterpret_cast<DrivePickData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
				switch (msg)
				{
				case WM_CREATE:
				{
					auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
					data = static_cast<DrivePickData*>(cs->lpCreateParams);
					SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
					data->hwnd = hwnd;

					data->dpi = ui::dlg_dpi(hwnd);
					drive_dlg_create_fonts(*data);
					drive_dlg_layout(*data);
					return 0;
				}
				case WM_DPICHANGED:
				{
					if (data)
					{
						data->dpi = ui::dlg_on_dpi_changed(hwnd, lParam);
						// 行高按新 DPI 重算（创建期为适配矮屏压过的行高不再沿用）
						data->rowHeightInit = 0;
						drive_dlg_create_fonts(*data);
						drive_dlg_layout(*data);
						InvalidateRect(hwnd, nullptr, TRUE);
					}
					return 0;
				}
				case WM_SIZE:
					// 创建后校正客户区尺寸会触发 WM_SIZE，需按最终客户区重新摆放控件
					if (data && data->hwnd && wParam != SIZE_MINIMIZED)
					{
						drive_dlg_layout(*data);
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				case WM_ERASEBKGND:
					return 1;
				case WM_PAINT:
				{
					PAINTSTRUCT ps{};
					const HDC hdc = BeginPaint(hwnd, &ps);
					RECT client{};
					GetClientRect(hwnd, &client);
					drive_draw(hdc, *data, client);
					EndPaint(hwnd, &ps);
					return 0;
				}
				case WM_MOUSEMOVE:
				{
					const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					const int row = drive_hit_row(*data, pt);
					const int btn = drive_hit_button(*data, pt);
					if (row != data->hoverRow || btn != data->hoverBtn)
					{
						data->hoverRow = row;
						data->hoverBtn = btn;
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					// 悬停到可选磁盘时显示手型，提示整行可点
					SetCursor(LoadCursorW(nullptr,
					                      (btn >= 0 || (row >= 0 && data->drives[static_cast<std::size_t>(row)].blockedReason.empty()))
						                      ? IDC_HAND
						                      : IDC_ARROW));
					return 0;
				}
				case WM_LBUTTONDOWN:
				{
					const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					const int btn = drive_hit_button(*data, pt);
					if (btn >= 0)
					{
						data->pressBtn = btn;
						SetCapture(hwnd);
						InvalidateRect(hwnd, nullptr, FALSE);
						return 0;
					}
					const int row = drive_hit_row(*data, pt);
					if (row >= 0 && data->drives[static_cast<std::size_t>(row)].blockedReason.empty())
					{
						data->selected = row;
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				}
				case WM_LBUTTONUP:
				{
					const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
					const int press = data->pressBtn;
					data->pressBtn = -1;
					if (GetCapture() == hwnd)
					{
						ReleaseCapture();
					}
					InvalidateRect(hwnd, nullptr, FALSE);
					const int btn = drive_hit_button(*data, pt);
					if (press >= 0 && btn == press)
					{
						if (btn == 0)
						{
							drive_accept(*data);
						}
						else
						{
							data->cancelled = true;
							DestroyWindow(hwnd);
						}
					}
					return 0;
				}
				case WM_MOUSEWHEEL:
				{
					if (static_cast<int>(data->drives.size()) <= data->visibleRows)
					{
						return 0;
					}
					const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
					const int maxTop = std::max(0, static_cast<int>(data->drives.size()) - data->visibleRows);
					const int top = std::clamp(data->scrollTop + (delta > 0 ? -1 : 1), 0, maxTop);
					if (top != data->scrollTop)
					{
						data->scrollTop = top;
						InvalidateRect(hwnd, nullptr, FALSE);
					}
					return 0;
				}
				case WM_KEYDOWN:
				{
					if (wParam == VK_ESCAPE)
					{
						data->cancelled = true;
						DestroyWindow(hwnd);
						return 0;
					}
					if (wParam == VK_RETURN)
					{
						drive_accept(*data);
						return 0;
					}
					if (wParam == VK_UP || wParam == VK_DOWN)
					{
						const int dir = (wParam == VK_UP) ? -1 : 1;
						for (int i = data->selected + dir; i >= 0 && i < static_cast<int>(data->drives.size()); i += dir)
						{
							if (!data->drives[static_cast<std::size_t>(i)].blockedReason.empty())
							{
								continue;
							}
							data->selected = i;
							drive_ensure_visible(*data, i);
							InvalidateRect(hwnd, nullptr, FALSE);
							break;
						}
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
					if (data->hFontBold)
					{
						DeleteObject(data->hFontBold);
					}
					if (data->hFontBody)
					{
						DeleteObject(data->hFontBody);
					}
					if (data->hFontSmall)
					{
						DeleteObject(data->hFontSmall);
					}
					data->done = true;
					return 0;
				default:
					break;
				}
				return DefWindowProcW(hwnd, msg, wParam, lParam);
			}
		}

		// 弹出磁盘选择窗口；用户取消返回 false，out 为 "<盘符>\eBoxData"。
		bool pick_drive_dialog(HWND parent, const std::wstring& currentDir, std::uint64_t neededBytes,
		                       std::wstring& out)
		{
			static const bool classRegistered = []()
			{
				WNDCLASSEXW wc = {sizeof(wc)};
				wc.lpfnWndProc = drive_pick_proc;
				wc.hInstance = GetModuleHandleW(nullptr);
				wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
				wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
				wc.lpszClassName = DRIVE_DLG_CLASS;
				return RegisterClassExW(&wc) != 0;
			}();
			(void)classRegistered;

			DrivePickData data;
			data.drives = scan_drives(currentDir, neededBytes);
			if (data.drives.empty())
			{
				MessageBoxW(parent, L"未检测到可用的本地磁盘。", MainApp::appName.data(),
				            MB_OK | MB_ICONWARNING | MB_TASKMODAL);
				return false;
			}
			// 默认选中可选盘中剩余空间最大的一个
			for (int i = 0; i < static_cast<int>(data.drives.size()); ++i)
			{
				if (!data.drives[static_cast<std::size_t>(i)].blockedReason.empty())
				{
					continue;
				}
				if (data.selected < 0 ||
				    data.drives[static_cast<std::size_t>(i)].freeBytes >
					    data.drives[static_cast<std::size_t>(data.selected)].freeBytes)
				{
					data.selected = i;
				}
			}

			const int dpi = ui::dlg_dpi(parent);
			const auto S = [dpi](int value) { return ui::dlg_s(value, dpi); };

			// 可见行数：最多 4 行。屏幕矮时先等比压缩行高（不低于 DRIVE_ROW_HEIGHT_MIN），
			// 压到下限仍放不下才减少行数，但优先保住 DRIVE_MIN_VISIBLE_ROWS 行 ——
			// 避免矮分辨率下只露出 1 个盘、D 盘必须滚动才能看到。
			int displayRows = std::clamp(static_cast<int>(data.drives.size()), 1, DRIVE_MAX_VISIBLE_ROWS);
			const int rowGapPx = S(DRIVE_ROW_GAP);
			const int fixedPx = S(DRIVE_LIST_TOP) + S(DRIVE_FOOTER_HEIGHT);
			const int minRowPx = S(DRIVE_ROW_HEIGHT_MIN);
			int rowHeightPx = S(DRIVE_ROW_HEIGHT);
			const SIZE workArea = ui::dlg_work_area(parent);
			if (workArea.cy > 0)
			{
				const int maxClient = workArea.cy - S(48);
				while (displayRows > 1 &&
				       fixedPx + displayRows * rowHeightPx + (displayRows - 1) * rowGapPx > maxClient)
				{
					const int fit = (maxClient - fixedPx - (displayRows - 1) * rowGapPx) / displayRows;
					if (fit >= minRowPx)
					{
						rowHeightPx = std::min(rowHeightPx, fit);
						break;
					}
					if (displayRows > DRIVE_MIN_VISIBLE_ROWS)
					{
						--displayRows;
						continue;
					}
					// 已是最少行数仍放不下：只能继续压缩行高（极小屏兜底）
					rowHeightPx = std::max(1, fit);
					break;
				}
			}
			const int dlgWidth = S(DRIVE_DLG_WIDTH);
			// 实像素逐项相加：MulDiv 舍入会让 listHeight 比整行所需少 1~2px，导致 visibleRows 少算一行
			const int dlgHeight = fixedPx + displayRows * rowHeightPx + (displayRows - 1) * rowGapPx;
			data.rowHeightInit = rowHeightPx;

			const SIZE clientSize{dlgWidth, dlgHeight};
			const HWND hDlg = CreateWindowExW(0, DRIVE_DLG_CLASS, L"选择数据存放的磁盘",
			                                  WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0,
			                                  clientSize.cx, clientSize.cy,
			                                  parent, nullptr, GetModuleHandleW(nullptr), &data);
			if (!hDlg)
			{
				return false;
			}
			ui::dlg_fit_client(hDlg, clientSize);
			ui::dlg_center_on(hDlg, parent);
			ShowWindow(hDlg, SW_SHOW);
			UpdateWindow(hDlg);
			SetFocus(hDlg);

			if (parent && IsWindow(parent))
			{
				EnableWindow(parent, FALSE);
			}

			MSG msg{};
			while (!data.done && GetMessageW(&msg, nullptr, 0, 0))
			{
				TranslateMessage(&msg);
				DispatchMessageW(&msg);
			}

			if (parent && IsWindow(parent))
			{
				EnableWindow(parent, TRUE);
				SetForegroundWindow(parent);
			}

			if (data.cancelled)
			{
				return false;
			}
			out = data.result;
			return true;
		}

		bool validate_data_dir(const std::wstring& currentDir, const std::wstring& targetDir,
		                       std::uint64_t neededBytes, std::wstring& error)
		{
			if (targetDir.empty())
			{
				error = L"请选择目标目录。";
				return false;
			}
			const std::wstring from = normalized_dir(currentDir);
			const std::wstring to = normalized_dir(targetDir);
			if (same_dir(from, to))
			{
				error = L"目标目录与当前目录相同，无需切换。";
				return false;
			}
			if (dir_within(to, from))
			{
				error = L"目标目录不能位于当前数据目录之内。";
				return false;
			}
			if (dir_within(from, to))
			{
				error = L"目标目录不能包含当前数据目录。";
				return false;
			}

			const std::filesystem::path target{to};
			std::error_code ec;
			if (std::filesystem::is_directory(target, ec) && !ec)
			{
				// 目标已有数据时迁移会混杂，直接拒绝，避免覆盖用户已有环境
				const std::filesystem::path envDir = target / L"Env";
				if (std::filesystem::is_directory(envDir, ec) && !ec && !std::filesystem::is_empty(envDir, ec) && !ec)
				{
					error = L"目标目录已存在环境数据（Env），请选择其他目录。";
					return false;
				}
				const std::filesystem::path binDir = target / L"bin";
				if (std::filesystem::is_directory(binDir, ec) && !ec && !std::filesystem::is_empty(binDir, ec) && !ec)
				{
					error = L"目标目录已存在运行组件（bin），请选择其他目录。";
					return false;
				}
			}
			if (!ensure_dir_writable(target, error))
			{
				return false;
			}

			const DiskSpaceInfo space = query_disk_space(to);
			if (space.totalBytes > 0 && space.freeBytes < neededBytes)
			{
				error = std::format(L"目标磁盘可用空间不足：需要约 {}，当前可用 {}。",
				                    format_bytes(neededBytes), format_bytes(space.freeBytes));
				return false;
			}
			return true;
		}
	}

	// ---- 设置中心：功能列表（图标 + 名称 + 说明），后续可继续追加设置项 ----
	namespace
	{
		constexpr wchar_t SETTINGS_DLG_CLASS[] = L"eBoxSettingsDialog";
		constexpr int SETTINGS_DLG_WIDTH = 720; // 设计尺寸（96 DPI）
		constexpr int SETTINGS_DLG_HEIGHT = 520;
		constexpr int SETTINGS_MARGIN = 32;
		constexpr int SETTINGS_LIST_TOP = 112;
		constexpr int SETTINGS_ITEM_HEIGHT = 92;
		constexpr int SETTINGS_ITEM_GAP = 12;
		constexpr int SETTINGS_FOOTER_HEIGHT = 72;

		enum class SettingAction
		{
			ChangeDataDir,
			Repair,
			WxWorkDataRepair,
		};

		struct SettingItem
		{
			std::wstring title;
			std::wstring subtitle;
			SettingAction action{SettingAction::ChangeDataDir};
		};

		struct SettingsDlgData
		{
			HWND hwnd{nullptr};
			std::vector<SettingItem> items;
			std::wstring currentDir;
			std::size_t runningEnvCount{0};
			int hoverItem{-1};
			int hoverBtn{-1}; // 0=关闭
			int pressItem{-1};
			int pressBtn{-1};
			int dpi{96};
			int itemHeight{0};
			int itemGap{0};
			RECT rcList{};
			RECT rcClose{};
			HFONT hFontTitle{nullptr};
			HFONT hFontBold{nullptr};
			HFONT hFontBody{nullptr};
			HFONT hFontSmall{nullptr};
			bool done{false};
		};

		RECT settings_item_rect(const SettingsDlgData& data, int index)
		{
			const int top = data.rcList.top + index * (data.itemHeight + data.itemGap);
			return RECT{data.rcList.left, top, data.rcList.right, top + data.itemHeight};
		}

		int settings_hit_item(const SettingsDlgData& data, POINT pt)
		{
			for (int i = 0; i < static_cast<int>(data.items.size()); ++i)
			{
				const RECT rc = settings_item_rect(data, i);
				if (rc.bottom > data.rcList.bottom)
				{
					break;
				}
				if (PtInRect(&rc, pt))
				{
					return i;
				}
			}
			return -1;
		}

		// 设置项图标：ChangeDataDir=硬盘外形（白色机身 + 插槽 + 指示灯），
		// Repair=盾牌 + 对勾，WxWorkDataRepair=数据库圆筒
		void draw_setting_icon(HDC hdc, const RECT& tile, int dpi, SettingAction action)
		{
			const auto S = [dpi](int value) { return MulDiv(value, dpi, 96); };
			if (action == SettingAction::WxWorkDataRepair)
			{
				const int left = tile.left + S(13);
				const int right = tile.right - S(13);
				const int top = tile.top + S(15);
				const int bottom = tile.bottom - S(14);
				const int eh = S(4); // 圆筒顶/底椭圆的半高
				HBRUSH hBrush = CreateSolidBrush(RGB(0xff, 0xff, 0xff));
				HPEN hPen = CreatePen(PS_SOLID, std::max(1, S(2)), CLEAN_ACCENT);
				const HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
				const HGDIOBJ oldPen = SelectObject(hdc, hPen);
				Rectangle(hdc, left, top, right, bottom);
				Ellipse(hdc, left, bottom - eh, right, bottom + eh);
				Ellipse(hdc, left, top - eh, right, top + eh);
				// 两道分隔弧线，读起来像一摞数据
				const int row1 = top + S(8);
				const int row2 = top + S(16);
				Arc(hdc, left, row1 - eh, right, row1 + eh, right, row1, left, row1);
				Arc(hdc, left, row2 - eh, right, row2 + eh, right, row2, left, row2);
				SelectObject(hdc, oldPen);
				SelectObject(hdc, oldBrush);
				DeleteObject(hBrush);
				DeleteObject(hPen);
				return;
			}
			if (action == SettingAction::Repair)
			{
				const int cx = (tile.left + tile.right) / 2;
				const int top = tile.top + S(10);
				const int bottom = tile.bottom - S(9);
				const int halfW = S(13);
				const POINT shield[6]{
					{cx, top},
					{cx + halfW, top + S(4)},
					{cx + halfW, top + S(17)},
					{cx, bottom},
					{cx - halfW, top + S(17)},
					{cx - halfW, top + S(4)},
				};
				HBRUSH hBrush = CreateSolidBrush(CLEAN_ACCENT);
				HPEN hPen = CreatePen(PS_SOLID, 1, CLEAN_ACCENT);
				const HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
				const HGDIOBJ oldPen = SelectObject(hdc, hPen);
				Polygon(hdc, shield, static_cast<int>(std::size(shield)));

				HPEN hCheck = CreatePen(PS_SOLID, std::max(2, S(2)), RGB(0xff, 0xff, 0xff));
				SelectObject(hdc, hCheck);
				MoveToEx(hdc, cx - S(7), top + S(13), nullptr);
				LineTo(hdc, cx - S(2), top + S(18));
				LineTo(hdc, cx + S(8), top + S(7));
				SelectObject(hdc, oldPen);
				SelectObject(hdc, oldBrush);
				DeleteObject(hCheck);
				DeleteObject(hBrush);
				DeleteObject(hPen);
				return;
			}

			const RECT body{tile.left + S(11), tile.top + S(13), tile.right - S(11), tile.bottom - S(13)};
			draw_round_rect(hdc, body, S(4), RGB(0xff, 0xff, 0xff), CLEAN_ACCENT, std::max(1, S(2)));
			const RECT slot{body.left + S(5), body.top + S(7), body.right - S(5), body.top + S(12)};
			draw_round_rect(hdc, slot, S(2), CLEAN_ACCENT, CLEAN_ACCENT, 1);
			const int radius = std::max(2, S(2));
			const int cx = body.right - S(8);
			const int cy = body.bottom - S(8);
			HBRUSH hBrush = CreateSolidBrush(CLEAN_ACCENT);
			HPEN hPen = CreatePen(PS_SOLID, 1, CLEAN_ACCENT);
			const HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
			const HGDIOBJ oldPen = SelectObject(hdc, hPen);
			Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
			SelectObject(hdc, oldBrush);
			SelectObject(hdc, oldPen);
			DeleteObject(hBrush);
			DeleteObject(hPen);
		}

		void draw_chevron(HDC hdc, int cx, int cy, int size, COLORREF color, int width)
		{
			HPEN hPen = CreatePen(PS_SOLID, width, color);
			const HGDIOBJ oldPen = SelectObject(hdc, hPen);
			MoveToEx(hdc, cx - size / 2, cy - size, nullptr);
			LineTo(hdc, cx + size / 2, cy);
			LineTo(hdc, cx - size / 2, cy + size);
			SelectObject(hdc, oldPen);
			DeleteObject(hPen);
		}

		void settings_draw(HDC hdc, SettingsDlgData& data, const RECT& client)
		{
			const int dpi = data.dpi > 0 ? data.dpi : 96;
			const auto S = [dpi](int value) { return MulDiv(value, dpi, 96); };

			fill_v_gradient(hdc, client, RGB(0xfa, 0xfb, 0xfd), RGB(0xf1, 0xf5, 0xfb));

			RECT rcTitle{S(SETTINGS_MARGIN), S(20), client.right - S(SETTINGS_MARGIN), S(52)};
			draw_text_line(hdc, data.hFontTitle, CLEAN_TEXT, L"设置", rcTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			RECT rcSub{S(SETTINGS_MARGIN), S(54), client.right - S(SETTINGS_MARGIN), S(82)};
			draw_text_line(hdc, data.hFontBody, CLEAN_TEXT_SUB, L"管理 eBox 的数据位置与运行方式", rcSub,
			               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

			for (int i = 0; i < static_cast<int>(data.items.size()); ++i)
			{
				const RECT row = settings_item_rect(data, i);
				if (row.bottom > data.rcList.bottom)
				{
					break;
				}
				const SettingItem& item = data.items[static_cast<std::size_t>(i)];
				const bool hovered = (i == data.hoverItem);
				draw_round_rect(hdc, row, S(12), hovered ? RGB(0xf5, 0xf9, 0xff) : RGB(0xff, 0xff, 0xff),
				                hovered ? RGB(0xb9, 0xd4, 0xf2) : CLEAN_BORDER, 1);

				const RECT tile{row.left + S(20), row.top + S(22), row.left + S(20) + S(48),
				                row.top + S(22) + S(48)};
				draw_round_rect(hdc, tile, S(12), RGB(0xe8, 0xf2, 0xfe), RGB(0xe8, 0xf2, 0xfe), 1);
				draw_setting_icon(hdc, tile, dpi, item.action);

				const int textLeft = tile.right + S(16);
				RECT rcItemTitle{textLeft, row.top + S(20), row.right - S(52), row.top + S(48)};
				draw_text_line(hdc, data.hFontBold, CLEAN_TEXT, item.title, rcItemTitle,
				               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
				RECT rcItemSub{textLeft, row.top + S(50), row.right - S(52), row.top + S(74)};
				draw_text_line(hdc, data.hFontSmall, CLEAN_TEXT_SUB, item.subtitle, rcItemSub,
				               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

				draw_chevron(hdc, row.right - S(28), (row.top + row.bottom) / 2, S(6),
				             hovered ? CLEAN_ACCENT : RGB(0xb0, 0xb8, 0xc2), std::max(1, S(2)));
			}

			// 底部：分隔线 + 提示 + 关闭
			const int footerTop = client.bottom - S(SETTINGS_FOOTER_HEIGHT);
			HPEN hLine = CreatePen(PS_SOLID, 1, CLEAN_BORDER);
			const HGDIOBJ oldPen = SelectObject(hdc, hLine);
			MoveToEx(hdc, S(SETTINGS_MARGIN), footerTop, nullptr);
			LineTo(hdc, client.right - S(SETTINGS_MARGIN), footerTop);
			SelectObject(hdc, oldPen);
			DeleteObject(hLine);

			RECT rcHint{S(SETTINGS_MARGIN), footerTop + S(8), data.rcClose.left - S(16), footerTop + S(52)};
			draw_text_line(hdc, data.hFontSmall, CLEAN_TEXT_SUB, L"更多设置项将在后续版本中加入", rcHint,
			               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
			draw_modern_dlg_button(hdc, data.rcClose, false, data.hoverBtn == 0, data.pressBtn == 0, L"关闭");
		}

		void settings_open_data_dir(SettingsDlgData& data)
		{
			if (data.runningEnvCount > 0)
			{
				// 环境进程会在迁移开始时被强制结束：先征得用户同意，避免误以为"点了没反应"
				const std::wstring tip = std::format(
					L"检测到有 {} 个环境正在运行。\n"
					L"迁移会先强制结束这些环境进程，再开始复制数据（未发送的内容可能丢失）。\n\n是否继续？",
					data.runningEnvCount);
				if (MessageBoxW(data.hwnd, tip.c_str(), MainApp::appName.data(),
				                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TASKMODAL) != IDYES)
				{
					return;
				}
			}

			// 统计当前占用空间（只用于容量校验与提示，点开时才算，避免打开设置卡顿）
			const HCURSOR oldCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
			const std::filesystem::path root{data.currentDir};
			const std::uint64_t usedBytes = dir_size_bytes(root / L"Env") + dir_size_bytes(root / L"bin");
			SetCursor(oldCursor);

			std::wstring picked;
			if (!pick_drive_dialog(data.hwnd, data.currentDir, usedBytes, picked))
			{
				return;
			}
			std::wstring error;
			if (!validate_data_dir(data.currentDir, picked, usedBytes, error))
			{
				MessageBoxW(data.hwnd, error.c_str(), L"无法使用该磁盘", MB_OK | MB_ICONWARNING | MB_TASKMODAL);
				return;
			}

			const std::wstring target = normalized_dir(picked);
			const std::wstring confirm = std::format(
				L"确定要把环境数据迁移到以下目录吗？\n\n{}\n\n迁移完成后，旧目录中的 Env 与 bin 会被删除，"
				L"eBox 将自动重启并继续迁移。",
				target);
			if (MessageBoxW(data.hwnd, confirm.c_str(), L"确认更改数据存储位置",
			                MB_YESNO | MB_ICONQUESTION | MB_TASKMODAL | MB_DEFBUTTON2) != IDYES)
			{
				return;
			}

			if (!request_data_dir_change(target))
			{
				MessageBoxW(data.hwnd, L"无法切换到该目录（重启 eBox 失败），请重试或选择其他磁盘。",
				            MainApp::appName.data(), MB_OK | MB_ICONERROR | MB_TASKMODAL);
				return;
			}
			// 新实例已拉起，本进程即将退出
			data.done = true;
			DestroyWindow(data.hwnd);
		}

		void settings_run_item(SettingsDlgData& data, int index)
		{
			if (index < 0 || index >= static_cast<int>(data.items.size()))
			{
				return;
			}
			switch (data.items[(std::size_t)index].action)
			{
			case SettingAction::ChangeDataDir:
				settings_open_data_dir(data);
				break;
			case SettingAction::Repair:
				run_self_check_and_repair(data.hwnd);
				break;
			case SettingAction::WxWorkDataRepair:
				run_wxwork_data_repair(data.hwnd);
				break;
			}
		}

		void settings_dlg_create_fonts(SettingsDlgData& data)
		{
			auto release = [](HFONT& font)
			{
				if (font)
				{
					DeleteObject(font);
					font = nullptr;
				}
			};
			release(data.hFontTitle);
			release(data.hFontBold);
			release(data.hFontBody);
			release(data.hFontSmall);
			const int dpi = data.dpi > 0 ? data.dpi : 96;
			data.hFontTitle = CreateFontW(-MulDiv(18, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontBold = CreateFontW(-MulDiv(13, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontBody = CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
			data.hFontSmall = CreateFontW(-MulDiv(11, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		}

		void settings_dlg_layout(SettingsDlgData& data)
		{
			if (!data.hwnd)
			{
				return;
			}
			const auto S = [dpi = data.dpi](int value) { return ui::dlg_s(value, dpi); };
			RECT client{};
			GetClientRect(data.hwnd, &client);
			data.itemHeight = S(SETTINGS_ITEM_HEIGHT);
			data.itemGap = S(SETTINGS_ITEM_GAP);
			data.rcList.left = S(SETTINGS_MARGIN);
			data.rcList.right = client.right - S(SETTINGS_MARGIN);
			data.rcList.top = S(SETTINGS_LIST_TOP);
			data.rcList.bottom = client.bottom - S(SETTINGS_FOOTER_HEIGHT) - S(4);

			const int btnHeight = S(34);
			const int btnTop = client.bottom - S(18) - btnHeight;
			data.rcClose = RECT{client.right - S(SETTINGS_MARGIN) - S(92), btnTop,
			                    client.right - S(SETTINGS_MARGIN), btnTop + btnHeight};
		}

		LRESULT CALLBACK settings_dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
		{
			auto* data = reinterpret_cast<SettingsDlgData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
			switch (msg)
			{
			case WM_CREATE:
			{
				auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
				data = static_cast<SettingsDlgData*>(cs->lpCreateParams);
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
				data->hwnd = hwnd;

				data->dpi = ui::dlg_dpi(hwnd);
				settings_dlg_create_fonts(*data);
				settings_dlg_layout(*data);
				return 0;
			}
			case WM_DPICHANGED:
			{
				if (data)
				{
					data->dpi = ui::dlg_on_dpi_changed(hwnd, lParam);
					settings_dlg_create_fonts(*data);
					settings_dlg_layout(*data);
					InvalidateRect(hwnd, nullptr, TRUE);
				}
				return 0;
			}
			case WM_SIZE:
				// 创建后校正客户区尺寸会触发 WM_SIZE，需按最终客户区重新摆放控件
				if (data && data->hwnd && wParam != SIZE_MINIMIZED)
				{
					settings_dlg_layout(*data);
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				return 0;
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
			{
				PAINTSTRUCT ps{};
				const HDC hdc = BeginPaint(hwnd, &ps);
				RECT client{};
				GetClientRect(hwnd, &client);
				settings_draw(hdc, *data, client);
				EndPaint(hwnd, &ps);
				return 0;
			}
			case WM_MOUSEMOVE:
			{
				const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
				const int item = settings_hit_item(*data, pt);
				const int btn = PtInRect(&data->rcClose, pt) ? 0 : -1;
				if (item != data->hoverItem || btn != data->hoverBtn)
				{
					data->hoverItem = item;
					data->hoverBtn = btn;
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				SetCursor(LoadCursorW(nullptr, (item >= 0 || btn >= 0) ? IDC_HAND : IDC_ARROW));
				return 0;
			}
			case WM_LBUTTONDOWN:
			{
				const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
				const int item = settings_hit_item(*data, pt);
				const int btn = PtInRect(&data->rcClose, pt) ? 0 : -1;
				if (item < 0 && btn < 0)
				{
					return 0;
				}
				data->pressItem = item;
				data->pressBtn = btn;
				SetCapture(hwnd);
				return 0;
			}
			case WM_LBUTTONUP:
			{
				const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
				const int pressItem = data->pressItem;
				const int pressBtn = data->pressBtn;
				data->pressItem = -1;
				data->pressBtn = -1;
				if (GetCapture() == hwnd)
				{
					ReleaseCapture();
				}
				InvalidateRect(hwnd, nullptr, FALSE);
				if (pressBtn == 0 && PtInRect(&data->rcClose, pt))
				{
					DestroyWindow(hwnd);
					return 0;
				}
				const int item = settings_hit_item(*data, pt);
				if (pressItem >= 0 && item == pressItem)
				{
					settings_run_item(*data, item);
				}
				return 0;
			}
			case WM_KEYDOWN:
			{
				if (wParam == VK_ESCAPE)
				{
					DestroyWindow(hwnd);
					return 0;
				}
				if (wParam == VK_RETURN && !data->items.empty())
				{
					settings_run_item(*data, data->hoverItem >= 0 ? data->hoverItem : 0);
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
				if (data->hFontBold)
				{
					DeleteObject(data->hFontBold);
				}
				if (data->hFontBody)
				{
					DeleteObject(data->hFontBody);
				}
				if (data->hFontSmall)
				{
					DeleteObject(data->hFontSmall);
				}
				data->done = true;
				return 0;
			default:
				break;
			}
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
	}

	void settings_dialog(const WindowBase* owner, std::size_t runningEnvCount)
	{
		static const bool classRegistered = []()
		{
			WNDCLASSEXW wc = {sizeof(wc)};
			wc.lpfnWndProc = settings_dlg_proc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
			wc.lpszClassName = SETTINGS_DLG_CLASS;
			return RegisterClassExW(&wc) != 0;
		}();
		(void)classRegistered;

		const HWND parent = owner ? owner->nativeHandle() : nullptr;

		SettingsDlgData data;
		data.currentDir = normalized_dir(app().envDataRoot());
		data.runningEnvCount = runningEnvCount;
		// 功能列表：后续新增设置项只需往这里追加（图标按 action 绘制）
		{
			const DiskSpaceInfo space = query_disk_space(data.currentDir);
			SettingItem item;
			item.title = L"数据存放位置";
			item.subtitle = std::format(L"当前：{}（该盘可用 {}）", data.currentDir, format_bytes(space.freeBytes));
			item.action = SettingAction::ChangeDataDir;
			data.items.push_back(std::move(item));
		}
		{
			SettingItem item;
			item.title = L"检查与修复";
			item.subtitle = L"自检运行组件与环境数据，修复被杀毒软件误删的文件";
			item.action = SettingAction::Repair;
			data.items.push_back(std::move(item));
		}
		{
			SettingItem item;
			item.title = L"修复企业微信数据";
			item.subtitle = L"对比企微自带备份，回填丢失或异常的客户数据文件";
			item.action = SettingAction::WxWorkDataRepair;
			data.items.push_back(std::move(item));
		}

		const SIZE clientSize = ui::dlg_client_size(parent, SETTINGS_DLG_WIDTH, SETTINGS_DLG_HEIGHT);
		const HWND hDlg = CreateWindowExW(0, SETTINGS_DLG_CLASS, L"设置", WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0,
		                                  clientSize.cx, clientSize.cy, parent, nullptr,
		                                  GetModuleHandleW(nullptr), &data);
		if (!hDlg)
		{
			return;
		}
		ui::dlg_fit_client(hDlg, clientSize);
		ui::dlg_center_on(hDlg, parent);
		ShowWindow(hDlg, SW_SHOW);
		UpdateWindow(hDlg);
		SetFocus(hDlg);

		if (parent && IsWindow(parent))
		{
			EnableWindow(parent, FALSE);
		}

		MSG msg{};
		while (!data.done && GetMessageW(&msg, nullptr, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		if (parent && IsWindow(parent))
		{
			EnableWindow(parent, TRUE);
			SetForegroundWindow(parent);
		}
	}
}
