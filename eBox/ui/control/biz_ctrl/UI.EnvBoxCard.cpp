module UI.EnvBoxCard;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import MainApp;
import Scheduler;
import Biz.Core;
import UI.Core;
import std;

namespace
{
	constexpr float PADDING = 16.f;
	constexpr float LINE1_GAP = 8.f;
	constexpr float TITLE_HEIGHT = 24.f;
	constexpr float COUNT_HEIGHT = 21.f;
	constexpr float BTN_WIDTH = 36.f;
	constexpr float BTN_HEIGHT = 20.f;
	constexpr float BTN_GAP = 4.f;

	std::wstring format_runtime(std::chrono::steady_clock::duration elapsed)
	{
		const auto totalSec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
		if (totalSec < 60)
		{
			return std::format(L"已运行 {} 秒", totalSec);
		}
		const auto totalMin = totalSec / 60;
		if (totalMin < 60)
		{
			return std::format(L"已运行 {} 分钟", totalMin);
		}
		const auto totalHour = totalMin / 60;
		if (totalHour < 24)
		{
			return std::format(L"已运行 {} 小时 {} 分钟", totalHour, totalMin % 60);
		}
		const auto totalDay = totalHour / 24;
		return std::format(L"已运行 {} 天 {} 小时", totalDay, totalHour % 24);
	}
}

namespace ui
{
	EnvBoxCard::~EnvBoxCard()
	{
		// 1. 先解除通知回调。注意：addProcessInternal/removeProcessInternal 已在锁外
		//    执行回调（避免在 env 共享锁上跑回调阻塞读者），故此处置空后仍可能有
		//    一个已捕获旧回调副本的 in-flight 通知在稍后 spawn 协程。该协程由
		//    m_lifeStopSource 包裹，下面 request_stop 会同步取消它（首个挂起点
		//    transfer_to(UI调度器) 被停时立即收尾）并触发 onWorkFinished，因此
		//    join() 不会被队列中残留的 UI 任务拖死。
		m_env->setProcCountChangeNotify(nullptr);
		// 2. 再请求取消：此前已 spawn 的协程（挂在 transfer_to/transfer_after 上）
		//    由 stop_callback 在当前线程同步恢复并抛取消异常，干净结束协程，
		//    从而保证下面 join() 不会无限阻塞（否则 eBox.exe 退出时残留进程）。
		m_stopSource.request_stop();
		m_lifeStopSource.request_stop();
		// 之后就绝对不会spawn新的协程，才可以安全等待所有协程结束
		m_asyncScope.join();
	}

	void EnvBoxCard::setEnv(const std::shared_ptr<biz::Env>& env)
	{
		m_env = env;
		m_name = m_env->getName();
		m_strProcCount = std::format(L"{}", m_env->getAllProcessesCount());

		m_env->setProcCountChangeNotify([this](biz::Env::EProcEvent e, const std::shared_ptr<biz::ProcessInfo>& proc, std::size_t count)
		{
			// 用 m_lifeStopSource 包裹：退出销毁时若该协程还挂在 transfer_to(UI调度器)
			// 队列里（killAllEnvProcesses 强杀进程后、UI 线程忙于析构），request_stop
			// 会同步取消它，避免 AsyncScope::join() 无限等待导致 eBox.exe 残留。
			m_asyncScope.spawn(coro::co_with_cancellation(onProcessCountChange(e, proc, count), m_lifeStopSource.get_token()));
		});
		// 新建环境（启动新进程创建）才有“首次初始化”提示；老环境无 pending 标记不显示
		m_bFirstLaunchPending = m_env->isFirstLaunchPending();
		if (m_bFirstLaunchPending)
		{
			m_asyncScope.spawn(coro::co_with_cancellation(tickFirstLaunch(), m_stopSource.get_token()));
		}
		updateNameLayout();
	}

	void EnvBoxCard::updateNameLayout()
	{
		m_pNameLayout.reset();
		m_bNameOverflow = false;
		if (m_name.empty() || m_nameAreaWidth <= 0.f)
		{
			return;
		}
		// 用实际文本布局测量宽度，判断名称是否超出可用区域（溢出时显示省略号并支持悬浮提示）
		if (SUCCEEDED(app().dWriteFactory()->CreateTextLayout(m_name.c_str(),
		                                                      static_cast<UINT32>(m_name.size()),
		                                                      app().textFormat().pTitleFormat,
		                                                      std::numeric_limits<float>::max(),
		                                                      std::numeric_limits<float>::max(),
		                                                      &m_pNameLayout)))
		{
			DWRITE_TEXT_METRICS textMetrics{};
			if (SUCCEEDED(m_pNameLayout->GetMetrics(&textMetrics)))
			{
				m_bNameOverflow = textMetrics.width > m_nameAreaWidth;
			}
		}
	}

	void EnvBoxCard::setBusyTemp()
	{
		// 由于无法保证启动的进程一定能立即反映在env中，所以简单设置一个是否空闲的标志，env在启动任意一个进程后的1.6s内属于非空闲状态
		// 非空闲的env无法被 “点击总启动按钮时” 挑选到
		m_stopSource.request_stop();
		m_bIdle = false;
		m_stopSource = std::stop_source{};
		m_asyncScope.spawn(coro::co_with_cancellation(resetToIdleLater(), m_stopSource.get_token()));
		if (m_env)
		{
			biz::env_logger().append(m_env->getIndex(), biz::EnvLogType::Message, biz::EnvLogStatus::Info,
			                         L"闪烁提示", L"环境被选中启动，进入非空闲状态（1.6s）");
		}
	}

