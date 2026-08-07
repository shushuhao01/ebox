module UI.RightContent;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import MainApp;

namespace
{
	constexpr float PADDING = 24.f;
	constexpr float MARGIN = 24.f;
	// 头部系统状态看板高度（含 CPU/内存/GPU/显存/磁盘 曲线卡片）
	constexpr float FEATURES_AREA_HEIGHT = 172.f;
	constexpr float BANNER_EXPANDED_HEIGHT = 40.f;
	constexpr float BANNER_COLLAPSED_HEIGHT = 24.f;
	constexpr float BANNER_BTN_WIDTH = 44.f;
	constexpr float BANNER_BTN_HEIGHT = 20.f;
	constexpr std::wstring_view BANNER_TEXT{
		L"提示：启用进程后建议改环境名称；下次如需再次启动对应进程，无需重新新建，直接启动该环境即可"
	};

	// 空态页面：用户须知 / 使用事项 卡片化排版
	constexpr float CARD_RADIUS = 10.f;
	constexpr float CARD_PADDING = 18.f;
	constexpr float CARD_GAP = 14.f;
	constexpr float CARD_TITLE_HEIGHT = 30.f;
	constexpr float TITLE_BULLET_RADIUS = 4.5f;
	constexpr float LIST_DOT_RADIUS = 3.5f;
	constexpr float ITEM_GAP = 10.f;
	constexpr float NOTICE_LINE_HEIGHT = 24.f;

	constexpr std::wstring_view NOTICE_TITLE{L"用户须知"};
	constexpr std::array<std::wstring_view, 3> NOTICE_LINES{
		L"本软件为免费软件，仅供个人非商业目的使用。",
		L"未经许可，任何单位或个人不得将其用于商业用途，否则须承担相应法律责任。",
		L"用户应确保使用行为符合所在国家法律法规，任何违法违规使用所产生的一切后果由用户自行承担。",
	};

	constexpr std::wstring_view TIPS_TITLE{L"使用事项"};
	constexpr std::array<std::wstring_view, 6> TIPS{
		L"首次启动需输入激活码授权使用；授权到期前可在右上角【授权】中查看到期时间并续期，到期后可正常使用界面，但无法再启动环境",
		L"点击【启动进程】选择需要运行的程序，或直接把文件【拖动】到本窗口，软件会自动新建或选择合适的环境运行",
		L".exe 后缀的可执行文件会自动选择一个没有重名进程的环境运行；.lnk 快捷方式或 .url 等其他关联了可执行程序的后缀，只会选择空环境或新建环境运行（也可自行指定环境）",
		L"本软件支持多开同一程序互不干扰：例如可同时登录多个 QYWX 等客户端，每个环境拥有独立的配置、缓存与聊天数据，方便多账号同时使用",
		L"本软件只能简单地在环境之间隔离，不会阻止环境内进程访问环境外资源，也不会阻止环境外进程感知环境内进程",
		L"环境中暂时只能查看所有正在运行的进程，实际还会将其中进程读写的一部分路径重定向，包括常见的配置、存档目录",
	};
}

