module UI.EnvDetail;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import MainApp;

namespace
{
	constexpr float PADDING = 16.f;
	constexpr float GAP = 8.f;
	constexpr float BUTTON_WIDTH = 78.f;
	constexpr float BUTTON_HEIGHT = 36.f;
	constexpr float CLEAR_BTN_WIDTH = 24.f;
	constexpr float CLEAR_BTN_HEIGHT = CLEAR_BTN_WIDTH;
	constexpr float SCROLL_WIDTH = 8.f;
	constexpr float WHEEL_SCROLL_SIZE = 24.f;
	// 进程记录标题占位宽度：与路径框/按钮同一行，标题在最左，路径框在标题右侧
	constexpr float PROC_TITLE_WIDTH = 60.f;
	// 标题行高：环境名称（加粗 14px）与“进程记录”等高对齐、垂直居中
	constexpr float PROC_TITLE_LINE_HEIGHT = 20.f;
	constexpr float LIST_Y_POS_START = PADDING + BUTTON_HEIGHT + GAP;
	constexpr float LIST_ITEM_HEIGHT = 94.f;
	// 进程卡片与日志卡片之间的间隔
	constexpr float CARD_GAP = 14.f;
	// 进程区域折叠按钮尺寸
	constexpr float COLLAPSE_BTN_WIDTH = 52.f;
	constexpr float COLLAPSE_BTN_HEIGHT = 26.f;

	constexpr float LIST_ITEM_GAP = 4.f;
	constexpr float LIST_ITEM_TITLE_HEIGHT = 24.f;
	constexpr float LIST_ITEM_TIPS_HEIGHT = 20.f;
	// 环境信息卡片标题行高度（与日志卡片一致）
	constexpr float INFO_HEADER_HEIGHT = 28.f;
	// 日志卡片标题行高度（EnvLogPanel 内部 HEADER_HEIGHT）
	constexpr float LOG_HEADER_HEIGHT = 28.f;

	constexpr std::wstring_view NO_PROC_TEXT = L"暂无进程";
	// 首次启动提示条尺寸（新建环境首次启动时显示"稍慢请稍候"提示）
	constexpr float FIRST_LAUNCH_TIP_HEIGHT = 52.f;
	constexpr float FIRST_LAUNCH_TIP_GAP = 8.f;
}

namespace ui
{
	void ProcessList::initialize()
	{
		m_scrollBar.setThumbPosChangeNotify([this] { updateAllItemPos(); });
	}

	void ProcessList::setEnv(const std::shared_ptr<biz::Env>& env)
	{
		m_env = env;
		m_processes.clear();
		std::vector<std::shared_ptr<biz::ProcessInfo>> allProc = env->getAllProcesses();
		for (const std::shared_ptr<biz::ProcessInfo>& proc : allProc)
		{
			m_processes.insert(std::make_pair(proc->getProcessId(), ListItem{proc}));
		}
		m_scrollBar.setTotalSize(m_processes.size() * LIST_ITEM_HEIGHT);
	}

	void ProcessList::clearEnv()
	{
		m_env.reset();
		m_processes.clear();
		m_scrollBar.setTotalSize(0.f);
	}

	void ProcessList::procCountChange(biz::Env::EProcEvent e, const std::shared_ptr<biz::ProcessInfo>& proc)
	{
		if (e == biz::Env::EProcEvent::Create)
		{
			if (m_processes.contains(proc->getProcessId()))
			{
				return;
			}
			m_processes.insert(std::make_pair(proc->getProcessId(), ListItem{proc}));
		}
		else if (e == biz::Env::EProcEvent::Terminate)
		{
			m_processes.erase(proc->getProcessId());
		}
		m_scrollBar.setTotalSize(m_processes.size() * LIST_ITEM_HEIGHT);
	}

	void ProcessList::onResize(float width, float height)
	{
		m_scrollBar.setVisibleSize(height);
		m_scrollBar.setBounds(D2D1::RectF(width - SCROLL_WIDTH - 4.f, 0, width - 4.f, height));
	}