	void EnvBoxCard::programmaticDeselect()
	{
		m_isSelected = false;
	}

	void EnvBoxCard::initialize()
	{
		// m_btnStart = std::make_unique<Button>(this);
		// m_btnStart->setText(L"添加进程");
		// m_btnStart->setBackgroundColor(D2D1::ColorF(0xe3f2fd));
		// m_btnStart->setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		// m_btnStart->setBorderColor(D2D1::ColorF(0x0078d4));
		// m_btnStart->setTextColor(D2D1::ColorF(0x0078d4));
		// m_btnStart->setOnClick([this] { onBtnStartPressed(); });

		m_btnStart = std::make_unique<Button>(this);
		m_btnStart->setText(L"启动");
		m_btnStart->setBackgroundColor(D2D1::ColorF(0x0078d4), Button::EState::Normal);
		m_btnStart->setBackgroundColor(D2D1::ColorF(0x006cbd), Button::EState::Hover);
		m_btnStart->setBackgroundColor(D2D1::ColorF(0x005a9e), Button::EState::Active);
		m_btnStart->setTextColor(D2D1::ColorF(D2D1::ColorF::White));
		m_btnStart->setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnStart->setOnClick([this]
		{
			if (!m_env)
			{
				return;
			}
			// 记录最后操作，供 UI 卡死 watchdog 转储时提示定位
			sched::hang_watchdog_mark(L"环境卡片-启动");
			// 点击启动前先选中该环境，右侧进程区联动显示该环境
			m_isSelected = true;
			if (m_pfnOnSelect)
			{
				m_pfnOnSelect(true);
			}
			// 该环境已有应用在运行且有窗口（多环境多实例时任务栏图标无法区分）：
			// 直接将该环境的窗口调到前台，不重复启动；没有窗口才走正常启动流程
			if (activateEnvWindows())
			{
				return;
			}
			// 启动该环境绑定的应用（首次启动时自动绑定）；未绑定则先让用户选择
			std::wstring appPath(m_env->getAppPath());
			if (appPath.empty())
			{
				const std::optional<std::wstring> fullPath = select_file(m_ownerWnd);
				if (!fullPath.has_value())
				{
					return;
				}
				appPath = fullPath.value();
				biz::env_mgr().setEnvAppPath(m_env, appPath);
			}
			biz::launcher().run(m_env, appPath);
		});

		m_btnClose = std::make_unique<Button>(this);
		m_btnClose->setText(L"关闭");
		m_btnClose->setBackgroundColor(D2D1::ColorF(0xffebee));
		m_btnClose->setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnClose->setBorderColor(D2D1::ColorF(0xf44336));
		m_btnClose->setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnClose->setTextColor(D2D1::ColorF(0x333333));
		m_btnClose->setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnClose->setOnClick([this]
		{
			if (!m_env)
			{
				return;
			}
			// 弹出两个选项：结束进程 / 删除环境
			HMENU hMenu = CreatePopupMenu();
			AppendMenuW(hMenu, MF_STRING, 1, L"结束进程");
			AppendMenuW(hMenu, MF_STRING, 2, L"删除环境");
			POINT pt{};
			GetCursorPos(&pt);
			SetForegroundWindow(m_ownerWnd->nativeHandle());
			const UINT id = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RETURNCMD,
			                                 pt.x, pt.y, m_ownerWnd->nativeHandle(), nullptr);
			DestroyMenu(hMenu);

			if (id == 1)
			{
				// 结束当前环境运行的所有进程（环境保留，可再次启动复用）
				for (const DWORD pid : m_env->getAllProcessIds())
				{
					if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid))
					{
						TerminateProcess(h, 1);
						CloseHandle(h);
					}
				}
			}
			else if (id == 2)
			{
				if (m_procCount)
				{
					MessageBoxW(m_ownerWnd->nativeHandle(), L"环境中仍有正在运行的程序时无法删除", MainApp::appName.data(), MB_OK);
				}
				else if (MessageBoxW(m_ownerWnd->nativeHandle(),
				                     std::format(L"删除环境将会把数据和登录记录等一并删除，之后将无法再通过该环境登录，请谨慎操作。\n\n是否确认删除【{}】环境？", m_env->getName()).c_str(),
				                     MainApp::appName.data(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
				{
					// 后台删除：cmd rd 大目录可达数十秒，同步等待会让 UI 线程冻结（未响应）。
					// UI 刷新由 removeEnv 内的 envChangeNotify 回调驱动，与同步版本行为一致；
					// env 以 shared_ptr 保活，卡片先销毁也不影响删除流程继续。
					std::thread([env = m_env]()
					{
						try
						{
							biz::env_mgr().deleteEnv(env);
						}
						catch (...)
						{
							// 防御：并发重复删除等场景 removeEnv 会抛异常，吞掉避免终止进程
						}
					}).detach();
				}
			}
		});

		m_btnRename = std::make_unique<Button>(this);
		m_btnRename->setText(L"改名");
		m_btnRename->setBackgroundColor(D2D1::ColorF(0xe8f5e9));
		m_btnRename->setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnRename->setBorderColor(D2D1::ColorF(0x4caf50));
		m_btnRename->setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnRename->setTextColor(D2D1::ColorF(0x333333));
		m_btnRename->setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnRename->setOnClick([this]
		{
			if (!m_env)
			{
				return;
			}
			if (std::optional<std::wstring> newName = ui::input_text(m_ownerWnd, L"重命名环境", m_env->getName()))
			{
				if (biz::env_mgr().renameEnv(m_env, newName.value()))
				{
					m_name = m_env->getName();
					updateNameLayout();
					updateWholeWnd();
				}
			}
		});

		if (!m_bIdle)
		{
			m_stopSource = std::stop_source{};
			m_asyncScope.spawn(coro::co_with_cancellation(resetToIdleLater(), m_stopSource.get_token()));
		}

		// 列表视图的图标字体（Segoe UI Symbol 支持 ▶ ✎ ×）
		HRESULT hr = app().dWriteFactory()->CreateTextFormat(
			L"Segoe UI Symbol",
			nullptr,
			DWRITE_FONT_WEIGHT_NORMAL,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			12.f,
			L"",
			&m_pListIconFormat);
		if (FAILED(hr))
		{
			m_pListIconFormat.reset();
		}
	}

