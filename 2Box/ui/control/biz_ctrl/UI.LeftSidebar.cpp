module UI.LeftSidebar;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import MainApp;

namespace
{
	constexpr float PADDING = 24.f;
	constexpr float MARGIN_BOTTOM = 24.f;
	constexpr float START_BTN_HEIGHT = 36.f;
}

namespace ui
{
	void LeftSidebar::onResize(float width, float height)
	{
		m_startAppDiv->setBounds(D2D1::RectF(PADDING, PADDING, width - PADDING, PADDING + m_startAppDiv->getPathAreaHeight() + START_BTN_HEIGHT));

		const float startYPos = m_startAppDiv->getBounds().bottom + MARGIN_BOTTOM;
		// 视图切换按钮放在汇总行右侧（"环境 x 个，在线 x 个" 之后）
		constexpr float viewBtnWidth = 78.f;
		constexpr float viewBtnHeight = 20.f;
		m_btnViewMode->setBounds(D2D1::RectF(width - PADDING - viewBtnWidth, startYPos + 3.f,
		                                     width - PADDING, startYPos + 3.f + viewBtnHeight));

		// 汇总行与下方环境列表之间留出稍大的间距，避免视觉拥挤
		const float cardsTop = startYPos + MARGIN_BOTTOM + 8.f;
		m_envCardsArea->setBounds(D2D1::RectF(PADDING - EnvBoxCardArea::shadowSize,
		                                      cardsTop - EnvBoxCardArea::shadowSize + EnvBoxCardArea::shadowOffsetY,
		                                      width - PADDING + EnvBoxCardArea::scrollAreaWidth + EnvBoxCardArea::shadowSize,
		                                      height));
	}

	void LeftSidebar::initialize()
	{
		m_startAppDiv = std::make_unique<StartAppDiv>(this);
		m_startAppDiv->setBounds(D2D1::RectF(PADDING, PADDING, PADDING + 232.f, PADDING + 36.f));
		m_startAppDiv->setUpdateBounds([this]()
		{
			const auto [width, height] = size();
			onResize(width, height);
			update();
		});
		m_startAppDiv->setLaunchProcess([this](const std::wstring& procPath)
		{
			// 头部“启动新进程”：每次都新建独立环境，用于多开
			m_envCardsArea->launchProcessInNewEnv(procPath);
		});

		m_envCardsArea = std::make_unique<EnvBoxCardArea>(this);
		m_envCardsArea->setOnSummaryChange([this]
		{
			updateSummary();
			update();
		});
		updateSummary();

		// 视图切换按钮：卡片视图（默认） <-> 列表视图
		m_btnViewMode = std::make_unique<Button>(this);
		m_btnViewMode->setText(L"列表视图");
		m_btnViewMode->setBackgroundColor(D2D1::ColorF(0xf5f5f5));
		m_btnViewMode->setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnViewMode->setBorderColor(D2D1::ColorF(0xbdbdbd));
		m_btnViewMode->setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnViewMode->setTextColor(D2D1::ColorF(0x333333));
		m_btnViewMode->setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnViewMode->setOnClick([this]
		{
			const EViewMode nextMode = (m_envCardsArea->getViewMode() == EViewMode::Card) ? EViewMode::List : EViewMode::Card;
			m_envCardsArea->setViewMode(nextMode);
			m_btnViewMode->setText(nextMode == EViewMode::Card ? L"列表视图" : L"卡片视图");
			update();
		});
	}

	void LeftSidebar::updateSummary()
	{
		m_summaryText = std::format(L"环境：{}个，在线{}个",
		                            m_envCardsArea->getEnvCount(),
		                            m_envCardsArea->getOnlineEnvCount());
	}

	void LeftSidebar::drawImpl(const RenderContext& renderCtx)
	{
		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const auto drawSize = size();

		solidBrush->SetColor(D2D1::ColorF(0xFFFFFF));
		renderTarget->FillRectangle(D2D1::RectF(0.f, 0.f, drawSize.width, drawSize.height), solidBrush);

		solidBrush->SetColor(D2D1::ColorF(0xe0e0e0));
		renderTarget->DrawLine(D2D1::Point2F(drawSize.width, 0.f), D2D1::Point2F(drawSize.width, drawSize.height), solidBrush);

		m_startAppDiv->draw(renderCtx);

		const float startYPos = m_startAppDiv->getBounds().bottom + MARGIN_BOTTOM;
		solidBrush->SetColor(D2D1::ColorF(0xe0e0e0));
		renderTarget->DrawLine(D2D1::Point2F(PADDING, startYPos), D2D1::Point2F(drawSize.width - PADDING, startYPos), solidBrush);

		// 环境汇总：环境 x 个，在线 x 个（始终显示）
		solidBrush->SetColor(D2D1::ColorF(0x757575));
		const float summaryTop = startYPos + 6.f;
		constexpr float summaryHeight = 18.f;
		renderTarget->DrawTextW(m_summaryText.c_str(),
		                        static_cast<UINT32>(m_summaryText.length()),
		                        app().textFormat().pTipsFormat,
		                        D2D1::RectF(PADDING, summaryTop,
		                                    drawSize.width - PADDING - 86.f, summaryTop + summaryHeight),
		                        solidBrush);
		m_btnViewMode->draw(renderCtx);

		if (m_envCardsArea->isNoEnvs())
		{
			return;
		}

		m_envCardsArea->draw(renderCtx);
	}
}