namespace ui
{
	void RightContent::initialize()
	{
		// 使用事项：每条单独建文本布局，卡片内左对齐逐条绘制
		for (const std::wstring_view tip : TIPS)
		{
			UniqueComPtr<IDWriteTextLayout> layout;
			HRESULT hr = app().dWriteFactory()->CreateTextLayout(tip.data(),
			                                                     static_cast<UINT32>(tip.size()),
			                                                     app().textFormat().pMainFormat,
			                                                     std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
			                                                     &layout);
			if (FAILED(hr))
			{
				throw std::runtime_error(std::format("CreateTextLayout fail, HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)));
			}
			layout->SetWordWrapping(DWRITE_WORD_WRAPPING_CHARACTER);
			m_tipLayouts.push_back(std::move(layout));
		}

		m_btnBannerToggle = std::make_unique<Button>(this);
		m_btnBannerToggle->setText(L"收起");
		m_btnBannerToggle->setBackgroundColor(D2D1::ColorF(0xfff3e0));
		m_btnBannerToggle->setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnBannerToggle->setBorderColor(D2D1::ColorF(0xffb74d));
		m_btnBannerToggle->setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnBannerToggle->setTextColor(D2D1::ColorF(0x333333));
		m_btnBannerToggle->setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnBannerToggle->setOnClick([this]
		{
			m_bannerCollapsed = !m_bannerCollapsed;
			m_btnBannerToggle->setText(m_bannerCollapsed ? L"展开" : L"收起");
			const auto [width, height] = size();
			onResize(width, height);
			update();
		});

		m_btnBannerClose = std::make_unique<Button>(this);
		m_btnBannerClose->setText(L"×");
		m_btnBannerClose->setBackgroundColor(D2D1::ColorF(0xffebee));
		m_btnBannerClose->setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnBannerClose->setBorderColor(D2D1::ColorF(0xf44336));
		m_btnBannerClose->setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnBannerClose->setTextColor(D2D1::ColorF(0x333333));
		m_btnBannerClose->setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnBannerClose->setOnClick([this]
		{
			m_bannerVisible = false;
			const auto [width, height] = size();
			onResize(width, height);
			update();
		});

		// 使用事项卡片滚动条
		m_tipsScrollBar = std::make_unique<ScrollBar>(this);
		m_tipsScrollBar->setThumbPosChangeNotify([this] { update(); });
	}

	void RightContent::onResize(float width, float height)
	{
		m_featuresArea.setBounds(D2D1::RectF(PADDING, PADDING, width - PADDING, PADDING + FEATURES_AREA_HEIGHT));

		const float bannerTop = PADDING + FEATURES_AREA_HEIGHT + MARGIN;
		const float bannerHeight = m_bannerVisible ? (m_bannerCollapsed ? BANNER_COLLAPSED_HEIGHT : BANNER_EXPANDED_HEIGHT) : 0.f;
		m_bannerRect = D2D1::RectF(PADDING, bannerTop, width - PADDING, bannerTop + bannerHeight);
		if (m_bannerVisible)
		{
			const float btnY = bannerTop + (bannerHeight - BANNER_BTN_HEIGHT) * 0.5f;
			m_btnBannerClose->setBounds(D2D1::RectF(width - PADDING - BANNER_BTN_WIDTH, btnY,
			                                        width - PADDING, btnY + BANNER_BTN_HEIGHT));
			m_btnBannerToggle->setBounds(D2D1::RectF(width - PADDING - BANNER_BTN_WIDTH * 2 - 6.f, btnY,
			                                         width - PADDING - BANNER_BTN_WIDTH - 6.f, btnY + BANNER_BTN_HEIGHT));
		}

		m_contentTop = bannerTop + bannerHeight + (bannerHeight > 0.f ? MARGIN : 0.f);
		m_envDetail.setBounds(D2D1::RectF(PADDING, m_contentTop, width - PADDING, height - PADDING));

		// 空态：使用事项卡片区域（内容超出时滚动条挂载在卡片右侧）
		const float noticeTop = m_contentTop;
		const float noticeBodyTop = noticeTop + CARD_PADDING + CARD_TITLE_HEIGHT + 6.f;
		const float noticeBottom = noticeBodyTop + static_cast<float>(NOTICE_LINES.size()) * NOTICE_LINE_HEIGHT + CARD_PADDING;
		const float tipsTop = noticeBottom + CARD_GAP;
		const float tipsBottom = height - PADDING;
		constexpr float SCROLL_WIDTH = 8.f;
		m_tipsScrollBar->setBounds(D2D1::RectF(width - PADDING - SCROLL_WIDTH - 4.f, tipsTop + 10.f,
		                                       width - PADDING - 4.f, tipsBottom - 10.f));
	}

	void RightContent::onMouseWheel(const MouseWheelEvent& e)
	{
		// 空态下：使用事项内容超出可视区时支持滚轮滑动查看
		if (m_envDetail.hasDetail())
		{
			return; // 非空态：滚轮交由环境详情处理
		}
		e.accept = true;
		const float wheelCount = e.zDelta / 120.f;
		m_tipsScrollBar->scroll(-wheelCount * 24.f);
	}

	void RightContent::drawImpl(const RenderContext& renderCtx)
	{
		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const auto drawSize = size();

		solidBrush->SetColor(D2D1::ColorF(0xf8f9fa));
		renderTarget->FillRectangle(D2D1::RectF(0.f, 0.f, drawSize.width, drawSize.height), solidBrush);

		m_featuresArea.draw(renderCtx);

		// 公告栏：横向卡片，红字提示，支持收起/关闭
		if (m_bannerVisible)
		{
			const D2D1_ROUNDED_RECT bannerRounded = D2D1::RoundedRect(m_bannerRect, 8.f, 8.f);
			solidBrush->SetColor(D2D1::ColorF(0xfff3e0));
			renderTarget->FillRoundedRectangle(bannerRounded, solidBrush);
			solidBrush->SetColor(D2D1::ColorF(0xffcc80));
			renderTarget->DrawRoundedRectangle(bannerRounded, solidBrush);

			solidBrush->SetColor(D2D1::ColorF(0xd32f2f));
			const float bannerHeight = m_bannerRect.bottom - m_bannerRect.top;
			const float textTop = m_bannerRect.top + (bannerHeight - 18.f) * 0.5f;
			if (!m_bannerCollapsed)
			{
				renderTarget->DrawTextW(BANNER_TEXT.data(),
				                        static_cast<UINT32>(BANNER_TEXT.size()),
				                        app().textFormat().pTipsFormat,
				                        D2D1::RectF(m_bannerRect.left + 10.f, textTop,
				                                    m_bannerRect.right - BANNER_BTN_WIDTH * 2 - 22.f, textTop + 18.f),
				                        solidBrush);
			}
			else
			{
				renderTarget->DrawTextW(L"公告",
				                        2u,
				                        app().textFormat().pTipsFormat,
				                        D2D1::RectF(m_bannerRect.left + 10.f, textTop,
				                                    m_bannerRect.right - BANNER_BTN_WIDTH * 2 - 22.f, textTop + 18.f),
				                        solidBrush);
			}
			m_btnBannerToggle->draw(renderCtx);
			m_btnBannerClose->draw(renderCtx);
		}

		if (m_envDetail.hasDetail())
		{
			m_envDetail.draw(renderCtx);
		}
		else
		{
			const float cardLeft = PADDING;
			const float cardRight = drawSize.width - PADDING;
			const float cardWidth = cardRight - cardLeft;

			// ---- 用户须知卡片（浅蓝）----
			const float noticeTop = m_contentTop;
			const float noticeBodyTop = noticeTop + CARD_PADDING + CARD_TITLE_HEIGHT + 6.f;
			const float noticeBottom = noticeBodyTop + static_cast<float>(NOTICE_LINES.size()) * NOTICE_LINE_HEIGHT + CARD_PADDING;

			const D2D1_ROUNDED_RECT noticeCard = D2D1::RoundedRect(
				D2D1::RectF(cardLeft, noticeTop, cardRight, noticeBottom), CARD_RADIUS, CARD_RADIUS);
			solidBrush->SetColor(D2D1::ColorF(0xeaf3fc));
			renderTarget->FillRoundedRectangle(noticeCard, solidBrush);
			solidBrush->SetColor(D2D1::ColorF(0xb8d4f0));
			renderTarget->DrawRoundedRectangle(noticeCard, solidBrush);

			// 标题：蓝色圆点 + “用户须知”
			const float noticeTitleY = noticeTop + CARD_PADDING + (CARD_TITLE_HEIGHT - 20.f) * 0.5f;
			solidBrush->SetColor(D2D1::ColorF(0x1565c0));
			renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cardLeft + CARD_PADDING + 6.f, noticeTitleY + 10.f),
			                                        TITLE_BULLET_RADIUS, TITLE_BULLET_RADIUS), solidBrush);
			renderTarget->DrawTextW(NOTICE_TITLE.data(), static_cast<UINT32>(NOTICE_TITLE.size()),
			                        app().textFormat().pBoldFormat,
			                        D2D1::RectF(cardLeft + CARD_PADDING + 20.f, noticeTitleY,
			                                    cardRight - CARD_PADDING, noticeTitleY + 20.f), solidBrush);

			// 正文：左对齐、深灰
			solidBrush->SetColor(D2D1::ColorF(0x455a64));
			float noticeLineY = noticeBodyTop;
			for (const std::wstring_view line : NOTICE_LINES)
			{
				renderTarget->DrawTextW(line.data(), static_cast<UINT32>(line.size()),
				                        app().textFormat().pMainFormat,
				                        D2D1::RectF(cardLeft + CARD_PADDING, noticeLineY,
				                                    cardRight - CARD_PADDING, noticeLineY + NOTICE_LINE_HEIGHT), solidBrush);
				noticeLineY += NOTICE_LINE_HEIGHT;
			}

			// ---- 使用事项卡片（白色）----
			const float tipsTop = noticeBottom + CARD_GAP;
			const float tipsBottom = drawSize.height - PADDING;
			const float tipsTextWidth = cardWidth - CARD_PADDING * 2 - 22.f; // 预留列表圆点区

			const D2D1_ROUNDED_RECT tipsCard = D2D1::RoundedRect(
				D2D1::RectF(cardLeft, tipsTop, cardRight, tipsBottom), CARD_RADIUS, CARD_RADIUS);
			solidBrush->SetColor(D2D1::ColorF(0xffffff));
			renderTarget->FillRoundedRectangle(tipsCard, solidBrush);
			solidBrush->SetColor(D2D1::ColorF(0xe3e8ee));
			renderTarget->DrawRoundedRectangle(tipsCard, solidBrush);

			// 标题：绿色圆点 + “使用事项”
			const float tipsTitleY = tipsTop + CARD_PADDING + (CARD_TITLE_HEIGHT - 20.f) * 0.5f;
			solidBrush->SetColor(D2D1::ColorF(0x2e7d32));
			renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cardLeft + CARD_PADDING + 6.f, tipsTitleY + 10.f),
			                                        TITLE_BULLET_RADIUS, TITLE_BULLET_RADIUS), solidBrush);
			renderTarget->DrawTextW(TIPS_TITLE.data(), static_cast<UINT32>(TIPS_TITLE.size()),
			                        app().textFormat().pBoldFormat,
			                        D2D1::RectF(cardLeft + CARD_PADDING + 20.f, tipsTitleY,
			                                    cardRight - CARD_PADDING, tipsTitleY + 20.f), solidBrush);

			// 列表：绿色序号圆点 + 左对齐文本，逐条测量高度向下排布
			// 先测量全部条目的总高度（按卡片实际文字宽度换行），内容超出可视区时支持滚动查看
			float totalTipsHeight = 0.f;
			for (UniqueComPtr<IDWriteTextLayout>& layout : m_tipLayouts)
			{
				layout->SetMaxWidth(tipsTextWidth);
				layout->SetMaxHeight(std::numeric_limits<float>::max());
				DWRITE_TEXT_METRICS metrics{};
				if (FAILED(layout->GetMetrics(&metrics)))
				{
					continue;
				}
				totalTipsHeight += std::max(metrics.height, 20.f) + ITEM_GAP;
			}
			totalTipsHeight -= ITEM_GAP; // 最后一项的间隔不计

			const float dotX = cardLeft + CARD_PADDING + 6.f;
			const float textX = cardLeft + CARD_PADDING + 20.f;
			const float bodyTop = tipsTop + CARD_PADDING + CARD_TITLE_HEIGHT + 6.f;
			const float bodyBottom = tipsBottom - CARD_PADDING;

			m_tipsScrollBar->setVisibleSize(bodyBottom - bodyTop);
			m_tipsScrollBar->setTotalSize(totalTipsHeight);
			const float scrollOffset = m_tipsScrollBar->getScrollOffset();

			// 列表区域裁剪，内容按滚动偏移上移，溢出部分不可见（支持滑动继续查看）
			renderTarget->PushAxisAlignedClip(D2D1::RectF(cardLeft, bodyTop, cardRight, bodyBottom),
			                                  D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			float itemY = bodyTop - scrollOffset;
			for (std::size_t i = 0; i < m_tipLayouts.size() && i < TIPS.size(); ++i)
			{
				UniqueComPtr<IDWriteTextLayout>& layout = m_tipLayouts[i];
				DWRITE_TEXT_METRICS metrics{};
				if (FAILED(layout->GetMetrics(&metrics)))
				{
					break;
				}
				const float itemHeight = std::max(metrics.height, 20.f);
				if (itemY > bodyBottom)
				{
					break; // 已滚出可视区下方的条目无需绘制
				}
				solidBrush->SetColor(D2D1::ColorF(0x43a047));
				renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, itemY + itemHeight * 0.5f),
				                                        LIST_DOT_RADIUS, LIST_DOT_RADIUS), solidBrush);
				solidBrush->SetColor(D2D1::ColorF(0x37474f));
				renderTarget->DrawTextLayout(D2D1::Point2F(textX, itemY), layout, solidBrush);
				itemY += itemHeight + ITEM_GAP;
			}
			renderTarget->PopAxisAlignedClip();

			// 内容超出可视区时才显示滚动条
			if (totalTipsHeight > bodyBottom - bodyTop)
			{
				m_tipsScrollBar->draw(renderCtx);
			}
		}
	}
}