	void EnvBoxCard::setViewMode(EViewMode mode)
	{
		if (m_viewMode == mode)
		{
			return;
		}
		m_viewMode = mode;
		const bool bList = (mode == EViewMode::List);
		const auto& commonFormat = app().textFormat().pToolBtnFormat;
		// 按钮文字/字体随视图模式切换：卡片=文字按钮，列表=图标按钮
		m_btnStart->setText(bList ? L"▶" : L"启动");
		m_btnRename->setText(bList ? L"✎" : L"改名");
		m_btnClose->setText(bList ? L"×" : L"关闭");
		m_btnStart->setTextFormat(bList ? m_pListIconFormat : commonFormat);
		m_btnRename->setTextFormat(bList ? m_pListIconFormat : commonFormat);
		m_btnClose->setTextFormat(bList ? m_pListIconFormat : commonFormat);
		// 颜色始终保持与卡片视图一致（蓝启动/绿改名/红关闭），列表模式同样保留彩色底
		m_btnStart->setBackgroundColor(D2D1::ColorF(0x0078d4), Button::EState::All);
		m_btnRename->setBackgroundColor(D2D1::ColorF(0xe8f5e9), Button::EState::All);
		m_btnRename->setBorderColor(D2D1::ColorF(0x4caf50), Button::EState::All);
		m_btnClose->setBackgroundColor(D2D1::ColorF(0xffebee), Button::EState::All);
		m_btnClose->setBorderColor(D2D1::ColorF(0xf44336), Button::EState::All);
		if (bList)
		{
			// 列表模式：全部实底彩色，图标更清晰可见
			m_btnStart->setBackgroundColor(D2D1::ColorF(0x0078d4), Button::EState::All);
		}
		else
		{
			// 卡片模式：恢复各按钮原本的悬停/按下颜色与透明 Normal 底
			m_btnStart->setBackgroundColor(D2D1::ColorF(0x0078d4), Button::EState::Normal);
			m_btnStart->setBackgroundColor(D2D1::ColorF(0x006cbd), Button::EState::Hover);
			m_btnStart->setBackgroundColor(D2D1::ColorF(0x005a9e), Button::EState::Active);
			m_btnRename->setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
			m_btnRename->setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
			m_btnClose->setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
			m_btnClose->setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		}
		const auto [width, height] = size();
		onResize(width, height);
		update();
	}

	void EnvBoxCard::onResize(float width, float height)
	{
		if (m_viewMode == EViewMode::List)
		{
			// 列表模式：右侧三个小图标按钮横排（▶ ✎ ×）
			constexpr float listBtnWidth = 24.f;
			constexpr float listBtnHeight = 20.f;
			constexpr float listBtnGap = 2.f;
			const float btnY = (height - listBtnHeight) * 0.5f;
			float right = width - PADDING;
			m_btnClose->setBounds(D2D1::RectF(right - listBtnWidth, btnY, right, btnY + listBtnHeight));
			right -= listBtnWidth + listBtnGap;
			m_btnRename->setBounds(D2D1::RectF(right - listBtnWidth, btnY, right, btnY + listBtnHeight));
			right -= listBtnWidth + listBtnGap;
			m_btnStart->setBounds(D2D1::RectF(right - listBtnWidth, btnY, right, btnY + listBtnHeight));
			return;
		}

		const float btnXPos = width - PADDING - BTN_WIDTH;
		// 三个按钮竖排：上=启动，中=改名，下=关闭(X)
		m_btnStart->setBounds(D2D1::RectF(btnXPos, PADDING,
		                                  btnXPos + BTN_WIDTH, PADDING + BTN_HEIGHT));
		m_btnRename->setBounds(D2D1::RectF(btnXPos, PADDING + BTN_HEIGHT + BTN_GAP,
		                                   btnXPos + BTN_WIDTH, PADDING + BTN_HEIGHT * 2 + BTN_GAP));
		m_btnClose->setBounds(D2D1::RectF(btnXPos, PADDING + (BTN_HEIGHT + BTN_GAP) * 2,
		                                  btnXPos + BTN_WIDTH, PADDING + (BTN_HEIGHT + BTN_GAP) * 2 + BTN_HEIGHT));

		// constexpr float startBtnYPos = PADDING + TITLE_HEIGHT + LINE1_GAP + COUNT_HEIGHT + LINE2_GAP;
		// m_btnStart->setBounds(D2D1::RectF(PADDING, startBtnYPos, PADDING + START_BTN_WIDTH, startBtnYPos + START_BTN_HEIGHT));

		// 名称溢出检测的可用宽度（列表模式才有效，卡片模式不提示）
		if (m_viewMode == EViewMode::List)
		{
			m_nameAreaWidth = std::max(0.f, width - 2.f * PADDING - 3.f * 26.f - 8.f - 16.f);
		}
		else
		{
			m_nameAreaWidth = 0.f;
		}
		updateNameLayout();
	}

