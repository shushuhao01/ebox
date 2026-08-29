module;
#include "res/resource.h"
#define WM_COPYGLOBALDATA 0x0049
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <tlhelp32.h>
#include <cstdio>
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
import biz.Update;

namespace ui
{
	static constexpr float DESIRED_WIDTH = 1024.f;
	static constexpr float DESIRED_HEIGHT = 768.f;

	static constexpr UINT TRAY_ID = 1;
	static constexpr UINT TRAY_MESSAGE = WM_USER + 9527;
	// 升级检查协程完成回调（跨线程：IO 线程 → UI 线程）
	static constexpr UINT WM_UPDATE_CHECK_DONE = WM_USER + 9528;
	// 心跳线程收到服务端系统公告 → 刷新公告栏（与 biz.License 的 WM_APP_LICENSENOTICE 一致）
	static constexpr UINT WM_APP_LICENSENOTICE = WM_USER + 9529;
	// 心跳线程发现授权被服务端锁定 → 请求 UI 线程弹窗（lParam=堆上 std::wstring*，UI 侧接管释放；
	// 与 biz.License 的 WM_APP_LICENSEINVALID 一致）
	static constexpr UINT WM_APP_LICENSEINVALID = WM_USER + 9530;
	// 退出清理（后台线程）已完成 → UI 线程销毁窗口并退出（保证进程终止先于应用退出）
	static constexpr UINT WM_APP_EXIT_AFTER_KILL = WM_USER + 9531;