	void ProcessList::onMouseEnter(const MouseEvent& e)
	{
		e.accept = true;

		m_isHovered = true;
		update();
	}

	void ProcessList::onMouseLeave(const MouseEvent& e)
	{
		e.accept = true;

		m_isHovered = false;
		update();
	}

	void ProcessList::onMouseWheel(const MouseWheelEvent& e)
	{
		e.accept = true;

		const float wheelCount = e.zDelta / 120.f;
		m_scrollBar.scroll(-wheelCount * WHEEL_SCROLL_SIZE);
	}

	void ProcessList::drawImpl(const RenderContext& renderCtx)
	{
		if (m_processesToDraw.empty())
		{
			return;
		}

		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;

		namespace fs = std::filesystem;
		app().textFormat().setTitleEllipsisTrimming();
		app().textFormat().setTipsEllipsisTrimming();
		for (auto it = m_processesToDraw.begin(); it != m_processesToDraw.end(); ++it)
		{
			ListItem* item = *it;
			biz::ProcessInfo* processInfo = item->process.get();
			const D2D1_RECT_F& rc = item->rect;
			const fs::path fullPath{processInfo->getProcessFullPath()};
			const fs::path procName = fullPath.stem();
			float yPos = rc.top + PADDING;
			solidBrush->SetColor(D2D1::ColorF(0x1a1a1a));
			renderTarget->DrawTextW(procName.native().c_str(),
			                        static_cast<UINT32>(procName.native().size()),
			                        app().textFormat().pTitleFormat,
			                        D2D1::RectF(0.f, yPos, rc.right, yPos + LIST_ITEM_TITLE_HEIGHT), solidBrush);
			yPos += LIST_ITEM_TITLE_HEIGHT + LIST_ITEM_GAP;
			const std::wstring pid = std::format(L"PID: {}", processInfo->getProcessId());
			solidBrush->SetColor(D2D1::ColorF(0x666666));
			renderTarget->DrawTextW(pid.c_str(),
			                        static_cast<UINT32>(pid.size()),
			                        app().textFormat().pTipsFormat,
			                        D2D1::RectF(0.f, yPos, rc.right, yPos + LIST_ITEM_TIPS_HEIGHT), solidBrush);
			yPos += LIST_ITEM_TIPS_HEIGHT + LIST_ITEM_GAP;
			renderTarget->DrawTextW(fullPath.native().c_str(),
			                        static_cast<UINT32>(fullPath.native().size()),
			                        app().textFormat().pTipsFormat,
			                        D2D1::RectF(0.f, yPos, rc.right, yPos + LIST_ITEM_TIPS_HEIGHT), solidBrush);

			solidBrush->SetColor(D2D1::ColorF(0xf0f0f0));
			renderTarget->DrawLine(D2D1::Point2F(rc.left, rc.bottom), D2D1::Point2F(rc.right, rc.bottom), solidBrush);
		}
		app().textFormat().clearTitleTrimming();
		app().textFormat().clearTipsEllipsisTrimming();

		if (m_isHovered || m_scrollBar.isThumbPressed())
		{
			m_scrollBar.draw(renderCtx);
		}
	}

	void ProcessList::updateAllItemPos()
	{
		if (m_processes.empty())
		{
			m_processesToDraw.clear();
			update();
			return;
		}

		const float scrollOffset = m_scrollBar.getScrollOffset();
		const std::uint32_t startIndex = static_cast<std::uint32_t>(scrollOffset / LIST_ITEM_HEIGHT);
		const std::uint32_t endIndex = std::min(
			static_cast<std::uint32_t>((scrollOffset + m_scrollBar.getVisibleSize()) / LIST_ITEM_HEIGHT),
			static_cast<std::uint32_t>(m_processes.size() - 1));
		m_processesToDraw.clear();

		if (startIndex > endIndex)
		{
			update();
			return;
		}

		m_processesToDraw.reserve(endIndex - startIndex);

		const std::uint32_t startDrawOffsetY = static_cast<std::uint32_t>(scrollOffset) % static_cast<std::uint32_t>(LIST_ITEM_HEIGHT);
		float startYPos = 0.f - startDrawOffsetY;
		const auto drawSize = size();
		std::size_t index = 0;
		for (auto it = m_processes.begin(); it != m_processes.end(); ++it, ++index)
		{
			if (index < startIndex || index > endIndex)
			{
				continue;
			}
			ListItem& item = it->second;
			item.rect = D2D1::RectF(0.f, startYPos, drawSize.width - PADDING, startYPos + LIST_ITEM_HEIGHT);
			m_processesToDraw.push_back(&item);
			startYPos += LIST_ITEM_HEIGHT;
		}
		update();
	}