	void EnvBoxCard::onMouseEnter(const MouseEvent& e)
	{
		m_isHovered = true;
		updateWholeWnd();

		// 列表模式下名称溢出（碰到按钮被截断）时，鼠标悬浮在名称区域显示完整名称
		if (m_viewMode == EViewMode::List && m_bNameOverflow)
		{
			const auto& bounds = getBoundsInOwner();
			const float localX = e.point.x - bounds.left;
			const float localY = e.point.y - bounds.top;
			const auto s = size();
			if (localX >= PADDING + 16.f && localX <= PADDING + 16.f + m_nameAreaWidth &&
				localY >= 0.f && localY <= s.height)
			{
				owner()->setTooltip(m_name, e.point);
			}
		}
	}

	void EnvBoxCard::onMouseLeave(const MouseEvent& e)
	{
		// 1.进入子控件会触发leave
		// 2.离开子控件也会触发leave(除非子控件拦截)
		if (!hitTest(e.point))
		{
			m_isHovered = false;
			owner()->clearTooltip();
			updateWholeWnd();
		}
	}

	void EnvBoxCard::onClick(const MouseEvent& e)
	{
		// 刚完成一次拖拽排序：本次释放不再当作“选中”点击（避免拖拽结束后误切换选中）
		if (m_justDragged)
		{
			m_justDragged = false;
			return;
		}

		m_isSelected = !m_isSelected;

		if (m_pfnOnSelect)
		{
			m_pfnOnSelect(m_isSelected);
		}
	}

	void EnvBoxCard::onMouseDown(const MouseEvent& e)
	{
		m_pressing = true;
		m_dragging = false;
		m_justDragged = false;
		m_pressTime = std::chrono::steady_clock::now();
		m_pressOwnerX = e.point.x;
		m_pressOwnerY = e.point.y;
	}

	void EnvBoxCard::onMouseMove(const MouseEvent& e)
	{
		if (!m_pressing)
		{
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_pressTime).count();
		const float dx = e.point.x - m_pressOwnerX;
		const float dy = e.point.y - m_pressOwnerY;
		const bool longPress = elapsedMs >= 250; // 长按 250ms 后移动即进入拖拽（原 500ms，响应更灵敏）
		const bool moved = std::abs(dx) > 4.f || std::abs(dy) > 4.f;
		const bool quickDrag = moved && (dx * dx + dy * dy) >= 196.f; // 明显快速拖动（>14px）无需等待长按

		// 长按后移动（或明显快速拖动）才进入拖拽模式，避免影响普通点击
		if (!m_dragging && moved && (longPress || quickDrag))
		{
			m_dragging = true;
			if (m_pfnOnDragStart)
			{
				m_pfnOnDragStart(this, e.point.x, e.point.y);
			}
		}
		if (m_dragging && m_pfnOnDragMove)
		{
			m_pfnOnDragMove(this, e.point.x, e.point.y);
		}
	}

	void EnvBoxCard::onMouseUp(const MouseEvent& e)
	{
		m_pressing = false;
		if (m_dragging)
		{
			m_dragging = false;
			m_justDragged = true;
			if (m_pfnOnDragEnd)
			{
				m_pfnOnDragEnd(this, false);
			}
		}
	}

	void EnvBoxCard::drawImpl(const RenderContext& renderCtx)
	{
		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const auto drawSize = size();

		const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
			D2D1::RectF(0.f, 0.f, drawSize.width, drawSize.height),
			12.0f, 12.0f);
		if (m_dragging)
		{
			// 拖拽中：半透明蓝底 + 蓝色粗描边，直观反馈当前卡片正在被拖动
			solidBrush->SetColor(D2D1::ColorF(0xbbdefb, 0.85f));
			renderTarget->FillRoundedRectangle(roundedRect, solidBrush);
			solidBrush->SetColor(D2D1::ColorF(0x0078d4, 1.f));
			renderTarget->DrawRoundedRectangle(roundedRect, solidBrush, 2.0f);
		}
		else if (m_isHovered || m_isSelected || m_isBright)
		{
			solidBrush->SetColor(D2D1::ColorF(0xf0f8ff));
			renderTarget->FillRoundedRectangle(roundedRect, solidBrush);

			solidBrush->SetColor(D2D1::ColorF(0x0078d4));
			renderTarget->DrawRoundedRectangle(roundedRect, solidBrush);
		}
		else
		{
			solidBrush->SetColor(D2D1::ColorF(0xffffff));
			renderTarget->FillRoundedRectangle(roundedRect, solidBrush);

			solidBrush->SetColor(D2D1::ColorF(0xe9e9e9));
			renderTarget->DrawRoundedRectangle(roundedRect, solidBrush);
		}

