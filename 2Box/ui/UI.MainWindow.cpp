module;
#include "res/resource.h"
#define WM_COPYGLOBALDATA 0x0049
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <tlhelp32.h>
#pragma comment(lib, "Comctl32.lib")
#include <commctrl.h>
module UI.MainWindow;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import std;
import MainApp;
import Scheduler;
import Biz.Core;
import UI.Core;
import biz.License;

namespace ui
{
	static constexpr float DESIRED_WIDTH = 1024.f;
	static constexpr float DESIRED_HEIGHT = 768.f;

	static constexpr UINT TRAY_ID = 1;
	static constexpr UINT TRAY_MESSAGE = WM_USER + 9527;

	MainWindow::MainWindow() : WindowBase({MainApp::appName})
	{
		setExitAppWhenWindowDestroyed(true);
		initWindow();
		initWindowPosition();
		initTitleIcon();
		initTray();
		reserveRenderers(2, 20);
		DragAcceptFiles(nativeHandle(), TRUE);
		ChangeWindowMessageFilterEx(nativeHandle(), WM_DROPFILES, MSGFLT_ALLOW, nullptr);
		ChangeWindowMessageFilterEx(nativeHandle(), WM_COPYGLOBALDATA, MSGFLT_ALLOW, nullptr);

		m_btnToTray.setBackgroundColor(D2D1::ColorF(0, 0.f), Button::EState::Normal);
		m_btnToTray.setBackgroundColor(D2D1::ColorF(0, 0.102f), Button::EState::Hover);
		m_btnToTray.setBackgroundColor(D2D1::ColorF(0, 0.208f), Button::EState::Active);
		m_btnToTray.setOnClick([this]
		{
			show(SW_HIDE);
		});
		m_btnToTray.setDrawCallback(std::bind(&MainWindow::drawToTryBtn, this, std::placeholders::_1, std::placeholders::_2));

		// 右上角“授权”按钮：查看授权信息 / 重新激活续期 / 购买 / 客服
		m_btnLicense.setBackgroundColor(D2D1::ColorF(0, 0.f), Button::EState::Normal);
		m_btnLicense.setBackgroundColor(D2D1::ColorF(0, 0.102f), Button::EState::Hover);
		m_btnLicense.setBackgroundColor(D2D1::ColorF(0, 0.208f), Button::EState::Active);
		m_btnLicense.setOnClick([this]
		{
			// 用户点击“重新激活”→ 弹出激活码输入框续期；点击“解绑本机”→ 退出应用
			const ui::LicenseInfoResult infoResult = ui::license_info_dialog(this);
			if (infoResult == ui::LicenseInfoResult::Reactivate)
			{
				if (ui::license_activation_dialog(this))
				{
					reinitWindow(); // 刷新标题栏的到期时间
					MessageBoxW(nativeHandle(), L"续期成功，感谢支持！", L"激活成功", MB_OK | MB_ICONINFORMATION);
				}
			}
			else if (infoResult == ui::LicenseInfoResult::Unbound)
			{
				// 解绑成功：本机退出授权，关闭应用
				app().exit();
			}
		});
		m_btnLicense.setDrawCallback(std::bind(&MainWindow::drawToLicenseBtn, this, std::placeholders::_1, std::placeholders::_2));

		// Win32 tooltip：悬浮授权按钮时显示"授权信息"
		m_hLicenseTooltip = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
		                                    WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		                                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		                                    nativeHandle(), nullptr, GetModuleHandleW(nullptr), nullptr);
		if (m_hLicenseTooltip)
		{
			SendMessageW(m_hLicenseTooltip, TTM_SETMAXTIPWIDTH, 0, 300);
			SendMessageW(m_hLicenseTooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 5000);
		}

#if 0	// 暂时不使用反射注入，就不需要下载pdb了
		initSymbols().detachAndStart();
#else
		// ===== 激活码检查：未激活则先显示系统界面，再弹出激活输入框 =====
		if (!biz::license::isActivated())
		{
			// 先初始化页面并显示窗口（否则主窗口无法绘制），再弹激活框
			changePageTo<HomePage>();
			show(app().cmdShow());
			if (!ui::license_activation_dialog(this))
			{
				// 用户取消或始终未激活：静默退出，不弹任何错误提示
				app().exit();
				return;
			}
			// 激活成功：刷新标题显示到期时间
			reinitWindow();
			return;
		}
		changePageTo<HomePage>();
#endif
	}

	MainWindow::~MainWindow()
	{
		// 引起MainWindow析构，也即导致main函数中的ui::MainWindow mainWnd析构的情况：
		// 1、程序式的主动调用destroyWindow, 引起的app().exit()退出消息循环。
		//		这种情况nativeHandle为空就不能也不需要调用destroyTray了, 因为主动调用destroyWindow引发的onBeforeWindowDestroy中已经destroyTray了
		// 2、用户任何形式的点击关闭，触发onClose且未进行阻止时，会先触发一次onBeforeWindowDestroy进行destroyTray，再由系统调用::DestroyWindow引起的app().exit()退出消息循环.
		//		这种情况和1一样，nativeHandle为空，且已经处理过destroyTray
		// 3、在没有销毁窗口的情况下，直接由于app().exit()而析构，这种情况下父类析构由于无法使用虚函数，子类必须自己在析构中处理额外清理业务。
		//		这种情况nativeHandle还有效，可以且必须destroyTray
		if (nativeHandle())
		{
			destroyTray();
		}
	}

	coro::LazyTask<void> MainWindow::cliCreateProcess(std::wstring exePath, std::wstring params) const
	{
		co_await sched::transfer_to(app().get_scheduler());
		const std::shared_ptr<biz::Env> env = getPage<HomePage>().getLeftSidebar()->getEnvBoxCardArea()->selectSuitableEnvAndSetItBusyTemp(exePath);
		co_await biz::launcher().coRun(env, exePath, params);
		co_return;
	}

	HResult MainWindow::onCreateDeviceResources(ID2D1HwndRenderTarget* renderTarget)
	{
		m_pD2D1Bitmap.reset();
		if (m_bmpIconData.size())
		{
			return renderTarget->CreateBitmap(D2D1::SizeU(m_bmIcon.bmWidth, m_bmIcon.bmHeight),
			                                  m_bmpIconData.data(),
			                                  m_bmIcon.bmWidthBytes,
			                                  D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
			                                                         dpiInfo().dpi, dpiInfo().dpi),
			                                  &m_pD2D1Bitmap);
		}
		return S_OK;
	}

	void MainWindow::onDiscardDeviceResources()
	{
		m_pD2D1Bitmap.reset();
	}

	void MainWindow::draw(const RenderContext& renderCtx)
	{
		// 绘制标题栏
		if (isCompositionEnabled())
		{
			const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
			const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
			const D2D1_RECT_F rc = rect();
			const float width = rc.right - rc.left;
			renderTarget->PushAxisAlignedClip(D2D1::RectF(0.f, 0.f, width - m_captionBtnWidth, m_margins.top), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			renderTarget->Clear(D2D1::ColorF{0, 0.f});
			constexpr float toTrayBthMarginRight = 0.f;
			const float toTrayBthWidth = m_captionBtnWidth / 3.f;
			const float toTrayBthXPos = width - m_captionBtnWidth - toTrayBthMarginRight - toTrayBthWidth;

			float paddingTop{0};
			float paddingLeft{8.f};
			if (IsMaximized(nativeHandle()))
			{
				RECT rcFrame = {};
				AdjustWindowRectEx(&rcFrame, GetWindowStyle(nativeHandle()) & ~WS_CAPTION, FALSE, GetWindowExStyle(nativeHandle()));
				paddingTop -= rcFrame.top;
				paddingLeft -= rcFrame.left;
			}
			const float captionHeight = m_margins.top - paddingTop;
			const float titleIconSize = captionHeight * 0.618f;

			// 授权按钮宽度：与最小化按钮等宽
			const float licenseBtnWidth = toTrayBthWidth;
			const float titleMaxWidth = toTrayBthXPos - licenseBtnWidth - 8.f;

			if (ID2D1Bitmap* bitmap = getTitleIconBitmap(renderTarget))
			{
				const float yPos = (captionHeight - titleIconSize) * 0.5f + paddingTop;
				renderTarget->DrawBitmap(bitmap, D2D1::RectF(paddingLeft, yPos,
				                                             paddingLeft + titleIconSize, yPos + titleIconSize));
			}
			const float textXPos = paddingLeft + titleIconSize + 8.f;
			const float textYPos = (captionHeight - m_titleTextHeight) * 0.5f + paddingTop - 1.f;
			solidBrush->SetColor(D2D1::ColorF{0});
			if (m_pTitleLayout)
			{
				renderTarget->DrawTextLayout(D2D1::Point2F(textXPos, textYPos), m_pTitleLayout, solidBrush);
			}
			else
			{
				renderTarget->DrawTextW(MainApp::appName.data(),
				                        static_cast<UINT32>(MainApp::appName.length()),
				                        app().textFormat().pMainFormat,
				                        D2D1::RectF(textXPos, textYPos, titleMaxWidth, textYPos + m_titleTextHeight),
				                        solidBrush);
			}
			m_btnToTray.setBounds(D2D1::Rect(toTrayBthXPos, paddingTop + 1.f, toTrayBthXPos + toTrayBthWidth, m_margins.top));
			m_btnToTray.draw(renderCtx);
			// 授权信息按钮（托盘按钮左侧，加宽加醒目）
			const float licenseBtnXPos = toTrayBthXPos - licenseBtnWidth;
			m_btnLicense.setBounds(D2D1::Rect(licenseBtnXPos, paddingTop + 1.f, licenseBtnXPos + licenseBtnWidth, m_margins.top));
			m_btnLicense.setDontDrawDefault(true);
			m_btnLicense.draw(renderCtx);
			// 更新 tooltip 工具矩形（物理像素）
			if (m_hLicenseTooltip)
			{
				const float d2p = dpiInfo().deviceToPhysical;
				RECT rcTool{
					static_cast<LONG>(licenseBtnXPos * d2p),
					static_cast<LONG>((paddingTop + 1.f) * d2p),
					static_cast<LONG>((licenseBtnXPos + licenseBtnWidth) * d2p),
					static_cast<LONG>(m_margins.top * d2p)};
				TOOLINFOW ti{};
				ti.cbSize = sizeof(ti);
				ti.uFlags = TTF_SUBCLASS;
				ti.hwnd = nativeHandle();
				ti.uId = 1;
				ti.rect = rcTool;
				ti.lpszText = const_cast<LPWSTR>(L"授权信息");
				SendMessageW(m_hLicenseTooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
				SendMessageW(m_hLicenseTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
			}
			renderTarget->PopAxisAlignedClip();
		}

		currentRenderer()->draw(renderCtx);
	}

	void MainWindow::drawToTryBtn(const RenderContext& renderCtx, Button::EState) const
	{
		const D2D1_RECT_F& bounds = m_btnToTray.getBounds();

		if (isCompositionEnabled())
		{
			const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
			const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
			const float width = bounds.right - bounds.left;
			const float height = bounds.bottom - bounds.top;
			const float contentWidth = width * 0.236f;
			const float contentHeight = height * 0.382f;
			const float paddingLr = width * 0.382f;
			const float paddingTb = height * 0.309f;
			const float contentTopHeight = contentHeight * 0.618f;
			const float contentBottomYPos = paddingTb + contentTopHeight + 0.191f * contentHeight;
			const D2D1_POINT_2F pt1{D2D1::Point2F(paddingLr, paddingTb)};
			const D2D1_POINT_2F pt2{D2D1::Point2F(paddingLr + contentWidth, paddingTb)};
			const D2D1_POINT_2F pt3{D2D1::Point2F((pt1.x + pt2.x) * 0.5f, paddingTb + contentTopHeight)};

			solidBrush->SetColor(D2D1::ColorF(D2D1::ColorF::Black));
			//renderTarget->DrawLine(pt1, pt2, solidBrush, 0.5f);
			renderTarget->DrawLine(pt2, pt3, solidBrush, 0.5f);
			renderTarget->DrawLine(pt3, pt1, solidBrush, 0.5f);

			renderTarget->DrawLine(D2D1::Point2F(pt1.x, contentBottomYPos), D2D1::Point2F(pt2.x, contentBottomYPos), solidBrush, 0.5f);
		}
		else
		{
			HDC hdc = GetWindowDC(nativeHandle());
			const float deviceToPhysical = dpiInfo().deviceToPhysical;
			// 先以设备单位转到非客户区坐标系再转到逻辑单位
			D2D1_RECT_F physicalBounds = D2D1::RectF((bounds.left + m_margins.left) * deviceToPhysical,
			                                         (bounds.top + m_margins.top) * deviceToPhysical,
			                                         (bounds.right + m_margins.left) * deviceToPhysical,
			                                         (bounds.bottom + m_margins.top) * deviceToPhysical);
			const LONG width = static_cast<LONG>(physicalBounds.right - physicalBounds.left);
			const LONG height = static_cast<LONG>(physicalBounds.bottom - physicalBounds.top);
			const LONG contentWidth = static_cast<LONG>(width * 0.382f);
			// const LONG contentHeight = static_cast<LONG>(height * 0.382f);
			const LONG paddingLr = static_cast<LONG>(width * 0.309f);
			// const LONG paddingTb = static_cast<LONG>(height * 0.309f);
			// const LONG contentTopHeight = static_cast<LONG>(contentHeight * 0.618f);

			const RECT rcFill{static_cast<LONG>(physicalBounds.left), static_cast<LONG>(physicalBounds.top), static_cast<LONG>(physicalBounds.right), static_cast<LONG>(physicalBounds.bottom)};
			HBRUSH hbr = CreateSolidBrush(RGB(214, 211, 206));
			FillRect(hdc, &rcFill, hbr);
			DeleteObject(hbr);

			HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
			HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
			Rectangle(hdc, rcFill.left, rcFill.top, rcFill.right, rcFill.bottom);

			// const D2D1_POINT_2U pt1{D2D1::Point2U(rcFill.left + paddingLr, rcFill.top + paddingTb)};
			// const D2D1_POINT_2U pt2{D2D1::Point2U(rcFill.left + paddingLr + contentWidth, pt1.y)};
			// const D2D1_POINT_2U pt3{D2D1::Point2U((pt1.x + pt2.x) / 2, pt1.y + contentTopHeight)};
			// MoveToEx(hdc, pt2.x, pt2.y, nullptr);
			// LineTo(hdc, pt3.x, pt3.y);
			// LineTo(hdc, pt1.x, pt1.y);

			// const LONG contentBottomYPos = static_cast<LONG>(pt3.y + 0.382f * contentHeight);
			// MoveToEx(hdc, pt1.x, contentBottomYPos, nullptr);
			// LineTo(hdc, pt2.x, contentBottomYPos);
			const LONG contentXPos = rcFill.left + paddingLr;
			const LONG contentYPos = rcFill.top + height / 2;
			MoveToEx(hdc, contentXPos, contentYPos, nullptr);
			LineTo(hdc, contentXPos + contentWidth, contentYPos);
			SelectObject(hdc, oldPen);
			DeleteObject(pen);
			ReleaseDC(nativeHandle(), hdc);
		}
	}

	void MainWindow::drawToLicenseBtn(const RenderContext& renderCtx, Button::EState state) const
	{
		// draw() 已将坐标系平移到按钮原点，必须使用本地坐标 (0,0)-(width,height)
		const float width = m_btnLicense.getBounds().right - m_btnLicense.getBounds().left;
		const float height = m_btnLicense.getBounds().bottom - m_btnLicense.getBounds().top;

		if (isCompositionEnabled())
		{
			const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
			const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
			// 纯文字按钮：无背景，悬浮/按下时浅蓝高亮，文字始终蓝色
			if (state == Button::EState::Hover || state == Button::EState::Active)
			{
				solidBrush->SetColor(D2D1::ColorF(0.00784f, 0.4706f, 0.8314f, 0.12f));
				renderTarget->FillRoundedRectangle(
					D2D1::RoundedRect(D2D1::RectF(0.f, 0.f, width, height), 4.f, 4.f),
					solidBrush);
			}
			// 蓝色文字"授权"，水平+垂直居中
			solidBrush->SetColor(D2D1::ColorF(0.00784f, 0.4706f, 0.8314f, 1.f));
			IDWriteTextFormat* const tipsFmt = app().textFormat().pTipsFormat.get();
			const auto oldAlign = tipsFmt->GetTextAlignment();
			tipsFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			renderTarget->DrawTextW(L"授权", 2,
			                        tipsFmt,
			                        D2D1::RectF(0.f, (height - 12.f) * 0.5f, width, (height - 12.f) * 0.5f + 12.f),
			                        solidBrush);
			tipsFmt->SetTextAlignment(oldAlign);
		}
		else
		{
			const D2D1_RECT_F& bounds = m_btnLicense.getBounds();
			HDC hdc = GetWindowDC(nativeHandle());
			const float deviceToPhysical = dpiInfo().deviceToPhysical;
			D2D1_RECT_F physicalBounds = D2D1::RectF((bounds.left + m_margins.left) * deviceToPhysical,
			                                         (bounds.top + m_margins.top) * deviceToPhysical,
			                                         (bounds.right + m_margins.left) * deviceToPhysical,
			                                         (bounds.bottom + m_margins.top) * deviceToPhysical);
			RECT rc{static_cast<LONG>(physicalBounds.left), static_cast<LONG>(physicalBounds.top),
			        static_cast<LONG>(physicalBounds.right), static_cast<LONG>(physicalBounds.bottom)};
			// 纯文字蓝字
			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, RGB(0x00, 0x78, 0xd4));
			HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
			HFONT hOld = static_cast<HFONT>(SelectObject(hdc, hFont));
			DrawTextW(hdc, L"授权", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			SelectObject(hdc, hOld);
			ReleaseDC(nativeHandle(), hdc);
		}
	}

	void MainWindow::onResize(float width, float height)
	{
		if (!std::holds_alternative<std::monostate>(m_pages))
		{
			currentRenderer()->onResize(width, height);
		}
	}

	void MainWindow::onActivate(WParam wParam, LParam lParam)
	{
		if (LOWORD(wParam) != WA_INACTIVE)
		{
			if (isCompositionEnabled())
			{
				RECT rc{};
				DwmGetWindowAttribute(nativeHandle(), DWMWA_CAPTION_BUTTON_BOUNDS, &rc, sizeof(rc));
				m_captionBtnWidth = (rc.right - rc.left) * dpiInfo().physicalToDevice;
			}
			else
			{
				m_captionBtnWidth = (GetSystemMetrics(SM_CXSIZE) + 8) * 3 * dpiInfo().physicalToDevice;
			}
		}

		if (!isCompositionEnabled())
		{
			RedrawWindow(nativeHandle(), nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_NOINTERNALPAINT | RDW_ERASENOW);
		}
	}

	bool MainWindow::onClose()
	{
		const bool hasProc = isPage<HomePage>() && getPage<HomePage>().getLeftSidebar()->getEnvBoxCardArea()->hasAnyProcesses();

		TASKDIALOGCONFIG cfg{};
		cfg.cbSize = sizeof(cfg);
		cfg.hwndParent = nativeHandle();
		cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
		cfg.pszWindowTitle = MainApp::appName.data();
		cfg.pszMainInstruction = L"关闭 2Box？";
		cfg.pszContent = hasProc ? L"仍有进程正在运行。\n\n缩小到托盘：2Box 继续在后台运行，进程不受影响。\n退出应用：将强制结束所有环境中的进程，并退出 2Box。"
		                         : L"缩小到托盘：2Box 继续在后台运行。\n退出应用：结束所有环境中的进程并退出 2Box。";
		cfg.pszMainIcon = TD_WARNING_ICON;
		static constexpr TASKDIALOG_BUTTON buttons[] =
		{
			{100, L"缩小到托盘(&T)"},
			{101, L"退出应用(&E)"},
			{102, L"取消(&C)"},
		};
		cfg.cButtons = ARRAYSIZE(buttons);
		cfg.pButtons = buttons;
		cfg.nDefaultButton = 102;

		int result = 0;
		TaskDialogIndirect(&cfg, &result, nullptr, nullptr);
		if (result == 100)
		{
			// 缩小到托盘
			show(SW_HIDE);
			return true;
		}
		if (result == 101)
		{
			// 退出应用：先结束所有环境中的进程，再销毁窗口退出
			killAllEnvProcesses();
			return false;
		}
		// 取消或直接关闭对话框
		return true;
	}

	void MainWindow::killAllEnvProcesses() const
	{
		const DWORD selfPid = GetCurrentProcessId();

		// 1. 环境中通过 RPC 上报过的进程
		std::vector<DWORD> pids = biz::env_mgr().getAllProcessIdsExclude(0);

		// 2. 递归收集进程树后代（cmd -> 目标程序 -> 其派生的子进程），确保零残留
		std::unordered_map<DWORD, std::vector<DWORD>> parentToChildren;
		{
			HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if (snapshot != INVALID_HANDLE_VALUE)
			{
				PROCESSENTRY32W pe{sizeof(pe)};
				if (Process32FirstW(snapshot, &pe))
				{
					do
					{
						parentToChildren[pe.th32ParentProcessID].push_back(pe.th32ProcessID);
					}
					while (Process32NextW(snapshot, &pe));
				}
				CloseHandle(snapshot);
			}
		}
		std::vector<DWORD> allToKill = pids;
		for (const DWORD pid : pids)
		{
			std::vector<DWORD> stack{pid};
			while (!stack.empty())
			{
				const DWORD cur = stack.back();
				stack.pop_back();
				const auto it = parentToChildren.find(cur);
				if (it == parentToChildren.end())
				{
					continue;
				}
				for (const DWORD child : it->second)
				{
					allToKill.push_back(child);
					stack.push_back(child);
				}
			}
		}

		for (const DWORD pid : allToKill)
		{
			if (pid == selfPid)
			{
				continue;
			}
			if (HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid))
			{
				TerminateProcess(hProc, 0);
				CloseHandle(hProc);
			}
		}
	}

	void MainWindow::onBeforeWindowDestroy()
	{
		destroyTray();
	}

	bool MainWindow::onNcCalcSize(WParam wParam, LParam lParam)
	{
		if (isCompositionEnabled())
		{
			if (wParam)
			{
				return true;
			}
		}
		return false;
	}

	LResult MainWindow::onNcHitTest(WPARAM wParam, LParam lParam, LResult dwmProcessedResult)
	{
		// Get the point coordinates for the hit test.
		POINT ptMouse = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

		if (isCompositionEnabled())
		{
			if (dwmProcessedResult != HTNOWHERE)
			{
				return dwmProcessedResult;
			}
			if (ncBtnHitTest(ptMouse))
			{
				return HTCLIENT;
			}

			// Get the window rectangle.
			RECT rcWindow;
			GetWindowRect(nativeHandle(), &rcWindow);

			// Get the frame rectangle, adjusted for the style without a caption.
			RECT rcFrame = {};
			AdjustWindowRectEx(&rcFrame, GetWindowStyle(nativeHandle()) & ~WS_CAPTION, FALSE, GetWindowExStyle(nativeHandle()));

			// Determine if the hit test is for resizing. Default middle (1,1).
			USHORT uRow = 1;
			USHORT uCol = 1;
			bool fOnResizeBorder = false;

			// Determine if the point is at the top or bottom of the window.
			if (ptMouse.y >= rcWindow.top && ptMouse.y < rcWindow.top + m_physicalMargins.cyTopHeight)
			{
				fOnResizeBorder = (ptMouse.y < (rcWindow.top - rcFrame.top));
				uRow = 0;
			}
			else if (ptMouse.y < rcWindow.bottom && ptMouse.y >= rcWindow.bottom - m_physicalMargins.cyBottomHeight)
			{
				uRow = 2;
			}

			// Determine if the point is at the left or right of the window.
			if (ptMouse.x >= rcWindow.left && ptMouse.x < rcWindow.left + m_physicalMargins.cxLeftWidth)
			{
				uCol = 0; // left side
			}
			else if (ptMouse.x < rcWindow.right && ptMouse.x >= rcWindow.right - m_physicalMargins.cxRightWidth)
			{
				uCol = 2; // right side
			}

			// Hit test (HTTOPLEFT, ... HTBOTTOMRIGHT)
			LRESULT hitTests[3][3] =
			{
				{HTTOPLEFT, fOnResizeBorder ? HTTOP : HTCAPTION, HTTOPRIGHT},
				{HTLEFT, HTNOWHERE, HTRIGHT},
				{HTBOTTOMLEFT, HTBOTTOM, HTBOTTOMRIGHT},
			};
			return hitTests[uRow][uCol];
		}
		// !isCompositionEnabled()
		if (ncBtnHitTest(ptMouse))
		{
			return HTCLIENT;
		}
		return HTNOWHERE;
	}

	void MainWindow::onNcPaint(WParam wParam, LParam lParam)
	{
		// dwm未启用的情况下才会调用这个函数来自绘标题栏新增按钮
		const D2D1_RECT_F rc = rect();
		const float width = rc.right - rc.left;
		constexpr float toTrayBthMarginRight = 0.f;
		const float toTrayBthWidth = m_captionBtnWidth / 3.f;
		const float toTrayBthXPos = width - m_captionBtnWidth - toTrayBthMarginRight - toTrayBthWidth - -m_margins.left;
		m_btnToTray.setBounds(D2D1::Rect(toTrayBthXPos, 6 - m_margins.top, toTrayBthXPos + toTrayBthWidth, -2.f));
		m_btnToTray.setDontDrawDefault(true);
		m_btnToTray.drawImpl(renderContext());
		// 授权信息按钮（托盘按钮左侧，加宽加醒目）
		const float licenseBtnWidth = toTrayBthWidth * 1.7f;
		m_btnLicense.setBounds(D2D1::Rect(toTrayBthXPos - licenseBtnWidth, 6 - m_margins.top, toTrayBthXPos, -2.f));
		m_btnLicense.setDontDrawDefault(true);
		m_btnLicense.drawImpl(renderContext());
	}

	void MainWindow::onDropFiles(WParam wParam)
	{
		HDROP hDrop = reinterpret_cast<HDROP>(wParam);
		if (UINT length = DragQueryFileW(hDrop, 0, nullptr, 0))
		{
			std::wstring filePath;
			filePath.resize(length + 1);
			length = DragQueryFileW(hDrop, 0, filePath.data(), static_cast<UINT>(filePath.size()));
			if (length && isPage<HomePage>())
			{
				getPage<HomePage>().getLeftSidebar()->getStartAppDiv()->launchFile(filePath);
			}
		}
		DragFinish(hDrop);
	}

	void MainWindow::onDwmCompositionChanged()
	{
		reinitWindow();
	}

	void MainWindow::onUserMsg(UINT message, WParam wParam, LParam lParam)
	{
		if (message == TRAY_MESSAGE)
		{
			if (lParam == WM_LBUTTONUP)
			{
				show(SW_RESTORE);
				SetForegroundWindow(nativeHandle());
			}
			else if (lParam == WM_RBUTTONUP)
			{
				HMENU hMenu = CreatePopupMenu();
				if (!hMenu)
				{
					return;
				}
				// 从下往上插入，最终菜单顺序（从上到下）：显示主界面 / 启动新进程 / 分隔线 / 退出
				{
					MENUITEMINFOW menuItem = {sizeof(MENUITEMINFOW)};
					menuItem.fMask = MIIM_ID | MIIM_STRING;
					wchar_t szText[] = {L"退出(&X)"};
					menuItem.dwTypeData = szText;
					menuItem.cch = sizeof(szText) / sizeof(wchar_t);
					menuItem.wID = 3;
					InsertMenuItemW(hMenu, 0, TRUE, &menuItem);
				}
				{
					MENUITEMINFOW menuItem = {sizeof(MENUITEMINFOW)};
					menuItem.fMask = MIIM_FTYPE;
					menuItem.fType = MFT_SEPARATOR;
					InsertMenuItemW(hMenu, 0, TRUE, &menuItem);
				}
				{
					MENUITEMINFOW menuItem = {sizeof(MENUITEMINFOW)};
					menuItem.fMask = MIIM_ID | MIIM_STRING;
					wchar_t szText[] = {L"启动新进程(&N)"};
					menuItem.dwTypeData = szText;
					menuItem.cch = sizeof(szText) / sizeof(wchar_t);
					menuItem.wID = 2;
					InsertMenuItemW(hMenu, 0, TRUE, &menuItem);
				}
				{
					MENUITEMINFOW menuItem = {sizeof(MENUITEMINFOW)};
					menuItem.fMask = MIIM_ID | MIIM_STRING;
					wchar_t szText[] = {L"显示主界面(&S)"};
					menuItem.dwTypeData = szText;
					menuItem.cch = sizeof(szText) / sizeof(wchar_t);
					menuItem.wID = 1;
					InsertMenuItemW(hMenu, 0, TRUE, &menuItem);
				}
				POINT pt{};
				GetCursorPos(&pt);
				SetForegroundWindow(nativeHandle());
				UINT id = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RETURNCMD, pt.x, pt.y, nativeHandle(), nullptr);
				SendMessageW(nativeHandle(), WM_NULL, 0, 0);
				DestroyMenu(hMenu);
				if (id == 1)
				{
					// 显示主界面
					show(SW_RESTORE);
					SetForegroundWindow(nativeHandle());
				}
				else if (id == 2)
				{
					// 启动新进程：选择程序后在新环境打开
					if (std::optional<std::wstring> path = ui::select_file(this))
					{
						getPage<HomePage>().getLeftSidebar()->getEnvBoxCardArea()->launchProcessInNewEnv(*path);
					}
				}
				else if (id == 3)
				{
					// 退出：结束所有环境中的进程并退出
					killAllEnvProcesses();
					destroyWindow();
				}
			}
		}
	}

	void MainWindow::initWindow()
	{
		RECT rcFrame = {};
		AdjustWindowRectEx(&rcFrame, GetWindowStyle(nativeHandle()), FALSE, GetWindowExStyle(nativeHandle()));

		m_physicalMargins.cxLeftWidth = -rcFrame.left;
		m_physicalMargins.cxRightWidth = rcFrame.right;
		m_physicalMargins.cyTopHeight = -rcFrame.top;
		m_physicalMargins.cyBottomHeight = rcFrame.bottom;

		const float physicalToDevice = dpiInfo().physicalToDevice;
		m_margins.left = m_physicalMargins.cxLeftWidth * physicalToDevice;
		m_margins.top = m_physicalMargins.cyTopHeight * physicalToDevice;
		m_margins.right = m_physicalMargins.cxRightWidth * physicalToDevice;
		m_margins.bottom = m_physicalMargins.cyBottomHeight * physicalToDevice;

		m_titleTextHeight = 20.f;
		m_pTitleLayout.reset();
		// 标题：2Box v2.7.0   更新时间：2026/8/7   [到期：yyyy-MM-dd]
		const std::wstring expireText = biz::license::expireDateText();
		const std::wstring titleText = expireText.empty()
			? std::format(L"{} {}   更新时间：{}",
			              MainApp::appName, MainApp::appVersion, MainApp::appUpdateDate)
			: std::format(L"{} {}   更新时间：{}   到期：{}",
			              MainApp::appName, MainApp::appVersion, MainApp::appUpdateDate, expireText);
		if (SUCCEEDED(app().dWriteFactory()->CreateTextLayout(titleText.c_str(),
			static_cast<UINT32>(titleText.size()),
			app().textFormat().pMainFormat,
			std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
			&m_pTitleLayout)))
		{
			DWRITE_TEXT_METRICS textMetrics;
			if (SUCCEEDED(m_pTitleLayout->GetMetrics(&textMetrics)))
			{
				m_titleTextHeight = textMetrics.height;
			}
		}

		if (isCompositionEnabled())
		{
			m_btnToTray.setDontDrawDefault(false);
			DWM_SYSTEMBACKDROP_TYPE t = DWMSBT_MAINWINDOW;
			MARGINS extendMargin = m_physicalMargins;
			const HRESULT hr = DwmSetWindowAttribute(nativeHandle(), DWMWA_SYSTEMBACKDROP_TYPE, &t, sizeof(t));
			if (hr != S_OK)
			{
				extendMargin.cxLeftWidth = 0;
				extendMargin.cxRightWidth = 0;
				extendMargin.cyBottomHeight = 0;
				m_margins.left = 0;
				m_margins.right = 0;
				m_margins.bottom = 0;
			}
			DwmExtendFrameIntoClientArea(nativeHandle(), &extendMargin);
		}
		else
		{
			m_btnToTray.setDontDrawDefault(true);
		}
	}

	void MainWindow::reinitWindow()
	{
		initWindow();
		m_pD2D1Bitmap.reset();
		if (isPage<HomePage>())
		{
			getPage<HomePage>().setMargins(m_margins);
			const D2D_RECT_F rc = rect();
			getPage<HomePage>().onResize(rc.right - rc.left, rc.bottom - rc.top);
		}
		// else if (isPage<DownloadPage>())
		// {
		// 	getPage<DownloadPage>().setMargins(m_margins);
		// 	const D2D_RECT_F rc = rect();
		// 	getPage<HomePage>().onResize(rc.right - rc.left, rc.bottom - rc.top);
		// }
		invalidateRect();
	}

	void MainWindow::initWindowPosition()
	{
		const auto rc = rect();
		const float width = rc.right - rc.left;
		const float height = rc.bottom - rc.top;

		const float desiredWidth = std::min(DESIRED_WIDTH, width);
		const float desiredHeight = std::min(DESIRED_HEIGHT, height);

		const float diffWidth = width - desiredWidth;
		const float diffHeight = height - desiredHeight;

		const float desiredX = rc.left + diffWidth * 0.5f;
		const float desiredY = rc.top + diffHeight * 0.5f;

		setRect(D2D1::RectF(
			        desiredX,
			        desiredY,
			        desiredX + desiredWidth,
			        desiredY + desiredHeight
		        ), SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}

	void MainWindow::initTitleIcon()
	{
		m_hIcon = LoadIconW(app().moduleInstance(), MAKEINTRESOURCE(IDI_APP_ICON));
		if (!m_hIcon)
		{
			return;
		}

		ICONINFO ii{};
		if (!GetIconInfo(m_hIcon, &ii))
		{
			return;
		}
		if (ii.hbmMask)
		{
			DeleteObject(ii.hbmMask);
		}
		if (!ii.hbmColor)
		{
			return;
		}
		HDC hdcWindow = GetDC(nativeHandle());
		do
		{
			if (!GetObjectW(ii.hbmColor, sizeof(m_bmIcon), &m_bmIcon))
			{
				break;
			}

			// 只处理BI_BITFIELDS类型， 后跟3个DWORD
			std::vector<std::byte> bmiData(sizeof(BITMAPINFOHEADER) + 3 * sizeof(RGBQUAD));
			BITMAPINFO& bmi = *reinterpret_cast<BITMAPINFO*>(bmiData.data());
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			if (!GetDIBits(hdcWindow, ii.hbmColor, 0, 0, nullptr, &bmi, DIB_RGB_COLORS))
			{
				break;
			}
			// 只处理BI_BITFIELDS类型
			if (bmi.bmiHeader.biCompression != BI_BITFIELDS)
			{
				break;
			}
			m_bmpIconData.resize(bmi.bmiHeader.biSizeImage);
			bmi.bmiHeader.biHeight = -bmi.bmiHeader.biHeight;
			if (!GetDIBits(hdcWindow, ii.hbmColor, 0, bmi.bmiHeader.biHeight, m_bmpIconData.data(), &bmi, DIB_RGB_COLORS))
			{
				break;
			}
			// RGBQUAD redMask = bmi.bmiColors[0];
			// RGBQUAD greenMask = bmi.bmiColors[1];  
			// RGBQUAD blueMask = bmi.bmiColors[2];
		}
		while (false);
		DeleteObject(ii.hbmColor);
		ReleaseDC(nativeHandle(), hdcWindow);
	}

	void MainWindow::initTray() const
	{
		NOTIFYICONDATAW nid = {sizeof(nid)};
		nid.hWnd = nativeHandle();
		nid.uID = TRAY_ID;
		nid.uFlags = NIF_ICON | NIF_MESSAGE;
		nid.uCallbackMessage = TRAY_MESSAGE;
		nid.hIcon = m_hIcon;

		Shell_NotifyIconW(NIM_ADD, &nid);
	}

	void MainWindow::destroyTray() const
	{
		NOTIFYICONDATAW nid = {sizeof(nid)};
		nid.hWnd = nativeHandle();
		nid.uID = TRAY_ID;
		Shell_NotifyIconW(NIM_DELETE, &nid);
	}

	ID2D1Bitmap* MainWindow::getTitleIconBitmap(ID2D1HwndRenderTarget* renderTarget)
	{
		if (m_bmpIconData.size())
		{
			if (!m_pD2D1Bitmap)
			{
				renderTarget->CreateBitmap(D2D1::SizeU(m_bmIcon.bmWidth, m_bmIcon.bmHeight),
				                           m_bmpIconData.data(),
				                           m_bmIcon.bmWidthBytes,
				                           D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
				                                                  dpiInfo().dpi, dpiInfo().dpi),
				                           &m_pD2D1Bitmap);
			}
			return m_pD2D1Bitmap;
		}
		return nullptr;
	}

	// coro::LazyTask<void> MainWindow::initSymbols()
	// {
	// 	changePageTo<DownloadPage>();
	//
	// 	co_await getPage<DownloadPage>().untilSuccess();
	//
	// 	if (getPage<DownloadPage>().isFileVerified())
	// 	{
	// 		changePageTo<HomePage>();
	// 	}
	// 	co_return;
	// }

	bool MainWindow::ncBtnHitTest(POINT pt) const
	{
		//这里的入参pt是相对于屏幕的
		ScreenToClient(nativeHandle(), &pt);
		const D2D1_POINT_2F local = D2D1::Point2F(pt.x * dpiInfo().physicalToDevice, pt.y * dpiInfo().physicalToDevice);
		return m_btnToTray.hitTest(local) || m_btnLicense.hitTest(local);
	}
}