	void EnvDetail::initialize()
	{
		m_btnClear.setText(L"x");
		m_btnClear.setBackgroundColor(D2D1::ColorF(0xe0e0e0));
		m_btnClear.setBackgroundColor(D2D1::ColorF(0xf0f0f0), Button::EState::Normal);
		m_btnClear.setTextColor(D2D1::ColorF(0x333333));
		m_btnClear.setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnClear.setOnClick([this]
		{
			m_strProcPath.clear();
			update();
		});

		m_btnLaunch.setText(L"添加进程");
		m_btnLaunch.setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnLaunch.setBackgroundColor(D2D1::ColorF(0xe3f2fd), Button::EState::Hover);
		m_btnLaunch.setBackgroundColor(D2D1::ColorF(0xbbdefb), Button::EState::Active);
		m_btnLaunch.setBorderColor(D2D1::ColorF(0x0078d4));
		m_btnLaunch.setTextColor(D2D1::ColorF(0x0078d4));
		m_btnLaunch.setOnClick([this] { onLaunchBtnClick(); });

		// 进程卡片折叠按钮：折叠时把展开区让给环境信息/环境日志（手风琴）
		m_btnCollapse.setText(L"折叠");
		m_btnCollapse.setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnCollapse.setBackgroundColor(D2D1::ColorF(0xf5f5f5), Button::EState::Hover);
		m_btnCollapse.setBackgroundColor(D2D1::ColorF(0xe0e0e0), Button::EState::Active);
		m_btnCollapse.setBorderColor(D2D1::ColorF(0x9e9e9e));
		m_btnCollapse.setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnCollapse.setTextColor(D2D1::ColorF(0x555555));
		m_btnCollapse.setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnCollapse.setOnClick([this]
		{
			// 当前展开进程记录则收起（切到日志），否则展开进程记录
			setExpandArea(m_expandArea == ExpandArea::Process ? ExpandArea::Log : ExpandArea::Process);
		});

		// 环境信息 / 环境日志 标题栏点击都交回 EnvDetail 统一做手风琴互斥
		m_infoPanel.onExpandRequest = [this](bool expand)
		{
			setExpandArea(expand ? ExpandArea::EnvInfo : ExpandArea::Process);
		};
		m_logPanel.onExpandRequest = [this](bool expand)
		{
			setExpandArea(expand ? ExpandArea::Log : ExpandArea::Process);
		};

		m_noProcTextHeight = LIST_ITEM_TITLE_HEIGHT;
		if (SUCCEEDED(app().dWriteFactory()->CreateTextLayout(NO_PROC_TEXT.data(),
			static_cast<UINT32>(NO_PROC_TEXT.size()),
			app().textFormat().pMainFormat,
			std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
			&m_noProcTextLayout)))
		{
			m_noProcTextLayout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
			DWRITE_TEXT_METRICS textMetrics;
			if (SUCCEEDED(m_noProcTextLayout->GetMetrics(&textMetrics)))
			{
				m_noProcTextHeight = textMetrics.height;
			}
		}
	}