		if (m_viewMode == EViewMode::List)
		{
			// 列表模式：圆点（绿=在线，灰=离线）+ 环境名称 + 右侧三个图标按钮
			const bool bOnline = m_procCount > 0;
			const float cy = drawSize.height * 0.5f;
			solidBrush->SetColor(bOnline ? D2D1::ColorF(0x4caf50) : D2D1::ColorF(0xbdbdbd));
			renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(PADDING + 5.f, cy), 4.f, 4.f), solidBrush);

			// 名称垂直居中，与圆点/图标保持同一行
			constexpr float nameLineHeight = 22.f;
			const float nameTop = (drawSize.height - nameLineHeight) * 0.5f;
			solidBrush->SetColor(D2D1::ColorF(0x333333));
			renderTarget->DrawTextW(m_name.c_str(),
			                        static_cast<UINT32>(m_name.length()),
			                        app().textFormat().pTitleFormat,
			                        D2D1::RectF(PADDING + 16.f, nameTop,
			                                    drawSize.width - PADDING - 3.f * 26.f - 8.f, nameTop + nameLineHeight),
			                        solidBrush);
			m_btnStart->draw(renderCtx);
			m_btnRename->draw(renderCtx);
			m_btnClose->draw(renderCtx);
			return;
		}

		solidBrush->SetColor(D2D1::ColorF(0x333333));
		renderTarget->DrawTextW(m_name.c_str(),
		                        static_cast<UINT32>(m_name.length()),
		                        app().textFormat().pTitleFormat,
		                        D2D1::RectF(PADDING, PADDING,
		                                    drawSize.width - PADDING - BTN_WIDTH - 10.f, PADDING + TITLE_HEIGHT),
		                        solidBrush);
		m_btnStart->draw(renderCtx);
		m_btnRename->draw(renderCtx);
		m_btnClose->draw(renderCtx);

		// 状态行：首次初始化提示（新建环境）/ 在线（绿色圆点）/ 离线（灰色圆点）
		constexpr float countLabelTop = PADDING + TITLE_HEIGHT + LINE1_GAP;
		const bool bOnline = m_procCount > 0;
		const bool bFirstInit = m_bFirstLaunchPending && !bOnline;
		constexpr float statusDotRadius = 4.f;
		solidBrush->SetColor(bFirstInit ? D2D1::ColorF(0xed6c00)
		                                : (bOnline ? D2D1::ColorF(0x4caf50) : D2D1::ColorF(0xbdbdbd)));
		renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(PADDING + 5.f, countLabelTop + COUNT_HEIGHT / 2.f),
		                                        statusDotRadius, statusDotRadius), solidBrush);

		std::wstring statusText;
		if (bFirstInit)
		{
			statusText = L"首次初始化中，请稍候…";
			solidBrush->SetColor(D2D1::ColorF(0xed6c00));
		}
		else if (bOnline)
		{
			statusText = std::format(L"在线 · {} 个进程", m_procCount);
			solidBrush->SetColor(D2D1::ColorF(0x2e7d32));
		}
		else
		{
			statusText = L"离线";
			solidBrush->SetColor(D2D1::ColorF(0x757575));
		}
		renderTarget->DrawTextW(statusText.c_str(),
		                        static_cast<UINT32>(statusText.length()),
		                        app().textFormat().pTipsFormat,
		                        D2D1::RectF(PADDING + 14.f, countLabelTop,
		                                    drawSize.width - PADDING, countLabelTop + COUNT_HEIGHT),
		                        solidBrush);

		// 运行时间：灰色，显示在在线/离线下方
		if (bOnline && m_startTime.time_since_epoch().count() != 0)
		{
			const std::wstring runtimeText = format_runtime(std::chrono::steady_clock::now() - m_startTime);
			constexpr float runtimeTop = countLabelTop + COUNT_HEIGHT + 2.f;
			solidBrush->SetColor(D2D1::ColorF(0x9e9e9e));
			renderTarget->DrawTextW(runtimeText.c_str(),
			                        static_cast<UINT32>(runtimeText.length()),
			                        app().textFormat().pTipsFormat,
			                        D2D1::RectF(PADDING, runtimeTop,
			                                    drawSize.width - PADDING - BTN_WIDTH - 10.f, runtimeTop + COUNT_HEIGHT),
			                        solidBrush);
		}

		// m_btnStart->draw(renderCtx);
	}

	coro::LazyTask<void> EnvBoxCard::resetToIdleLater()
	{
		std::uint32_t twinkleCount = 4;
		while (twinkleCount > 0)
		{
			m_isBright = true;
			// 只重绘卡片自身区域：updateWholeWnd 会让整个主窗口重建绘制，
			// 闪烁 800ms 内 8 次全窗重绘纯属浪费（多卡片时拖慢整体渲染）
			update();
			co_await sched::transfer_after(std::chrono::milliseconds{200}, app().get_scheduler());
			m_isBright = false;
			update();
			co_await sched::transfer_after(std::chrono::milliseconds{200}, app().get_scheduler());
			twinkleCount--;
		}

		m_bIdle = true;
	}

	coro::LazyTask<void> EnvBoxCard::tickFirstLaunch()
	{
		// 首次初始化提示期间每秒刷新重绘；pending 清除后（进程出现并持续运行
		// 满阈值）提示自动消失。卡片销毁时 m_stopSource 请求取消，transfer_after
		// 抛出的取消异常被 OnewayTask 吞掉，协程干净结束，不会阻塞 AsyncScope::join。
		while (m_env && m_env->isFirstLaunchPending())
		{
			co_await sched::transfer_after(std::chrono::seconds{1}, app().get_scheduler());
			if (!m_env->isFirstLaunchPending())
			{
				break;
			}
			update();
		}
		m_bFirstLaunchPending = false;
		update();
		co_return;
	}

	coro::LazyTask<void> EnvBoxCard::activateHiddenWhenVisible(HWND wnd)
	{
		// “隐藏到托盘/关闭主界面”(SW_HIDE、任务栏不可见)的主窗，**绝不能外部强显**。
		// 实测任何外部显示（SW_SHOWNORMAL / SW_SHOW 都一样）都会让 WXWork 自绘主窗渲染
		// 成一团黑，再拖拽黑框还会触发其内部空指针崩溃（0xC0000005；这是 v3.0.2 引入
		// SW_SHOWNORMAL 后才出现的黑屏）。这里采用：先等一小段（1.5s），WXWork 自行显示
		// （登录完成/自身恢复，渲染状态正常）则仅置前——不黑屏；若 1.5s 后仍隐藏，则判定
		// 为已缩到托盘/关闭界面，不强显，改为提示用户手动打开（托盘图标 / 显示快捷键 /
		// 停止进程再启动）。
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1500};
		while (std::chrono::steady_clock::now() < deadline)
		{
			co_await sched::transfer_after(std::chrono::milliseconds{200}, app().get_scheduler());
			if (!::IsWindow(wnd))
			{
				co_return; // 窗口已销毁，无需再置前
			}
			if (::IsIconic(wnd))
			{
				// 若等待期间被最小化，还原到正常状态（最小化是自身正常状态，SW_RESTORE 安全）
				::ShowWindow(wnd, SW_RESTORE);
			}
			if (!::IsWindowVisible(wnd))
			{
				continue; // 仍未显示，继续等 WXWork 自行显示
			}
			// WXWork 已自行显示，此时渲染状态正常，仅置前（不强显、不触碰可见性）
			const HWND fg = ::GetForegroundWindow();
			const DWORD fgThread = fg ? ::GetWindowThreadProcessId(fg, nullptr) : 0;
			const DWORD targetThread = ::GetWindowThreadProcessId(wnd, nullptr);
			if (fgThread && fgThread != targetThread)
			{
				::AttachThreadInput(fgThread, targetThread, TRUE);
				::BringWindowToTop(wnd);
				::SetForegroundWindow(wnd);
				::AttachThreadInput(fgThread, targetThread, FALSE);
			}
			else
			{
				::BringWindowToTop(wnd);
				::SetForegroundWindow(wnd);
			}
			{
				DWORD pid = 0;
				::GetWindowThreadProcessId(wnd, &pid);
				biz::env_logger().append(m_env->getIndex(), biz::EnvLogType::Message, biz::EnvLogStatus::Info, L"窗口激活",
				                         std::format(L"{}：窗口已由应用自行显示，已置前 [pid={}]", m_env->getName(), pid));
			}
			co_return;
		}
		// 1.5s 内 WXWork 仍未自行显示 → 判定为已缩到托盘/关闭主界面（任务栏不可见）。
		// 实测外部强显（SW_SHOWNORMAL / SW_SHOW 都一样）会让 WXWork 自绘主窗渲染成一团黑，
		// 再拖拽黑框还会触发其内部空指针崩溃（0xC0000005）。因此**绝不强显**，改为弹窗提示
		// 用户手动打开界面（托盘图标 / 显示快捷键 / 关闭进程后再启动企业微信）。
		if (!::IsWindow(wnd))
		{
			co_return;
		}
		DWORD pid = 0;
		::GetWindowThreadProcessId(wnd, &pid);
		biz::env_logger().append(m_env->getIndex(), biz::EnvLogType::Message, biz::EnvLogStatus::Info, L"窗口激活",
		                         std::format(L"{}：隐藏窗口 1.5s 未自显(任务栏不可见)，不强显避免黑屏，改为提示用户手动打开 [pid={}]", m_env->getName(), pid));
		const std::wstring msg = std::format(
			L"企业微信「{}」已隐藏到托盘（任务栏不可见），eBox 不会强显以免黑屏。\n\n"
			L"请选择以下方式手动打开：\n"
			L"① 点击系统托盘/任务栏右侧的企微图标重新打开主面板；\n"
			L"② 在「企业微信-设置-快捷键」中设置“显示主面板”快捷键后直接呼出；\n"
			L"③ 关闭该环境进程后再点【启动】企业微信。", m_env->getName());
		::MessageBoxW(m_ownerWnd ? m_ownerWnd->nativeHandle() : nullptr, msg.c_str(), MainApp::appName.data(), MB_OK | MB_ICONINFORMATION);
	}

	bool EnvBoxCard::activateEnvWindows()
	{
		if (!m_env)
		{
			return false;
		}
		const std::vector<void*> allWnds = m_env->getAllToplevelWindows();
		const std::uint32_t envIdx = m_env->getIndex();
		int titleWinCount = 0;
		// 记录一次“点启动”的反馈：选窗依据/选中窗口详情/执行的操作全部写入环境日志，
		// 便于诊断“点了第二个号变黑”这类选错窗口或置前异常。
		auto wndInfo = [](HWND h) -> std::wstring
		{
			DWORD pid = 0;
			::GetWindowThreadProcessId(h, &pid);
			wchar_t cls[128]{}; ::GetClassNameW(h, cls, 128);
			wchar_t title[128]{}; ::GetWindowTextW(h, title, 128);
			RECT rc{}; ::GetWindowRect(h, &rc);
			return std::format(L"pid={} class='{}' title='{}' rect={}x{} visible={} iconic={}",
			                   pid, cls, title, rc.right - rc.left, rc.bottom - rc.top,
			                   ::IsWindowVisible(h) ? L"Y" : L"N", ::IsIconic(h) ? L"Y" : L"N");
		};

		// 选“主窗口”：优先取标题含“企业微信”的窗口，再在其中挑面积最大者。
		// 原因：企业微信窗口结构里存在一个比主窗口更大的“无标题背景/蒙版窗”
		// （实测如 1022x784 的黑边框），若只看面积会优先误选它，导致点启动后
		// 弹出黑色空框而非真正的主界面。主窗口固定是标题为“企业微信”的那个，
		// 故必须按标题筛选。
		//
		// 另一注意点：在“标题命中”里也要**优先可见**的主窗。若不区分可见性仅按
		// 面积挑，可能选中面积更大但被 SW_HIDE 隐藏的主窗（可见=N），点启动就会落
		// 到隐藏还原分支而“无反应”；而真正正在显示（任务栏有、仅不在台前）的可见
		// 窗口反而被比下去。最小化窗口 IsWindowVisible 仍为 TRUE，会被优先挑中并走
		// 正常的 SW_RESTORE 分支，符合预期。
		HWND bestWnd = nullptr;
		long bestArea = -1;
		HWND bestVisibleWnd = nullptr;   // 标题命中且可见的主窗
		long bestVisibleArea = -1;
		int titleVisibleCount = 0;       // 标题命中且可见的窗口个数
		for (void* raw : allWnds)
		{
			const HWND hWnd = static_cast<HWND>(raw);
			if (!::IsWindow(hWnd))
			{
				continue;
			}
			wchar_t title[128]{};
			if (::GetWindowTextW(hWnd, title, 128) <= 0 || !wcsstr(title, L"企业微信"))
			{
				continue;
			}
			++titleWinCount;
			RECT rc{};
			if (!::GetWindowRect(hWnd, &rc))
			{
				continue;
			}
			const long area = (rc.right - rc.left) * (rc.bottom - rc.top);
			if (area > bestArea)
			{
				bestArea = area;
				bestWnd = hWnd;
			}
			if (::IsWindowVisible(hWnd))
			{
				++titleVisibleCount;
				if (area > bestVisibleArea)
				{
					bestVisibleArea = area;
					bestVisibleWnd = hWnd;
				}
			}
		}
		// 优先级：标题命中且可见 > 标题命中（含隐藏/最小化）> 全量面积兜底。
		if (bestVisibleWnd)
		{
			bestWnd = bestVisibleWnd;
		}
		// 找不到标题为“企业微信”的窗口时，退化为所有有效窗口里面积最大者。
		if (bestWnd)
		{
			biz::env_logger().append(envIdx, biz::EnvLogType::Message, biz::EnvLogStatus::Info, L"窗口激活",
			                         std::format(L"{}：标题命中 {} 个(可见 {} 个)，选取{}", m_env->getName(), titleWinCount, titleVisibleCount,
			                                    bestVisibleWnd ? L"其中面积最大且可见的主窗" : L"标题命中中面积最大者(无可见主窗)"));
		}
		else
		{
			biz::env_logger().append(envIdx, biz::EnvLogType::Message, biz::EnvLogStatus::Info, L"窗口激活",
			                         std::format(L"{}：未找到标题含『企业微信』的窗口，共 {} 个顶层窗口，转入『面积最大』兜底", m_env->getName(), allWnds.size()));
		}
		if (!bestWnd)
		{
			for (void* raw : allWnds)
			{
				const HWND hWnd = static_cast<HWND>(raw);
				if (!::IsWindow(hWnd))
				{
					continue;
				}
				RECT rc{};
				if (!::GetWindowRect(hWnd, &rc))
				{
					continue;
				}
				const long area = (rc.right - rc.left) * (rc.bottom - rc.top);
				if (area > bestArea)
				{
					bestArea = area;
					bestWnd = hWnd;
				}
			}
		}
		if (!bestWnd)
		{
			return false;
		}
		biz::env_logger().append(envIdx, biz::EnvLogType::Message, biz::EnvLogStatus::Info, L"窗口激活",
		                         std::format(L"{}：选中主窗口 [{}]", m_env->getName(), wndInfo(bestWnd)));

		// 激活前先探测目标窗口线程是否响应：AttachThreadInput / SetForegroundWindow /
		// BringWindowToTop 作用于“未响应或繁忙”的外部进程窗口线程时，会让调用它的
		// UI 线程一起阻塞（Windows 经典冻结源）。这里用 SendMessageTimeout(WM_NULL,
		// SMTO_ABORTIFHUNG) 快速探测，超时（>=1 秒无响应）即视为挂死，放弃激活直接返回，
		// 确保 UI 线程永不被远程窗口拖住。
		{
			DWORD_PTR lr = 0;
			if (!::SendMessageTimeoutW(bestWnd, WM_NULL, 0, 0,
			                           SMTO_NORMAL | SMTO_ABORTIFHUNG, 1000, &lr))
			{
				// 目标已挂死：不做任何可能阻塞的窗口操作，仅“识别到已有窗口”以阻止重复启动
				biz::env_logger().append(envIdx, biz::EnvLogType::Message, biz::EnvLogStatus::Fail, L"窗口激活",
				                         std::format(L"{}：目标窗口无响应(>1s)，已放弃置前，仅阻止重复启动 [{}]", m_env->getName(), wndInfo(bestWnd)));
				return true;
			}
		}

		// 窗口恢复：只处理“最小化”这一种状态 —— 最小化是窗口自身的正常状态，用
		// SW_RESTORE 交回 Windows 还原即可，不会破坏 WXWork 自绘渲染。
		//
		// 特别注意：**绝不能对“隐藏到托盘”(SW_HIDE / !IsWindowVisible) 的主窗口强显
		// SW_SHOWNORMAL**。企业微信的自绘主窗依赖它自己维护的激活与渲染状态，外部强显
		// 会绕过其内部初始化流程，导致窗口一片黑；用户再拖拽该黑框还会触发 WXWork 内部
		// 空指针崩溃（0xC0000005）。这正是 v3.0.2 引入 SW_SHOWNORMAL 之后才出现的
		// “点第二个号变黑”的根源（用户明确反馈：未加该逻辑的旧版只偶尔点击无反应，
		// 从无黑框）。
		// 因此对隐藏窗口：不走下方“仅置前”（那对已隐藏窗口不会真的显示它），而是交由
		// activateHiddenWhenVisible 两段式处理——先等 WXWork 自行显示（不黑屏），1.5s 仍
		// 隐藏（托盘常驻）则不强显、改为弹窗提示用户手动打开（托盘图标/显示快捷键/关闭
		// 进程后再启动企业微信）。既不会黑屏，也不会在用户未察觉时强弹黑框。
		if (::IsIconic(bestWnd))
		{
			::ShowWindow(bestWnd, SW_RESTORE);
			biz::env_logger().append(envIdx, biz::EnvLogType::Message, biz::EnvLogStatus::Info, L"窗口激活",
			                         std::format(L"{}：最小化窗口 SW_RESTORE 还原 [{}]", m_env->getName(), wndInfo(bestWnd)));
		}
		else if (!::IsWindowVisible(bestWnd))
		{
			// 隐藏到托盘：不强显 SW_SHOWNORMAL / SW_SHOW（→ 黑屏 + 拖拽崩溃），改为两段式——
			// 先等 WXWork 自行显示，1.5s 仍隐藏则不强显，改为弹窗提示用户手动打开；本环境已有
			// 窗口，返回 true 阻止重复启动。
			biz::env_logger().append(envIdx, biz::EnvLogType::Message, biz::EnvLogStatus::Info, L"窗口激活",
			                         std::format(L"{}：隐藏窗口 两段式——先等自显，1.5s 仍隐藏则不强显改为提示手动打开 [{}]", m_env->getName(), wndInfo(bestWnd)));
			m_asyncScope.spawn(coro::co_with_cancellation(activateHiddenWhenVisible(bestWnd), m_stopSource.get_token()));
			return true;
		}
		// 绕过 Windows 前台锁：把前台线程输入临时附加到目标窗口线程再置前
		const HWND fg = ::GetForegroundWindow();
		const DWORD fgThread = fg ? ::GetWindowThreadProcessId(fg, nullptr) : 0;
		const DWORD targetThread = ::GetWindowThreadProcessId(bestWnd, nullptr);
		if (fgThread && fgThread != targetThread)
		{
			::AttachThreadInput(fgThread, targetThread, TRUE);
			::BringWindowToTop(bestWnd);
			::SetForegroundWindow(bestWnd);
			::AttachThreadInput(fgThread, targetThread, FALSE);
		}
		else
		{
			::BringWindowToTop(bestWnd);
			::SetForegroundWindow(bestWnd);
		}
		return true;
	}

	coro::LazyTask<void> EnvBoxCard::onProcessCountChange(biz::Env::EProcEvent e, std::shared_ptr<biz::ProcessInfo> proc, std::size_t count)
	{
		// 转到主线程
		co_await sched::transfer_to(app().get_scheduler());
		m_procCount = count;
		m_strProcCount = std::format(L"{}", count);

		if (m_env)
		{
			if (e == biz::Env::EProcEvent::Create)
			{
				biz::env_logger().append(m_env->getIndex(), biz::EnvLogType::Process, biz::EnvLogStatus::Success,
				                         L"进程启动", std::format(L"PID {} {}", proc->getProcessId(), proc->getProcessFullPath()));
			}
			else
			{
				biz::env_logger().append(m_env->getIndex(), biz::EnvLogType::Process, biz::EnvLogStatus::Info,
				                         L"进程退出", std::format(L"PID {} {}", proc->getProcessId(), proc->getProcessFullPath()));
			}
		}

		if (m_procCount > 0 && m_startTime.time_since_epoch().count() == 0)
		{
			// 环境首次有进程时记录启动时刻，并启动每秒刷新计时。
			// 用 m_lifeStopSource 包裹：退出销毁时（m_procCount 可能因通知未处理
			// 而未归零）request_stop 同步取消这个 while(true) 循环，否则
			// AsyncScope::join() 将无限阻塞，eBox.exe 退出后残留进程。
			m_startTime = std::chrono::steady_clock::now();
			m_asyncScope.spawn(coro::co_with_cancellation(tickRuntime(), m_lifeStopSource.get_token()));
		}
		if (m_procCount == 0)
		{
			m_startTime = {};
			if (m_env)
			{
				biz::env_logger().append(m_env->getIndex(), biz::EnvLogType::Info, biz::EnvLogStatus::Info,
				                         L"环境离线", L"环境内进程已全部退出");
			}
		}

		if (m_pfnOnSummaryChange)
		{
			m_pfnOnSummaryChange();
		}

		if (m_isSelected)
		{
			if (m_pfnOnProcCountChange)
			{
				m_pfnOnProcCountChange(e, proc);
			}
		}

		update();
	}

	coro::LazyTask<void> EnvBoxCard::tickRuntime()
	{
		while (true)
		{
			co_await sched::transfer_after(std::chrono::seconds{1}, app().get_scheduler());
			// 卡片销毁时由析构函数请求取消，这里双保险直接退出循环
			if (m_lifeStopSource.stop_requested() || m_procCount == 0)
			{
				break;
			}
			update();
		}
		co_return;
	}

	// void EnvBoxCard::onBtnStartPressed()
	// {
	// 	if (const std::optional<std::wstring> fullPath = select_file(m_ownerWnd))
	// 	{
	// 		launchProcess(fullPath.value());
	// 	}
	// }
}