	// 同步悬浮提示工具：首次注册；之后仅矩形/文字变化时才发消息更新。
	// 不能每帧 DELTOOL+ADDTOOL——那会重置 tooltip 弹出计时器，持续渲染的主窗口提示永远弹不出来。
	static void sync_tooltip(HWND hTip, UINT id, HWND owner, const RECT& rc, const wchar_t* text,
	                         bool& added, RECT& lastRc, std::wstring& lastText)
	{
		if (!hTip)
		{
			return;
		}
		TOOLINFOW ti{};
		ti.cbSize = sizeof(ti);
		ti.uFlags = TTF_SUBCLASS;
		ti.hwnd = owner;
		ti.uId = id;
		ti.rect = rc;
		ti.lpszText = const_cast<LPWSTR>(text);
		if (!added)
		{
			SendMessageW(hTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
			added = true;
		}
		else
		{
			if (memcmp(&rc, &lastRc, sizeof(RECT)) != 0)
			{
				SendMessageW(hTip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&ti));
			}
			if (lastText != text)
			{
				SendMessageW(hTip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
			}
		}
		lastRc = rc;
		lastText = text;
	}

	MainWindow::MainWindow() : WindowBase({MainApp::appName})
	{
		setExitAppWhenWindowDestroyed(true);
		initWindow();
		initWindowPosition();
		initTitleIcon();
		initTray();
		// 升级恢复：清理上次成功升级遗留的 .bak；若上次 bat 更新失败（已回滚）则提示用户
		biz::update::cleanupBackupIfNeeded();
		{
			const std::wstring failFlag = biz::update::getUpdateFailFlagPath();
			if (GetFileAttributesW(failFlag.c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				DeleteFileW(failFlag.c_str());
				MessageBoxW(nativeHandle(), L"上次更新未能成功完成，已自动恢复原版本，请重试更新。",
				            MainApp::appName.data(), MB_OK | MB_ICONWARNING);
			}
		}
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

		// 右上角"更新"按钮：有新版本时亮红点，点击查看更新内容
		m_btnUpdate.setBackgroundColor(D2D1::ColorF(0, 0.f), Button::EState::Normal);
		m_btnUpdate.setBackgroundColor(D2D1::ColorF(0, 0.102f), Button::EState::Hover);
		m_btnUpdate.setBackgroundColor(D2D1::ColorF(0, 0.208f), Button::EState::Active);
		m_btnUpdate.setOnClick([this] { showUpdateDialog(); });
		m_btnUpdate.setDrawCallback(std::bind(&MainWindow::drawToUpdateBtn, this, std::placeholders::_1, std::placeholders::_2));
		m_btnUpdate.setDontDrawDefault(true);
		// Win32 tooltip：悬浮更新按钮
		m_hUpdateTooltip = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
		                                   WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		                                   nativeHandle(), nullptr, GetModuleHandleW(nullptr), nullptr);
		if (m_hUpdateTooltip)
		{
			SendMessageW(m_hUpdateTooltip, TTM_SETMAXTIPWIDTH, 0, 300);
			SendMessageW(m_hUpdateTooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 5000);
		}

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

		// 右上角"帮助"按钮（更新按钮左侧）：点击弹出常见问题对话框
		m_btnHelp.setBackgroundColor(D2D1::ColorF(0, 0.f), Button::EState::Normal);
		m_btnHelp.setBackgroundColor(D2D1::ColorF(0, 0.102f), Button::EState::Hover);
		m_btnHelp.setBackgroundColor(D2D1::ColorF(0, 0.208f), Button::EState::Active);
		m_btnHelp.setOnClick([this] { ui::faq_dialog(this); });
		m_btnHelp.setDrawCallback(std::bind(&MainWindow::drawToHelpBtn, this, std::placeholders::_1, std::placeholders::_2));
		m_btnHelp.setDontDrawDefault(true);
		// Win32 tooltip：悬浮帮助按钮
		m_hHelpTooltip = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
		                                 WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		                                 nativeHandle(), nullptr, GetModuleHandleW(nullptr), nullptr);
		if (m_hHelpTooltip)
		{
			SendMessageW(m_hHelpTooltip, TTM_SETMAXTIPWIDTH, 0, 300);
			SendMessageW(m_hHelpTooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 5000);
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
				// 用户取消或始终未激活：彻底退出，避免后台残留进程。
				// 说明：仅调用 app().exit() 时，事件循环依赖 WM_QUIT 被正确取出才退出，
				// 取消路径实测存在进程残留（再次点击会提示"已运行"却找不到窗口）。
				// 因此先清理托盘图标，再直接结束进程，确保二次启动可正常弹出激活框。
				if (nativeHandle())
				{
					destroyTray();
				}
				ExitProcess(0);
			}
			// 激活成功：刷新标题显示到期时间
			reinitWindow();
			return;
		}
		changePageTo<HomePage>();
		startUpdateCheck();
		// 每 6 小时复检一次（调度器周期任务，窗口销毁时 request_stop 取消）
		app().get_scheduler().addPeriodicTimer(std::chrono::hours(6), [this]
		{
			if (nativeHandle())
			{
				startUpdateCheck();
			}
		}, m_updateTimerStop.get_token());
#endif
	}

	MainWindow::~MainWindow()
	{
		// 先停定时器并取消进行中的检查/下载协程，再 join 等待全部协程退出，避免析构阻塞
		m_updateTimerStop.request_stop();
		m_dlStopSource.request_stop();
		m_updateScope.join();
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

			// 授权按钮宽度：与最小化按钮等宽；更新/帮助按钮同宽
			const float licenseBtnWidth = toTrayBthWidth;
			const float updateBtnWidth = toTrayBthWidth;
			const float helpBtnWidth = toTrayBthWidth;
			const float titleMaxWidth = toTrayBthXPos - licenseBtnWidth - updateBtnWidth - helpBtnWidth - 8.f;

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
			// 更新按钮（授权按钮左侧）：有新版本时亮红点
			const float updateBtnXPos = licenseBtnXPos - updateBtnWidth;
			m_btnUpdate.setBounds(D2D1::Rect(updateBtnXPos, paddingTop + 1.f, updateBtnXPos + updateBtnWidth, m_margins.top));
			m_btnUpdate.draw(renderCtx);
			if (m_hUpdateTooltip)
			{
				const float d2p = dpiInfo().deviceToPhysical;
				RECT rcTool{
					static_cast<LONG>(updateBtnXPos * d2p),
					static_cast<LONG>((paddingTop + 1.f) * d2p),
					static_cast<LONG>((updateBtnXPos + updateBtnWidth) * d2p),
					static_cast<LONG>(m_margins.top * d2p)};
				sync_tooltip(m_hUpdateTooltip, 2, nativeHandle(), rcTool,
				             m_hasUpdate ? L"发现新版本，点击更新" : L"检查更新",
				             m_updateTipAdded, m_lastUpdateTipRect, m_lastUpdateTipText);
			}
			// 帮助按钮（更新按钮左侧）：点击查看常见问题
			const float helpBtnXPos = updateBtnXPos - helpBtnWidth;
			m_btnHelp.setBounds(D2D1::Rect(helpBtnXPos, paddingTop + 1.f, helpBtnXPos + helpBtnWidth, m_margins.top));
			m_btnHelp.draw(renderCtx);
			if (m_hHelpTooltip)
			{
				const float d2p = dpiInfo().deviceToPhysical;
				RECT rcTool{
					static_cast<LONG>(helpBtnXPos * d2p),
					static_cast<LONG>((paddingTop + 1.f) * d2p),
					static_cast<LONG>((helpBtnXPos + helpBtnWidth) * d2p),
					static_cast<LONG>(m_margins.top * d2p)};
				sync_tooltip(m_hHelpTooltip, 3, nativeHandle(), rcTool, L"常见问题",
				             m_helpTipAdded, m_lastHelpTipRect, m_lastHelpTipText);
			}
			// 更新 tooltip 工具矩形（物理像素）
			if (m_hLicenseTooltip)
			{
				const float d2p = dpiInfo().deviceToPhysical;
				RECT rcTool{
					static_cast<LONG>(licenseBtnXPos * d2p),
					static_cast<LONG>((paddingTop + 1.f) * d2p),
					static_cast<LONG>((licenseBtnXPos + licenseBtnWidth) * d2p),
					static_cast<LONG>(m_margins.top * d2p)};
				sync_tooltip(m_hLicenseTooltip, 1, nativeHandle(), rcTool, L"授权信息",
				             m_licenseTipAdded, m_lastLicenseTipRect, m_lastLicenseTipText);
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
			// 到期提醒红点：剩余 1~7 天时右上角亮红点（点击"授权"查看详情，不重复弹窗提醒）
			if (m_licenseRemindDays >= 1 && m_licenseRemindDays <= 7)
			{
				solidBrush->SetColor(D2D1::ColorF(0.929f, 0.1176f, 0.1176f, 1.f));  // #ED1E1E
				const float dotR = std::min(width, height) * 0.13f;
				renderTarget->FillEllipse(
					D2D1::Ellipse(D2D1::Point2F(width - dotR - 2.f, dotR + 2.f), dotR, dotR),
					solidBrush);
			}
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
			// 到期提醒红点：剩余 1~7 天时右上角亮红点
			if (m_licenseRemindDays >= 1 && m_licenseRemindDays <= 7)
			{
				const LONG dotR = std::max<LONG>(3, static_cast<LONG>((rc.right - rc.left) * 0.07f));
				HBRUSH hRed = CreateSolidBrush(RGB(0xED, 0x1E, 0x1E));
				HBRUSH hOldBr = static_cast<HBRUSH>(SelectObject(hdc, hRed));
				HPEN hOldPn = static_cast<HPEN>(SelectObject(hdc, GetStockObject(NULL_PEN)));
				Ellipse(hdc, rc.right - dotR * 2 - 1, rc.top + 1, rc.right - 1, rc.top + dotR * 2 + 1);
				SelectObject(hdc, hOldBr);
				SelectObject(hdc, hOldPn);
				DeleteObject(hRed);
			}
		SelectObject(hdc, hOld);
		ReleaseDC(nativeHandle(), hdc);
	}
}

	void MainWindow::drawToUpdateBtn(const RenderContext& renderCtx, Button::EState state) const
	{
		// draw() 已将坐标系平移到按钮原点，使用本地坐标 (0,0)-(width,height)
		const float width = m_btnUpdate.getBounds().right - m_btnUpdate.getBounds().left;
		const float height = m_btnUpdate.getBounds().bottom - m_btnUpdate.getBounds().top;

		if (isCompositionEnabled())
		{
			const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
			const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
			// 悬浮/按下时浅蓝高亮
			if (state == Button::EState::Hover || state == Button::EState::Active)
			{
				solidBrush->SetColor(D2D1::ColorF(0.00784f, 0.4706f, 0.8314f, 0.12f));
				renderTarget->FillRoundedRectangle(
					D2D1::RoundedRect(D2D1::RectF(0.f, 0.f, width, height), 4.f, 4.f),
					solidBrush);
			}
			// 绘制"下载"图标：向下箭头 + 底线（简约线图标）
			const float cx = width * 0.5f;
			const float cy = height * 0.5f;
			const float arrowSize = std::min(width, height) * 0.34f;
			// 箭头颜色：有更新用主题蓝，无更新用灰色
			solidBrush->SetColor(m_hasUpdate
			                     ? D2D1::ColorF(0.00784f, 0.4706f, 0.8314f, 1.f)
			                     : D2D1::ColorF(0.45f, 0.45f, 0.45f, 1.f));
			// 箭头杆（竖线）
			renderTarget->DrawLine(
				D2D1::Point2F(cx, cy - arrowSize * 0.5f),
				D2D1::Point2F(cx, cy + arrowSize * 0.25f),
				solidBrush, 1.5f);
			// 箭头头（V 形左半 + 右半）
			renderTarget->DrawLine(
				D2D1::Point2F(cx - arrowSize * 0.35f, cy - arrowSize * 0.05f),
				D2D1::Point2F(cx, cy + arrowSize * 0.25f),
				solidBrush, 1.5f);
			renderTarget->DrawLine(
				D2D1::Point2F(cx + arrowSize * 0.35f, cy - arrowSize * 0.05f),
				D2D1::Point2F(cx, cy + arrowSize * 0.25f),
				solidBrush, 1.5f);
			// 底线
			renderTarget->DrawLine(
				D2D1::Point2F(cx - arrowSize * 0.55f, cy + arrowSize * 0.55f),
				D2D1::Point2F(cx + arrowSize * 0.55f, cy + arrowSize * 0.55f),
				solidBrush, 1.5f);
			// 红点：有更新时显示在右上角
			if (m_hasUpdate)
			{
				solidBrush->SetColor(D2D1::ColorF(0.929f, 0.1176f, 0.1176f, 1.f));  // #ED1E1E
				const float dotR = std::min(width, height) * 0.13f;
				renderTarget->FillEllipse(
					D2D1::Ellipse(D2D1::Point2F(width - dotR - 2.f, dotR + 2.f), dotR, dotR),
					solidBrush);
			}
		}
		else
		{
			const D2D1_RECT_F& bounds = m_btnUpdate.getBounds();
			HDC hdc = GetWindowDC(nativeHandle());
			const float deviceToPhysical = dpiInfo().deviceToPhysical;
			D2D1_RECT_F physicalBounds = D2D1::RectF((bounds.left + m_margins.left) * deviceToPhysical,
			                                         (bounds.top + m_margins.top) * deviceToPhysical,
			                                         (bounds.right + m_margins.left) * deviceToPhysical,
			                                         (bounds.bottom + m_margins.top) * deviceToPhysical);
			RECT rc{static_cast<LONG>(physicalBounds.left), static_cast<LONG>(physicalBounds.top),
			        static_cast<LONG>(physicalBounds.right), static_cast<LONG>(physicalBounds.bottom)};
			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, m_hasUpdate ? RGB(0x00, 0x78, 0xd4) : RGB(0x80, 0x80, 0x80));
			HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
			HFONT hOld = static_cast<HFONT>(SelectObject(hdc, hFont));
			DrawTextW(hdc, L"↓", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			SelectObject(hdc, hOld);
			ReleaseDC(nativeHandle(), hdc);
		}
	}

	void MainWindow::drawToHelpBtn(const RenderContext& renderCtx, Button::EState state) const
	{
		// draw() 已将坐标系平移到按钮原点，使用本地坐标 (0,0)-(width,height)
		const float width = m_btnHelp.getBounds().right - m_btnHelp.getBounds().left;
		const float height = m_btnHelp.getBounds().bottom - m_btnHelp.getBounds().top;
		const bool hot = (state == Button::EState::Hover || state == Button::EState::Active);

		if (isCompositionEnabled())
		{
			const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
			const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
			// 悬浮/按下时浅蓝高亮（与更新按钮一致）
			if (hot)
			{
				solidBrush->SetColor(D2D1::ColorF(0.00784f, 0.4706f, 0.8314f, 0.12f));
				renderTarget->FillRoundedRectangle(
					D2D1::RoundedRect(D2D1::RectF(0.f, 0.f, width, height), 4.f, 4.f),
					solidBrush);
			}
			// 只画一个"?"：常态灰色、悬浮/按下主题蓝，无背景、无描边
			solidBrush->SetColor(hot
			                     ? D2D1::ColorF(0.00784f, 0.4706f, 0.8314f, 1.f)
			                     : D2D1::ColorF(0.45f, 0.45f, 0.45f, 1.f));
			IDWriteTextFormat* const tipsFmt = app().textFormat().pTipsFormat.get();
			const auto oldAlign = tipsFmt->GetTextAlignment();
			tipsFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			renderTarget->DrawTextW(L"?", 1,
			                        tipsFmt,
			                        D2D1::RectF(0.f, (height - 14.f) * 0.5f, width, (height - 14.f) * 0.5f + 14.f),
			                        solidBrush);
			tipsFmt->SetTextAlignment(oldAlign);
		}
		else
		{
			const D2D1_RECT_F& bounds = m_btnHelp.getBounds();
			HDC hdc = GetWindowDC(nativeHandle());
			const float deviceToPhysical = dpiInfo().deviceToPhysical;
			D2D1_RECT_F physicalBounds = D2D1::RectF((bounds.left + m_margins.left) * deviceToPhysical,
			                                         (bounds.top + m_margins.top) * deviceToPhysical,
			                                         (bounds.right + m_margins.left) * deviceToPhysical,
			                                         (bounds.bottom + m_margins.top) * deviceToPhysical);
			RECT rc{static_cast<LONG>(physicalBounds.left), static_cast<LONG>(physicalBounds.top),
			        static_cast<LONG>(physicalBounds.right), static_cast<LONG>(physicalBounds.bottom)};
			SetBkMode(hdc, TRANSPARENT);
			SetTextColor(hdc, hot ? RGB(0x00, 0x78, 0xd4) : RGB(0x80, 0x80, 0x80));
			HFONT hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
			HFONT hOld = static_cast<HFONT>(SelectObject(hdc, hFont));
			DrawTextW(hdc, L"?", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
		// 已进入退出清理流程：阻断重复关闭（避免再次弹确认框/重复杀进程）
		if (m_bExitStarted)
		{
			return true;
		}
		const bool hasProc = isPage<HomePage>() && getPage<HomePage>().getLeftSidebar()->getEnvBoxCardArea()->hasAnyProcesses();

		TASKDIALOGCONFIG cfg{};
		cfg.cbSize = sizeof(cfg);
		cfg.hwndParent = nativeHandle();
		cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
		cfg.pszWindowTitle = MainApp::appName.data();
		cfg.pszMainInstruction = L"关闭 eBox？";
		cfg.pszContent = hasProc ? L"仍有进程正在运行。\n\n缩小到托盘：eBox 继续在后台运行，进程不受影响。\n退出应用：将强制结束所有环境中的进程，并退出 eBox。"
		                         : L"缩小到托盘：eBox 继续在后台运行。\n退出应用：结束所有环境中的进程并退出 eBox。";
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
			// 退出应用：先结束所有环境中的进程，再销毁窗口退出。
			// 进程清理放到后台线程：本函数（以及它所在的进程树遍历）涉及多轮系统进程快照、
			// 逐个进程查 exe、终止进程并反复等待，若是同步执行会把 UI 线程饿死导致主窗口"未响应"。
			// 清理完成后再由 WM_APP_EXIT_AFTER_KILL 通知 UI 线程销毁窗口并退出。
			startExitCleanup();
			// 阻止本次关闭：先保持窗口存活，待后台清理完成后统一销毁窗口退出。
			return true;
		}
		// 取消或直接关闭对话框
		return true;
	}

	void MainWindow::startExitCleanup()
	{
		if (m_bExitStarted)
		{
			// 已进入退出清理流程：忽略重复触发（防止重复点击关闭/重复杀进程/重复弹窗）
			return;
		}
		m_bExitStarted = true;
		const HWND hWnd = nativeHandle();
		// 进程清理放到后台线程，避免阻塞 UI 线程导致主窗口"未响应"。
		// 清理完成后投递消息回 UI 线程，由 UI 线程统一销毁窗口并退出（保证进程终止先于应用退出）。
		std::thread(
			[this, hWnd]
			{
				// killAllEnvProcesses 为 const 方法、且只用局部变量与 RPC/env 锁，不触碰本对象可变成员，
				// 后台线程调用是安全的；PostMessageW 使用按值捕获的 hWnd，不依赖 this。
				killAllEnvProcesses();
				PostMessageW(hWnd, WM_APP_EXIT_AFTER_KILL, 0, 0);
			})
			.detach();
	}

	void MainWindow::killAllEnvProcesses() const
	{
		const DWORD selfPid = GetCurrentProcessId();

		// 多轮迭代终止：父进程被终止后子进程可能重新归属（reparent）甚至继续
		// 派生新进程，单轮快照无法保证零残留。每轮重新快照、收集进程树，并按
		// 已上报进程的 exe 文件名兜底匹配（解决 reparent 后树收集失效），
		// 直到连续一轮没有任何进程被杀才结束。
		for (int round = 0; round < 4; ++round)
		{
			// 1. 环境中通过 RPC 上报过的进程
			std::vector<DWORD> pids = biz::env_mgr().getAllProcessIdsExclude(0);

			// 2. 记录这些进程的 exe 文件名（完整路径取文件名，规避 32/64 位路径重定向差异）
			std::set<std::wstring> envExeNames;
			for (const DWORD pid : pids)
			{
				if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
				{
					wchar_t buf[MAX_PATH]{};
					DWORD size = MAX_PATH;
					if (QueryFullProcessImageNameW(h, 0, buf, &size) && size > 0)
					{
						envExeNames.emplace(std::filesystem::path{buf}.filename().native());
					}
					CloseHandle(h);
				}
			}

			// 3. 快照进程表：父子关系 + 每个进程的 exe 文件名
			std::unordered_map<DWORD, std::vector<DWORD>> parentToChildren;
			std::unordered_map<DWORD, std::wstring> pidToExeName;
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
							pidToExeName.emplace(pe.th32ProcessID, pe.szExeFile);
						}
						while (Process32NextW(snapshot, &pe));
					}
					CloseHandle(snapshot);
				}
			}

			// 4. 收集待杀列表：上报进程 + 进程树后代 + 同名 exe 兜底
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
			for (const auto& [pid, exeName] : pidToExeName)
			{
				if (envExeNames.contains(exeName))
				{
					allToKill.push_back(pid);
				}
			}
			// 去重
			std::sort(allToKill.begin(), allToKill.end());
			allToKill.erase(std::unique(allToKill.begin(), allToKill.end()), allToKill.end());

			bool anyKilled = false;
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
					anyKilled = true;
				}
			}
			if (!anyKilled)
			{
				break;
			}
			// 给被杀进程一点退出时间，并让新派生的子进程有时间出现，下一轮再收
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
		// 更新按钮（授权按钮左侧）
		const float updateBtnWidthNc = toTrayBthWidth;
		m_btnUpdate.setBounds(D2D1::Rect(toTrayBthXPos - licenseBtnWidth - updateBtnWidthNc, 6 - m_margins.top, toTrayBthXPos - licenseBtnWidth, -2.f));
		m_btnUpdate.setDontDrawDefault(true);
		m_btnUpdate.drawImpl(renderContext());
		// 帮助按钮（更新按钮左侧）
		const float helpBtnWidthNc = toTrayBthWidth;
		m_btnHelp.setBounds(D2D1::Rect(toTrayBthXPos - licenseBtnWidth - updateBtnWidthNc - helpBtnWidthNc, 6 - m_margins.top, toTrayBthXPos - licenseBtnWidth - updateBtnWidthNc, -2.f));
		m_btnHelp.setDontDrawDefault(true);
		m_btnHelp.drawImpl(renderContext());
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
					// 启动新进程：有"上次使用应用"记忆 → 直接启动；否则弹出应用选择列表
					const std::wstring last = ui::get_last_app_path();
					std::optional<std::wstring> path;
					if (!last.empty() && std::filesystem::exists(std::filesystem::path{last}))
					{
						path = last;
					}
					else
					{
						path = ui::select_app_dialog(this);
					}
					if (path.has_value())
					{
						getPage<HomePage>().getLeftSidebar()->getEnvBoxCardArea()->launchProcessInNewEnv(*path);
					}
				}
				else if (id == 3)
		{
			// 退出：结束所有环境中的进程并退出。
			// 进程清理放到后台线程，避免阻塞 UI 线程导致"未响应"；清理完成后
			// 由 WM_APP_EXIT_AFTER_KILL 通知 UI 线程销毁窗口并退出。
			startExitCleanup();
		}
		}
		}
		else if (message == WM_APP_EXIT_AFTER_KILL)
		{
			// 后台线程已完成进程清理：由 UI 线程销毁窗口并退出（触发 onBeforeWindowDestroy 清理托盘等）
			destroyWindow();
		}
		else if (message == WM_UPDATE_CHECK_DONE)
		{
			// IO 线程检查完毕：取出堆上结果，所有权转移到 UI 线程
			std::unique_ptr<biz::update::CheckOutcome> pOutcome(reinterpret_cast<biz::update::CheckOutcome*>(wParam));
			onCheckUpdateDone(std::move(*pOutcome));
		}
		else if (message == WM_APP_LICENSENOTICE)
		{
			// 心跳线程收到服务端系统公告：刷新主界面公告栏（显示最新一条）
			if (isPage<HomePage>())
			{
				getPage<HomePage>().getRightContent()->refreshNotice();
			}
		}
		else if (message == WM_APP_LICENSEINVALID)
		{
			// 心跳线程发现授权被服务端锁定：在 UI 线程弹窗（工作线程不直接弹窗抢焦点）
			std::unique_ptr<std::wstring> pMsg(reinterpret_cast<std::wstring*>(lParam));
			if (pMsg && !pMsg->empty())
			{
				MessageBoxW(nativeHandle(), pMsg->c_str(), L"eBox 授权失效",
				            MB_OK | MB_ICONWARNING | MB_TASKMODAL);
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
		// 授权到期红点：距到期 <=7 天时"授权"按钮亮红点（点击进入授权信息查看详情）
		m_licenseRemindDays = biz::license::remainingDays();
		// 标题：eBox v3.0.2   更新时间：2026/8/28   [到期：yyyy-MM-dd]
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

		// 把窗口居中到所在显示器的工作区（排除任务栏），而不是沿用创建时的默认位置。
		// 旧逻辑基于"当前窗口 rect"收缩尺寸并保留其 left/top，导致首窗永远偏左上而非屏幕居中。
		MONITORINFO mi{sizeof(mi)};
		const HMONITOR mon = MonitorFromWindow(nativeHandle(), MONITOR_DEFAULTTONEAREST);
		GetMonitorInfoW(mon, &mi);
		const float workX = static_cast<float>(mi.rcWork.left);
		const float workY = static_cast<float>(mi.rcWork.top);
		const float workW = static_cast<float>(mi.rcWork.right - mi.rcWork.left);
		const float workH = static_cast<float>(mi.rcWork.bottom - mi.rcWork.top);

		const float desiredX = workX + (workW - desiredWidth) * 0.5f;
		const float desiredY = workY + (workH - desiredHeight) * 0.5f;

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
		return m_btnToTray.hitTest(local) || m_btnLicense.hitTest(local) || m_btnUpdate.hitTest(local) || m_btnHelp.hitTest(local);
	}

	// ===== 自动升级实现 =====

	void MainWindow::startUpdateCheck()
	{
		m_updateScope.spawn(checkUpdateTask());
	}

	coro::LazyTask<void> MainWindow::checkUpdateTask()
	{
		// 挂接取消令牌：窗口析构 request_stop 时中断进行中的网络拉取，避免 join 阻塞
		co_await coro::set_cancellation_token(m_updateTimerStop.get_token());
		biz::update::CheckOutcome outcome = co_await biz::update::checkUpdateAsync();
		// 协程在 IO 线程完成：PostMessage 回 UI 线程（不依赖 scheduler，避免析构死锁）
		auto* pOutcome = new biz::update::CheckOutcome(std::move(outcome));
		if (!PostMessageW(nativeHandle(), WM_UPDATE_CHECK_DONE, reinterpret_cast<WPARAM>(pOutcome), 0))
		{
			delete pOutcome;  // 窗口已销毁：释放堆上结果
		}
		co_return;
	}

	void MainWindow::onCheckUpdateDone(biz::update::CheckOutcome outcome)
	{
		// 记录最近一次检查结果：NoUpdate/NetworkError/SkippedThisVersion 时点击更新按钮区分提示
		m_lastCheckResult = outcome.result;
		if (outcome.result == biz::update::CheckResult::HasUpdate && outcome.manifest.latestVersionCode > 0)
		{
			m_pendingUpdate = std::move(outcome.manifest);
			m_hasUpdate = true;
			RedrawWindow(nativeHandle(), nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_NOINTERNALPAINT | RDW_ERASENOW);
		}
		else
		{
			// 无更新/失败/已忽略：保持红点熄灭
			m_hasUpdate = false;
		}

		// 手动点击"更新"触发的实时检查：完成后自动展示结果
		if (m_manualCheckPending)
		{
			m_manualCheckPending = false;
			if (m_pendingUpdate)
			{
				showUpdateDialog();  // 有更新：弹出更新详情
			}
			else
			{
				// 无更新/失败/已忽略：按最近一次检查结果区分提示，避免网络失败误报"已是最新版本"
				switch (m_lastCheckResult)
				{
				case biz::update::CheckResult::NetworkError:
					MessageBoxW(nativeHandle(), L"检查更新失败，请检查网络连接后重试。",
					            MainApp::appName.data(), MB_OK | MB_ICONWARNING);
					break;
				case biz::update::CheckResult::SkippedThisVersion:
					MessageBoxW(nativeHandle(), L"您已选择忽略此版本的更新，有新版本时会再次提示。",
					            MainApp::appName.data(), MB_OK | MB_ICONINFORMATION);
					break;
				default:
					MessageBoxW(nativeHandle(), L"当前已是最新版本。",
					            MainApp::appName.data(), MB_OK | MB_ICONINFORMATION);
					break;
				}
			}
		}
	}

	void MainWindow::showUpdateDialog()
	{
		if (!m_pendingUpdate)
		{
			// 无待装更新：先实时拉取最新 manifest（时间戳破除 CDN 缓存），检查完成后自动展示结果
			if (m_manualCheckPending)
			{
				return;  // 已有实时检查在进行，忽略重复点击
			}
			m_manualCheckPending = true;
			startUpdateCheck();
			return;
		}
		const auto& manifest = *m_pendingUpdate;

		// 组装 changelog 文本
		std::wstring content;
		for (const auto& line : manifest.changelog)
		{
			content += line;
			content += L"\r\n";
		}
		// 大小显示
		wchar_t sizeBuf[64] = {};
		if (manifest.downloadSize > 0)
		{
			const double mb = static_cast<double>(manifest.downloadSize) / (1024.0 * 1024.0);
			swprintf_s(sizeBuf, L"  ·  约 %.1f MB", mb);
		}
		const std::wstring mainInstr = L"eBox " + manifest.latestVersion + L" 已发布";
		const std::wstring windowTitle = std::wstring(MainApp::appName) + L" 更新";
		const std::wstring contentFull = L"发布日期：" + manifest.releaseDate + sizeBuf +
		                                 L"\r\n\r\n更新内容：\r\n" + content;

		// 是否允许"以后再说"：forceUpdate 或本地版本 <= minSkipVersionCode 时必须更新
		const bool canSkip = !manifest.forceUpdate &&
		                     (manifest.minSkipVersionCode <= 0 ||
		                      MainApp::kVerCode > manifest.minSkipVersionCode);

		TASKDIALOGCONFIG config{};
		config.cbSize = sizeof(config);
		config.hwndParent = nativeHandle();
		config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_COMMAND_LINKS;
		config.pszWindowTitle = windowTitle.c_str();
		config.pszMainIcon = TD_INFORMATION_ICON;
		config.pszMainInstruction = mainInstr.c_str();
		config.pszContent = contentFull.c_str();

		if (canSkip)
		{
			TASKDIALOG_BUTTON buttons[2];
			buttons[0].nButtonID = IDOK;
			buttons[0].pszButtonText = L"立即更新";
			buttons[1].nButtonID = IDCANCEL;
			buttons[1].pszButtonText = L"以后再说";
			config.cButtons = 2;
			config.pButtons = buttons;
		}
		else
		{
			// 强制更新：只提供"立即更新"，不允许忽略
			TASKDIALOG_BUTTON buttons[1];
			buttons[0].nButtonID = IDOK;
			buttons[0].pszButtonText = L"立即更新";
			config.cButtons = 1;
			config.pButtons = buttons;
		}
		config.nDefaultButton = IDOK;

		int nButton = -1;
		if (FAILED(TaskDialogIndirect(&config, &nButton, nullptr, nullptr)))
		{
			// TaskDialog 不可用：回退到 MessageBox
			const int ret = MessageBoxW(nativeHandle(), (mainInstr + L"\r\n\r\n" + contentFull).c_str(),
			                           windowTitle.c_str(),
			                           canSkip ? (MB_YESNO | MB_ICONINFORMATION) : (MB_OK | MB_ICONINFORMATION));
			nButton = (ret == IDYES || ret == IDOK) ? IDOK : IDCANCEL;
		}

		if (nButton == IDOK)
		{
			startDownloadAndApply(manifest);
		}
		else if (canSkip)
		{
			// 以后再说：记录忽略此版本，熄灭红点
			biz::update::ignoreVersion(manifest.latestVersionCode);
			m_hasUpdate = false;
			RedrawWindow(nativeHandle(), nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_NOINTERNALPAINT | RDW_ERASENOW);
		}
		// 强制更新且未点"立即更新"（如按 Esc 关闭）：保持红点，下次点击仍提示
	}

	HRESULT CALLBACK MainWindow::downloadDlgCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LONG_PTR lpRefData)
	{
		auto* self = reinterpret_cast<MainWindow*>(lpRefData);
		switch (msg)
		{
		case TDN_CREATED:
			SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(0, 100));
			SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_POS, 0, 0);
			SendMessageW(hwnd, TDM_SET_MARQUEE_PROGRESS_BAR, FALSE, 0);
			break;
		case TDN_TIMER:
		{
			const std::uint64_t dl = self->m_dlDownloaded.load();
			const std::uint64_t total = self->m_dlTotal.load();
			if (total > 0)
			{
				int pct = static_cast<int>(dl * 100 / total);
				if (pct > 100) pct = 100;
				SendMessageW(hwnd, TDM_SET_PROGRESS_BAR_POS, pct, 0);
				wchar_t buf[128] = {};
				const double mbDl = static_cast<double>(dl) / (1024.0 * 1024.0);
				const double mbTotal = static_cast<double>(total) / (1024.0 * 1024.0);
				swprintf_s(buf, L"已下载 %.1f / %.1f MB  ·  %d%%", mbDl, mbTotal, pct);
				SendMessageW(hwnd, TDM_SET_ELEMENT_TEXT, TDE_CONTENT, reinterpret_cast<LPARAM>(buf));
			}
			const int state = self->m_dlState.load(std::memory_order_acquire);
			if (state != 0)
			{
				// 下载完成/失败/取消：自动关闭弹窗（此弹窗仅有的按钮是 IDCANCEL）
				SendMessageW(hwnd, TDM_CLICK_BUTTON, IDCANCEL, 0);
			}
			break;
		}
		case TDN_BUTTON_CLICKED:
			if (wParam == IDCANCEL)
			{
				// 仅当下载仍在进行中视为用户取消；完成/失败后的自动关闭（TDM_CLICK_BUTTON）
				// 也会触发本回调，此时不能覆盖成功/失败状态
				if (self->m_dlState.load(std::memory_order_acquire) == 0)
				{
					self->m_dlStopSource.request_stop();
					self->m_dlState.store(3);
				}
			}
			break;
		default:
			break;
		}
		return S_OK;
	}

	void MainWindow::startDownloadAndApply(biz::update::UpdateManifest manifest)
	{
		// 重置下载状态
		m_dlDownloaded.store(0);
		m_dlTotal.store(manifest.downloadSize);
		m_dlState.store(0);
		m_dlOutcome.reset();

		// 启动后台下载协程（IO 线程）
		m_updateScope.spawn(([this, manifest]() -> coro::LazyTask<void>
		{
			// 挂接下载取消令牌：用户点取消/窗口析构时 request_stop，中断网络传输
			co_await coro::set_cancellation_token(m_dlStopSource.get_token());
			biz::update::DownloadOutcome outcome = co_await biz::update::downloadAndVerifyAsync(
				manifest,
				[this](const biz::update::DownloadProgress& p)
				{
					m_dlDownloaded.store(p.downloaded);
					if (p.total > 0) m_dlTotal.store(p.total);
				});
			// 用户取消/传输被中断：清理可能残留的临时文件
			if (m_dlState.load(std::memory_order_acquire) == 3 ||
			    outcome.result == biz::update::DownloadResult::Cancelled)
			{
				DeleteFileW(biz::update::getTempDownloadPath(manifest.latestVersionCode).c_str());
			}
			m_dlOutcome = std::move(outcome);
			// 用户取消状态优先：不被协程后续完成覆盖
			if (m_dlState.load(std::memory_order_acquire) != 3)
			{
				m_dlState.store(m_dlOutcome->result == biz::update::DownloadResult::Success ? 1 : 2,
				                std::memory_order_release);
			}
			co_return;
		})());

		// 显示进度弹窗（模态，由 callback 轮询 atomic 更新）
		TASKDIALOGCONFIG config{};
		config.cbSize = sizeof(config);
		config.hwndParent = nativeHandle();
		config.dwFlags = TDF_SHOW_PROGRESS_BAR | TDF_CALLBACK_TIMER | TDF_ALLOW_DIALOG_CANCELLATION;
		config.pszWindowTitle = L"正在下载更新";
		config.pszMainInstruction = L"正在下载 eBox 更新包...";
		config.pszContent = L"请耐心等待，下载完成后将自动安装。";
		config.pfCallback = &MainWindow::downloadDlgCallback;
		config.lpCallbackData = reinterpret_cast<LONG_PTR>(this);
		TASKDIALOG_BUTTON cancelBtn{};
		cancelBtn.nButtonID = IDCANCEL;
		cancelBtn.pszButtonText = L"取消";
		config.cButtons = 1;
		config.pButtons = &cancelBtn;
		config.nDefaultButton = IDCANCEL;

		int nButton = -1;
		TaskDialogIndirect(&config, &nButton, nullptr, nullptr);

		// 弹窗关闭：根据状态处理
		const int state = m_dlState.load();
		if (state == 1 && m_dlOutcome && m_dlOutcome->result == biz::update::DownloadResult::Success)
		{
			// 二次确认安装
			if (MessageBoxW(nativeHandle(), L"更新下载完成，点击确定开始安装（程序将退出并自动重启）。",
			                MainApp::appName.data(), MB_OKCANCEL | MB_ICONINFORMATION) == IDOK)
			{
				if (biz::update::applyUpdate(m_dlOutcome->filePath))
				{
					destroyWindow();  // 退出当前进程，bat 接管覆盖 + 重启
				}
				else
				{
					MessageBoxW(nativeHandle(), L"启动更新失败，请稍后重试。", MainApp::appName.data(), MB_OK | MB_ICONERROR);
				}
			}
		}
		else if (state == 3)
		{
			// 用户取消：不处理
		}
		else
		{
			// 失败
			std::wstring msg = L"下载更新失败。";
			if (m_dlOutcome && !m_dlOutcome->errorMessage.empty())
			{
				msg += L"\r\n";
				msg += m_dlOutcome->errorMessage;
			}
			MessageBoxW(nativeHandle(), msg.c_str(), MainApp::appName.data(), MB_OK | MB_ICONERROR);
		}
	}
}