	void EnvDetail::setEnv(const std::shared_ptr<biz::Env>& env)
	{
		m_selectedEnvName = std::wstring{env->getName()};
		// 测量环境名称在加粗 14px 下的实际宽度，标题区按实际宽度动态布局（全显示）
		m_envNameWidth = 0.f;
		if (!m_selectedEnvName.empty())
		{
			UniqueComPtr<IDWriteTextLayout> layout;
			if (SUCCEEDED(app().dWriteFactory()->CreateTextLayout(
				    m_selectedEnvName.c_str(), static_cast<UINT32>(m_selectedEnvName.size()),
				    app().textFormat().pBoldFormat, std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), &layout)))
			{
				DWRITE_TEXT_METRICS metrics{};
				if (SUCCEEDED(layout->GetMetrics(&metrics)))
				{
					m_envNameWidth = metrics.width;
				}
			}
		}
		m_processList.setEnv(env);
		m_logPanel.setEnvIndex(env->getIndex());
		m_infoPanel.setEnv(env);
		update();
	}

	void EnvDetail::clearEnv()
	{
		m_selectedEnvName.clear();
		m_envNameWidth = 0.f;
		m_processList.clearEnv();
		m_logPanel.clearEnv();
		m_infoPanel.clearEnv();
		update();
	}

	void EnvDetail::setExpandArea(ExpandArea area)
	{
		if (m_expandArea == area)
		{
			return;
		}
		m_expandArea = area;
		m_infoPanel.setExpanded(area == ExpandArea::EnvInfo);
		m_logPanel.setExpanded(area == ExpandArea::Log);
		m_btnCollapse.setText(area == ExpandArea::Process ? L"折叠" : L"展开");
		onResize(size().width, size().height);
		update();
	}

	void EnvDetail::onResize(float width, float height)
	{
		// 顶部按钮行（与“进程记录”标题同一行）：路径框 / 清除 / 添加进程 / 折叠
		const float launchBtnXPos = width - PADDING - BUTTON_WIDTH;
		m_btnLaunch.setBounds(D2D1::RectF(launchBtnXPos, PADDING, launchBtnXPos + BUTTON_WIDTH, PADDING + BUTTON_HEIGHT));

		const float collapseBtnXPos = launchBtnXPos - GAP - COLLAPSE_BTN_WIDTH;
		const float collapseBtnYPos = PADDING + (BUTTON_HEIGHT - COLLAPSE_BTN_HEIGHT) * 0.5f;
		m_btnCollapse.setBounds(D2D1::RectF(collapseBtnXPos, collapseBtnYPos,
		                                    collapseBtnXPos + COLLAPSE_BTN_WIDTH, collapseBtnYPos + COLLAPSE_BTN_HEIGHT));

		const float clearBtnXPos = collapseBtnXPos - GAP * 2.f - CLEAR_BTN_WIDTH;
		const float clearBtnYPos = PADDING + (BUTTON_HEIGHT - CLEAR_BTN_HEIGHT) * 0.5f;
		m_btnClear.setBounds(D2D1::RectF(clearBtnXPos, clearBtnYPos, clearBtnXPos + CLEAR_BTN_WIDTH, clearBtnYPos + CLEAR_BTN_HEIGHT));

		// ---- 三卡片手风琴布局（从上到下：进程记录 / 环境信息 / 环境日志）----
		// 固定部分 = 三个标题行 + 两处卡片间隔；剩余空间给当前展开的卡片
		const float fixedH = LIST_Y_POS_START + CARD_GAP + INFO_HEADER_HEIGHT + CARD_GAP + LOG_HEADER_HEIGHT;
		const float contentH = std::max(60.f, height - PADDING - fixedH);

		float procCardH;
		float infoCardH;
		float logCardH;
		switch (m_expandArea)
		{
			case ExpandArea::Process:
				procCardH = LIST_Y_POS_START + contentH;
				infoCardH = INFO_HEADER_HEIGHT;
				logCardH = LOG_HEADER_HEIGHT;
				break;
			case ExpandArea::EnvInfo:
				procCardH = LIST_Y_POS_START;
				infoCardH = INFO_HEADER_HEIGHT + contentH;
				logCardH = LOG_HEADER_HEIGHT;
				break;
			case ExpandArea::Log:
				procCardH = LIST_Y_POS_START;
				infoCardH = INFO_HEADER_HEIGHT;
				logCardH = LOG_HEADER_HEIGHT + contentH;
				break;
		}

		const float infoTop = procCardH + CARD_GAP;
		const float logTop = infoTop + infoCardH + CARD_GAP;

		m_infoPanel.setContentHeight(contentH);
		m_infoPanel.setBounds(D2D1::RectF(0.f, infoTop, width, infoTop + infoCardH));
		m_logPanel.setMaxExpandHeight(contentH);
		m_logPanel.setBounds(D2D1::RectF(0.f, logTop, width, logTop + logCardH));

		// 进程列表：展开进程时填充进程卡片内容区，否则隐藏（高度为 0）
		const float procCardBottom = procCardH;
		const float procListBottom = m_expandArea == ExpandArea::Process ? procCardBottom : LIST_Y_POS_START;
		m_processList.setBounds(D2D1::RectF(PADDING, LIST_Y_POS_START, width, procListBottom));
	}

	void EnvDetail::drawImpl(const RenderContext& renderCtx)
	{
		// 三卡片高度随展开状态变化，每次绘制前按当前状态重排
		onResize(size().width, size().height);

		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const auto [width, height] = size();

		// 进程卡片：顶部卡片，标题行始终显示；展开时才显示进程列表
		const float procCardBottom = m_expandArea == ExpandArea::Process
			? (m_infoPanel.getBounds().top - CARD_GAP)
			: LIST_Y_POS_START;
		const D2D1_ROUNDED_RECT procCard = D2D1::RoundedRect(D2D1::RectF(0.f, 0.f, width, procCardBottom), 8.f, 8.f);
		solidBrush->SetColor(D2D1::ColorF(0xffffff));
		renderTarget->FillRoundedRectangle(procCard, solidBrush);
		solidBrush->SetColor(D2D1::ColorF(0xe0e0e0));
		renderTarget->DrawRoundedRectangle(procCard, solidBrush);

		// 卡片标题行：环境名称（加粗）+“进程记录”等高对齐，路径框/按钮同一行
		const float procTitleY = PADDING + (BUTTON_HEIGHT - PROC_TITLE_LINE_HEIGHT) * 0.5f;
		// 右侧按钮排布（清除/折叠/启动），路径框右边界以此为准
		const float clearBtnXPos = (width - PADDING - BUTTON_WIDTH) - GAP - COLLAPSE_BTN_WIDTH - GAP * 2.f - CLEAR_BTN_WIDTH;
		float titleX = PADDING;
		if (!m_selectedEnvName.empty() && m_envNameWidth > 0.f)
		{
			// 环境名可用最大宽度：保留“进程记录”标题与最小路径框空间；名称短则全显示
			constexpr float minPathWidth = 60.f;
			const float maxEnvNameWidth = std::max(0.f, (clearBtnXPos - GAP - PADDING) - GAP - PROC_TITLE_WIDTH - GAP - minPathWidth);
			const float envNameWidth = std::min(m_envNameWidth, maxEnvNameWidth);
			// 全显示（不加省略号）；仅当宽度不足时才截断
			if (envNameWidth < m_envNameWidth)
			{
				app().textFormat().setBoldEllipsisTrimming();
			}
			else
			{
				app().textFormat().clearBoldTrimming();
			}
			solidBrush->SetColor(D2D1::ColorF(0x0078d4));
			renderTarget->DrawTextW(m_selectedEnvName.c_str(),
			                        static_cast<UINT32>(m_selectedEnvName.size()),
			                        app().textFormat().pBoldFormat,
			                        D2D1::RectF(titleX, procTitleY, titleX + envNameWidth, procTitleY + PROC_TITLE_LINE_HEIGHT),
			                        solidBrush);
			app().textFormat().clearBoldTrimming();
			// 环境名称与“进程记录”标题之间留出间隙
			titleX += envNameWidth + GAP;
		}
		// "进程记录"标题：与环境名称同字号（14px 常规不加粗）、同一标题行，垂直居中平行对齐
		solidBrush->SetColor(D2D1::ColorF(0x333333));
		renderTarget->DrawTextW(L"进程记录",
		                        static_cast<UINT32>(std::wstring_view(L"进程记录").size()),
		                        app().textFormat().pMainFormat,
		                        D2D1::RectF(titleX, procTitleY, titleX + PROC_TITLE_WIDTH, procTitleY + PROC_TITLE_LINE_HEIGHT),
		                        solidBrush);

		if (m_strProcPath.size())
		{
			// 路径框起点跟随环境名称+“进程记录”动态后移，右端固定到清除按钮，宽度自适应
			const float pathXStart = titleX + PROC_TITLE_WIDTH + GAP;
			const D2D1_ROUNDED_RECT pathRect = D2D1::RoundedRect(D2D1::RectF(pathXStart, PADDING,
			                                                                 clearBtnXPos - GAP,
			                                                                 PADDING + BUTTON_HEIGHT), 8.0f, 8.0f);
			solidBrush->SetColor(D2D1::ColorF(0xf8f9fa));
			renderTarget->FillRoundedRectangle(pathRect, solidBrush);
			solidBrush->SetColor(D2D1::ColorF(0xe0e0e0));
			renderTarget->DrawRoundedRectangle(pathRect, solidBrush);

			solidBrush->SetColor(D2D1::ColorF(0x1a1a1a));

			const float textXPos = pathXStart + GAP;
			const float textYPos = PADDING + (BUTTON_HEIGHT - m_pathTextHeight) * 0.5f;
			const float textWidth = pathRect.rect.right - GAP - CLEAR_BTN_WIDTH - textXPos;
			if (m_procPathTextLayout)
			{
				m_procPathTextLayout->SetMaxWidth(textWidth);
				renderTarget->DrawTextLayout(D2D1::Point2F(textXPos, textYPos), m_procPathTextLayout, solidBrush);
			}
			else
			{
				app().textFormat().setMainEllipsisTrimming();
				renderTarget->DrawTextW(m_strProcPath.c_str(),
				                        static_cast<UINT32>(m_strProcPath.length()),
				                        app().textFormat().pMainFormat,
				                        D2D1::RectF(textXPos, textYPos, textXPos + textWidth, PADDING + BUTTON_HEIGHT),
				                        solidBrush);
				app().textFormat().clearMainEllipsisTrimming();
			}
			m_btnClear.draw(renderCtx);
		}

		m_btnLaunch.draw(renderCtx);
		m_btnCollapse.draw(renderCtx);

		if (m_expandArea == ExpandArea::Process)
		{
			// 进程卡片内的分隔线（灰色）
			solidBrush->SetColor(D2D1::ColorF(0xe0e0e0));
			renderTarget->DrawLine(D2D1::Point2F(PADDING, LIST_Y_POS_START), D2D1::Point2F(width - PADDING, LIST_Y_POS_START), solidBrush);

			// 新建环境首次启动提示：首次数据初始化期间提示用户稍候，后续启动恢复正常
			bool firstLaunch = m_processList.hasEnv() && m_processList.getEnv()->isFirstLaunchPending();
			if (firstLaunch)
			{
				// 应用窗口已出现或进程已全部退出，立即清除提示
				if (m_processList.getEnv()->shouldClearFirstLaunchPending())
				{
					m_processList.getEnv()->markFirstLaunchDone();
					firstLaunch = false;
				}
			}
			if (firstLaunch)
			{
				const float tipTop = LIST_Y_POS_START + FIRST_LAUNCH_TIP_GAP;
				const float tipBottom = tipTop + FIRST_LAUNCH_TIP_HEIGHT;
				const D2D1_ROUNDED_RECT tipRect = D2D1::RoundedRect(
					D2D1::RectF(PADDING, tipTop, width - PADDING, tipBottom), 6.f, 6.f);
				solidBrush->SetColor(D2D1::ColorF(0xfff7e6));
				renderTarget->FillRoundedRectangle(tipRect, solidBrush);
				solidBrush->SetColor(D2D1::ColorF(0xe6c27a));
				renderTarget->DrawRoundedRectangle(tipRect, solidBrush);

				solidBrush->SetColor(D2D1::ColorF(0x7a5200));
				constexpr float tipTextX = PADDING + 12.f;
				renderTarget->DrawTextW(L"首次启动加载中，请稍候…",
				                        static_cast<UINT32>(std::wstring_view(L"首次启动加载中，请稍候…").size()),
				                        app().textFormat().pMainFormat,
				                        D2D1::RectF(tipTextX, tipTop + 6.f, width - PADDING - 12.f, tipTop + 26.f),
				                        solidBrush);
				renderTarget->DrawTextW(L"首次启动需初始化数据，约需 15~30 秒，请耐心等待；后续启动将恢复正常速度。",
				                        static_cast<UINT32>(std::wstring_view(L"首次启动需初始化数据，约需 15~30 秒，请耐心等待；后续启动将恢复正常速度。").size()),
				                        app().textFormat().pTipsFormat,
				                        D2D1::RectF(tipTextX, tipTop + 28.f, width - PADDING - 12.f, tipBottom - 6.f),
				                        solidBrush);

				// 进程列表下移，避开提示条
				m_processList.setBounds(D2D1::RectF(PADDING, tipBottom + FIRST_LAUNCH_TIP_GAP, width, procCardBottom));
			}

			if (m_processList.hasAnyProcesses())
			{
				m_processList.draw(renderCtx);
			}
			else if (!firstLaunch)
			{
				solidBrush->SetColor(D2D1::ColorF(0x333333));
				const float listAreaHeight = std::max(0.f, procCardBottom - LIST_Y_POS_START);
				const float textYPos = listAreaHeight > 0.f ? (listAreaHeight - m_noProcTextHeight) * 0.5f : 0.f;
				if (m_noProcTextLayout)
				{
					m_noProcTextLayout->SetMaxWidth(width - PADDING - PADDING);
					m_noProcTextLayout->SetMaxHeight(std::max(1.f, listAreaHeight));
					renderTarget->DrawTextLayout(D2D1::Point2F(PADDING, textYPos), m_noProcTextLayout, solidBrush);
				}
				else
				{
					DWRITE_TEXT_ALIGNMENT oldAlignment = app().textFormat().pMainFormat->GetTextAlignment();
					app().textFormat().pMainFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
					renderTarget->DrawTextW(NO_PROC_TEXT.data(), static_cast<UINT32>(NO_PROC_TEXT.size()), app().textFormat().pMainFormat,
					                        D2D1::RectF(PADDING, textYPos, width - PADDING, height - PADDING), solidBrush);
					app().textFormat().pMainFormat->SetTextAlignment(oldAlignment);
				}
			}
		}

		// 中间：环境信息卡片
		m_infoPanel.draw(renderCtx);

		// 底部：环境日志卡片
		m_logPanel.draw(renderCtx);
	}

	void EnvDetail::setProcPath(std::wstring_view path)
	{
		m_strProcPath = path;
		if (m_strProcPath.size())
		{
			m_pathTextHeight = CLEAR_BTN_HEIGHT;
			m_procPathTextLayout.reset();
			app().textFormat().setMainEllipsisTrimming();
			if (SUCCEEDED(app().dWriteFactory()->CreateTextLayout(m_strProcPath.c_str(),
				static_cast<UINT32>(m_strProcPath.size()),
				app().textFormat().pMainFormat,
				std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
				&m_procPathTextLayout)))
			{
				DWRITE_TEXT_METRICS textMetrics;
				if (SUCCEEDED(m_procPathTextLayout->GetMetrics(&textMetrics)))
				{
					m_pathTextHeight = textMetrics.height;
				}
			}
			app().textFormat().clearMainEllipsisTrimming();
		}
		update();
	}

	void EnvDetail::onLaunchBtnClick()
	{
		if (!m_processList.hasEnv())
		{
			return;
		}
		if (m_strProcPath.size())
		{
			biz::launcher().run(m_processList.getEnv(), m_strProcPath);
			return;
		}

		const std::optional<std::wstring> fullPath = select_file(m_ownerWnd);
		if (!fullPath.has_value())
		{
			return;
		}
		setProcPath(fullPath.value());

		biz::launcher().run(m_processList.getEnv(), m_strProcPath);
	}
}
