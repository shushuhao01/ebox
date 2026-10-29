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
	constexpr float BANNER_MIN_HEIGHT = 40.f;
	constexpr float BANNER_TEXT_PADDING = 20.f;   // 标题上下留白（标题两行时自动增高）
	constexpr float BANNER_COLLAPSED_HEIGHT = 24.f;
	constexpr float BANNER_BTN_WIDTH = 44.f;
	constexpr float BANNER_BTN_HEIGHT = 20.f;
	constexpr float BANNER_BADGE_WIDTH = 34.f;    // 强提醒左侧“公告”胶囊宽度
	constexpr float BANNER_BADGE_GAP = 8.f;
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
	// 用户须知卡片正文右侧预留的滚动条/间隙宽度（与使用事项一致）
	constexpr float NOTICE_TEXT_GUTTER = 22.f;

	constexpr std::wstring_view NOTICE_TITLE{L"用户须知"};
	constexpr std::wstring_view AGREEMENT_NAME_UA{L"《eBox 用户协议》"};
	constexpr std::wstring_view AGREEMENT_NAME_PA{L"《eBox 隐私协议》"};
	constexpr std::size_t NOTICE_LINK_INDEX = 6; // 第7条：含《用户协议》《隐私协议》超链接
	constexpr std::array<std::wstring_view, 7> NOTICE_LINES{
		L"本软件为免费软件，仅供个人非商业目的使用。",
		L"未经许可，任何单位或个人不得将其用于商业用途，否则须承担相应法律责任。",
		L"本软件仅提供多实例隔离，不提供任何外挂功能；第三方多开微信、企业微信等可能存在封号风险，请遵守平台规则，风险自担。",
		L"本软件禁止用于任何违法犯罪活动，包括但不限于诈骗、传销、刷单、洗钱、非法集资、赌博、侵犯他人权益等。",
		L"因上述违法违规或第三方平台风控产生的一切后果，由使用者自行承担；开发者仅供学习交流使用，不承担任何责任。",
		L"对于明确不允许多开/多实例运行的平台、程序或应用，建议您遵守其规则与条款；因违反平台规则导致的账号受限、封禁等损失，属用户个人行为，与开发者无关，请自行评估风险。",
		L"继续使用本软件，即表示您已阅读并同意《eBox 用户协议》《eBox 隐私协议》，并同意本须知的全部内容。",
	};

	constexpr std::wstring_view TIPS_TITLE{L"使用事项"};
	constexpr std::array<std::wstring_view, 6> TIPS{
		L"首次启动需输入激活码授权使用；授权到期前可在右上角【授权】中查看到期时间并续期，到期后可正常使用界面，但无法再启动环境",
		L"点击左侧上方【启动新进程】按钮，选择需要运行的程序即可打开；每次启动会新建独立环境，便于多开",
		L".exe 后缀的可执行文件会自动选择一个没有重名进程的环境运行；.lnk 快捷方式或 .url 等其他关联了可执行程序的后缀，只会选择空环境或新建环境运行（也可自行指定环境）",
		L"本软件支持多开同一程序互不干扰：例如可同时登录多个 QYWX 等客户端，每个环境拥有独立的配置、缓存与聊天数据，方便多账号同时使用",
		L"本软件只能简单地在环境之间隔离，不会阻止环境内进程访问环境外资源，也不会阻止环境外进程感知环境内进程",
		L"环境中暂时只能查看所有正在运行的进程，实际还会将其中进程读写的一部分路径重定向，包括常见的配置、存档目录",
	};

	// 公告栏按钮配色：普通公告为浅色底深色字；强提醒深红底下改为白字 + 半透明白描边/悬停底色，保证反差清晰
	void applyBannerButtonStyle(ui::Button& btn, bool strong, const D2D1_COLOR_F& hoverBg, const D2D1_COLOR_F& hoverBorder)
	{
		if (strong)
		{
			btn.setTextColor(D2D1::ColorF(0xffffff));
			btn.setBackgroundColor(D2D1::ColorF(0xffffff, 0.f), ui::Button::EState::Normal);
			btn.setBackgroundColor(D2D1::ColorF(0xffffff, 0.18f), ui::Button::EState::Hover);
			btn.setBackgroundColor(D2D1::ColorF(0xffffff, 0.30f), ui::Button::EState::Active);
			btn.setBorderColor(D2D1::ColorF(0xffffff, 0.55f));
			return;
		}
		btn.setTextColor(D2D1::ColorF(0x333333));
		btn.setBackgroundColor(D2D1::ColorF(0x000000, 0.f), ui::Button::EState::Normal);
		btn.setBackgroundColor(hoverBg, ui::Button::EState::Hover);
		btn.setBackgroundColor(hoverBg, ui::Button::EState::Active);
		btn.setBorderColor(D2D1::ColorF(0xe0e0e0));
		btn.setBorderColor(hoverBorder, ui::Button::EState::Hover);
		btn.setBorderColor(hoverBorder, ui::Button::EState::Active);
	}
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

		// 用户须知：每条单独建文本布局（按换行后实际高度向下排布，避免固定行高导致的文字重叠）；
		// 第6条为《用户协议》《隐私协议》名加下划线，标明可点击。
		for (std::size_t i = 0; i < NOTICE_LINES.size(); ++i)
		{
			const std::wstring_view line = NOTICE_LINES[i];
			UniqueComPtr<IDWriteTextLayout> layout;
			HRESULT hr = app().dWriteFactory()->CreateTextLayout(line.data(),
			                                                     static_cast<UINT32>(line.size()),
			                                                     app().textFormat().pMainFormat,
			                                                     std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
			                                                     &layout);
			if (FAILED(hr))
			{
				throw std::runtime_error(std::format("CreateTextLayout fail, HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)));
			}
			layout->SetWordWrapping(DWRITE_WORD_WRAPPING_CHARACTER);
			if (i == NOTICE_LINK_INDEX)
			{
				const auto uaPos = line.find(AGREEMENT_NAME_UA);
				const auto paPos = line.find(AGREEMENT_NAME_PA);
				if (uaPos != std::wstring_view::npos)
				{
					m_noticeUARange = { static_cast<UINT32>(uaPos), static_cast<UINT32>(AGREEMENT_NAME_UA.size()) };
				}
				if (paPos != std::wstring_view::npos)
				{
					m_noticePARange = { static_cast<UINT32>(paPos), static_cast<UINT32>(AGREEMENT_NAME_PA.size()) };
				}
				layout->SetUnderline(TRUE, m_noticeUARange);
				layout->SetUnderline(TRUE, m_noticePARange);
			}
			m_noticeLayouts.push_back(std::move(layout));
		}

		m_btnBannerToggle = std::make_unique<Button>(this);
		m_btnBannerToggle->setText(L"收起");
		applyBannerButtonStyle(*m_btnBannerToggle, m_notice.strong,
		                       D2D1::ColorF(0xfff3e0), D2D1::ColorF(0xffb74d));
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
		applyBannerButtonStyle(*m_btnBannerClose, m_notice.strong,
		                       D2D1::ColorF(0xffebee), D2D1::ColorF(0xf44336));
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

		// 用户须知卡片滚动条（内容超出可视区时支持滑动查看，与使用事项一致）
		m_noticesScrollBar = std::make_unique<ScrollBar>(this);
		m_noticesScrollBar->setThumbPosChangeNotify([this] { update(); });

		// 启动时读取已持久化的服务端系统公告（最新一条），无公告则显示本地使用提示
		refreshNotice();
	}

	void RightContent::refreshNotice()
	{
		biz::licenseserver::NoticeInfo notice = biz::licenseserver::currentNotice();
		if (notice == m_notice)
		{
			return;
		}
		m_notice = std::move(notice);
		// 强提醒会切换公告栏底色，按钮需同步换成反差配色
		if (m_btnBannerToggle && m_btnBannerClose)
		{
			applyBannerButtonStyle(*m_btnBannerToggle, m_notice.strong,
			                       D2D1::ColorF(0xfff3e0), D2D1::ColorF(0xffb74d));
			applyBannerButtonStyle(*m_btnBannerClose, m_notice.strong,
			                       D2D1::ColorF(0xffebee), D2D1::ColorF(0xf44336));
		}
		// 新公告（含强提醒）到达时自动展开，保证用户能看到最新内容
		if (!m_notice.empty())
		{
			m_bannerVisible = true;
			m_bannerCollapsed = false;
			m_btnBannerToggle->setText(L"收起");
		}
		const auto [width, height] = size();
		if (width > 0.f)
		{
			onResize(width, height);
		}
		else
		{
			rebuildBannerLayout(0.f);
		}
		update();
	}

	void RightContent::rebuildBannerLayout(float bannerWidth)
	{
		m_bannerLayout.reset();
		m_bannerTextWidth = 0.f;
		m_bannerTextHeight = 0.f;
		m_bannerExpandedHeight = BANNER_MIN_HEIGHT;
		if (bannerWidth <= 0.f)
		{
			return; // 尚未布局（首次构造时），等 onResize 再重建
		}
		// 标题优先；仅配置内容时以内容作为标题展示
		std::wstring text = m_notice.title;
		if (text.empty())
		{
			text = m_notice.content;
		}
		if (text.empty() && !m_notice.link.empty())
		{
			text = L"点击查看详情"; // 仅配置链接：公告栏本身即跳转入口
		}
		if (text.empty())
		{
			return; // 无公告文本：绘制时回退本地提示
		}
		const float badgeWidth = m_notice.strong ? BANNER_BADGE_WIDTH + BANNER_BADGE_GAP : 0.f;
		m_bannerTextWidth = bannerWidth - BANNER_BTN_WIDTH * 2.f - 22.f - 10.f - badgeWidth;
		if (m_bannerTextWidth <= 0.f)
		{
			return;
		}
		const bool strong = m_notice.strong;
		UniqueComPtr<IDWriteTextLayout> layout;
		HRESULT hr = app().dWriteFactory()->CreateTextLayout(
			text.c_str(),
			static_cast<UINT32>(text.size()),
			strong ? app().textFormat().pBoldFormat : app().textFormat().pTipsFormat,
			m_bannerTextWidth, std::numeric_limits<float>::max(), &layout);
		if (FAILED(hr))
		{
			return;
		}
		// 逐字换行：先按自然高度求行高，再限制为最多两行，超出部分用省略号收尾
		layout->SetWordWrapping(DWRITE_WORD_WRAPPING_CHARACTER);
		DWRITE_TEXT_METRICS natural{};
		if (FAILED(layout->GetMetrics(&natural)))
		{
			return;
		}
		const float lineHeight = natural.lineCount > 0 ? natural.height / static_cast<float>(natural.lineCount) : natural.height;
		const float maxHeight = lineHeight * 2.f;
		layout->SetMaxHeight(maxHeight);
		IDWriteInlineObject* ellipsis = nullptr;
		if (SUCCEEDED(app().dWriteFactory()->CreateEllipsisTrimmingSign(layout.get(), &ellipsis)))
		{
			DWRITE_TRIMMING trimming{};
			trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
			layout->SetTrimming(&trimming, ellipsis);
			ellipsis->Release();
		}
		DWRITE_TEXT_METRICS fitted{};
		m_bannerTextHeight = SUCCEEDED(layout->GetMetrics(&fitted)) ? fitted.height : maxHeight;
		m_bannerLayout = std::move(layout);
		m_bannerExpandedHeight = std::max(BANNER_MIN_HEIGHT, m_bannerTextHeight + BANNER_TEXT_PADDING);
	}

	void RightContent::onResize(float width, float height)
	{
		m_featuresArea.setBounds(D2D1::RectF(PADDING, PADDING, width - PADDING, PADDING + FEATURES_AREA_HEIGHT));

		const float bannerTop = PADDING + FEATURES_AREA_HEIGHT + MARGIN;
		// 先按新宽度重建标题布局（最多两行自适应），得到展开态高度再定位公告栏
		rebuildBannerLayout(width - PADDING * 2.f);
		const float bannerHeight = m_bannerVisible ? (m_bannerCollapsed ? BANNER_COLLAPSED_HEIGHT : m_bannerExpandedHeight) : 0.f;
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

		// 空态：用户须知卡片（按内容实际高度测高排布，内容超出可视区时卡片内可滚动）
		const float cardLeft = PADDING;
		const float cardRight = width - PADDING;
		const float noticeTop = m_contentTop;
		const float noticeTextWidth = (cardRight - cardLeft) - CARD_PADDING * 2.f - NOTICE_TEXT_GUTTER;

		// 先测量用户须知全部条目的实际总高度（按卡片文字宽度换行），避免固定行高导致的文字重叠
		float totalNoticeHeight = 0.f;
		for (UniqueComPtr<IDWriteTextLayout>& layout : m_noticeLayouts)
		{
			layout->SetMaxWidth(noticeTextWidth);
			layout->SetMaxHeight(std::numeric_limits<float>::max());
			DWRITE_TEXT_METRICS metrics{};
			if (FAILED(layout->GetMetrics(&metrics)))
			{
				continue;
			}
			totalNoticeHeight += std::max(metrics.height, 20.f) + ITEM_GAP;
		}
		totalNoticeHeight -= ITEM_GAP; // 最后一项的间隔不计
		m_noticeTotalHeight = totalNoticeHeight;

		// 两个卡片均分可用空间；用户须知按内容高度，但不超过一半，超出部分在卡片内滚动
		const float availBottom = height - PADDING;
		const float availArea = availBottom - noticeTop - CARD_GAP;
		const float noticeNeeded = CARD_PADDING * 2.f + CARD_TITLE_HEIGHT + 6.f + totalNoticeHeight;
		const float maxNoticeCardHeight = std::max(120.f, availArea * 0.5f);
		const float noticeCardHeight = std::min(noticeNeeded, maxNoticeCardHeight);
		const float noticeBottom = noticeTop + noticeCardHeight;

		const float noticeBodyTop = noticeTop + CARD_PADDING + CARD_TITLE_HEIGHT + 6.f;
		const float noticeBodyBottom = noticeBottom - CARD_PADDING;
		m_noticeBodyRect = D2D1::RectF(cardLeft + CARD_PADDING, noticeBodyTop,
		                               cardRight - CARD_PADDING - NOTICE_TEXT_GUTTER, noticeBodyBottom);
		m_noticeBodyHeight = noticeBodyBottom - noticeBodyTop;

		constexpr float SCROLL_WIDTH = 8.f;
		m_noticesScrollBar->setBounds(D2D1::RectF(width - PADDING - SCROLL_WIDTH - 4.f, noticeBodyTop + 6.f,
		                                          width - PADDING - 4.f, noticeBodyBottom - 6.f));
		m_noticesScrollBar->setVisibleSize(m_noticeBodyHeight);
		m_noticesScrollBar->setTotalSize(m_noticeTotalHeight);

		// 空态：使用事项卡片区域（内容超出时滚动条挂载在卡片右侧）
		const float tipsTop = noticeBottom + CARD_GAP;
		const float tipsBottom = availBottom;
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
		// 将 owner 坐标转换为控件局部坐标，判断滚轮悬停在哪个卡片上，从而滚动对应卡片
		const D2D1_RECT_F& bo = getBoundsInOwner();
		const D2D1_POINT_2F localPt{ e.point.x - bo.left, e.point.y - bo.top };
		const auto [ctrlW, ctrlH] = size();
		if (localPt.y >= m_noticeBodyRect.top && localPt.y <= m_noticeBodyRect.bottom &&
		    localPt.x >= PADDING && localPt.x <= ctrlW - PADDING)
		{
			m_noticesScrollBar->scroll(-wheelCount * 24.f);
		}
		else
		{
			m_tipsScrollBar->scroll(-wheelCount * 24.f);
		}
	}

	void RightContent::drawImpl(const RenderContext& renderCtx)
	{
		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const auto drawSize = size();

		solidBrush->SetColor(D2D1::ColorF(0xf8f9fa));
		renderTarget->FillRectangle(D2D1::RectF(0.f, 0.f, drawSize.width, drawSize.height), solidBrush);

		m_featuresArea.draw(renderCtx);

		// 公告栏：横向卡片（强提醒为深红底 + 白字加粗），支持收起/关闭
		if (m_bannerVisible)
		{
			const bool strong = m_notice.strong && !m_notice.empty();
			const D2D1_ROUNDED_RECT bannerRounded = D2D1::RoundedRect(m_bannerRect, 8.f, 8.f);
			solidBrush->SetColor(D2D1::ColorF(strong ? 0x8e1b12 : 0xfff3e0));
			renderTarget->FillRoundedRectangle(bannerRounded, solidBrush);
			solidBrush->SetColor(D2D1::ColorF(strong ? 0x6d150e : 0xffcc80));
			renderTarget->DrawRoundedRectangle(bannerRounded, solidBrush);

			const float bannerHeight = m_bannerRect.bottom - m_bannerRect.top;
			if (!m_bannerCollapsed)
			{
				float textX = m_bannerRect.left + 10.f;
				if (strong)
				{
					// 左侧“公告”胶囊（白底深红字），强化视觉反差
					const float badgeTop = m_bannerRect.top + (bannerHeight - 20.f) * 0.5f;
					const D2D1_ROUNDED_RECT badge = D2D1::RoundedRect(
						D2D1::RectF(textX, badgeTop, textX + BANNER_BADGE_WIDTH, badgeTop + 20.f), 10.f, 10.f);
					solidBrush->SetColor(D2D1::ColorF(0xffffff));
					renderTarget->FillRoundedRectangle(badge, solidBrush);
					solidBrush->SetColor(D2D1::ColorF(0x8e1b12));
					renderTarget->DrawTextW(L"公告", 2u, app().textFormat().pTipsFormat,
					                        D2D1::RectF(textX + 5.f, badgeTop + 2.f, textX + BANNER_BADGE_WIDTH, badgeTop + 20.f),
					                        solidBrush);
					textX += BANNER_BADGE_WIDTH + BANNER_BADGE_GAP;
				}
				if (m_bannerLayout)
				{
					// 服务端系统公告：标题最多两行，整体纵向居中
					const float textTop = m_bannerRect.top + (bannerHeight - m_bannerTextHeight) * 0.5f;
					solidBrush->SetColor(D2D1::ColorF(strong ? 0xffffff : 0xd32f2f));
					renderTarget->DrawTextLayout(D2D1::Point2F(textX, textTop), m_bannerLayout, solidBrush);
				}
				else
				{
					// 无服务端公告：回退本地使用提示
					const float textTop = m_bannerRect.top + (bannerHeight - 18.f) * 0.5f;
					const float textWidth = m_bannerRect.right - textX - BANNER_BTN_WIDTH * 2 - 22.f;
					solidBrush->SetColor(D2D1::ColorF(0xd32f2f));
					renderTarget->DrawTextW(BANNER_TEXT.data(),
					                        static_cast<UINT32>(BANNER_TEXT.size()),
					                        app().textFormat().pTipsFormat,
					                        D2D1::RectF(textX, textTop, textX + textWidth, textTop + 18.f),
					                        solidBrush);
				}
			}
			else
			{
				const float textTop = m_bannerRect.top + (bannerHeight - 18.f) * 0.5f;
				solidBrush->SetColor(D2D1::ColorF(strong ? 0xffffff : 0xd32f2f));
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
			const float noticeBottom = m_noticeBodyRect.bottom + CARD_PADDING;

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

			// 正文：按每条实际换行高度向下排布（避免固定行高导致文字重叠），内容超出可视区时支持滚动
			const float noticeBodyTop = m_noticeBodyRect.top;
			const float noticeBodyBottom = m_noticeBodyRect.bottom;
			const float noticeTextX = m_noticeBodyRect.left;
			const float noticeScrollOffset = m_noticesScrollBar->getScrollOffset();
			renderTarget->PushAxisAlignedClip(D2D1::RectF(cardLeft, noticeBodyTop, cardRight, noticeBodyBottom),
			                                  D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
			float noticeLineY = noticeBodyTop - noticeScrollOffset;
			for (UniqueComPtr<IDWriteTextLayout>& layout : m_noticeLayouts)
			{
				layout->SetMaxWidth(m_noticeBodyRect.right - m_noticeBodyRect.left);
				DWRITE_TEXT_METRICS metrics{};
				if (FAILED(layout->GetMetrics(&metrics)))
				{
					break;
				}
				const float itemHeight = std::max(metrics.height, 20.f);
				if (noticeLineY > noticeBodyBottom)
				{
					break; // 已滚出可视区下方的条目无需绘制
				}
				solidBrush->SetColor(D2D1::ColorF(0x455a64));
				renderTarget->DrawTextLayout(D2D1::Point2F(noticeTextX, noticeLineY), layout, solidBrush);
				noticeLineY += itemHeight + ITEM_GAP;
			}
			renderTarget->PopAxisAlignedClip();

			// 内容超出可视区时才显示滚动条
			if (m_noticeTotalHeight > noticeBodyBottom - noticeBodyTop)
			{
				m_noticesScrollBar->draw(renderCtx);
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

	void RightContent::onClick(const MouseEvent& e)
	{
		if (e.button != MouseEvent::ButtonType::Left)
		{
			return;
		}
		// e.point 是 owner 窗口坐标系，转换为控件局部坐标
		const D2D1_RECT_F& bo = getBoundsInOwner();
		const D2D1_POINT_2F localPt{ e.point.x - bo.left, e.point.y - bo.top };
		// 公告栏优先：收起态点击展开；有详情弹窗；无详情有链接直接跳转
		if (m_bannerVisible &&
		    localPt.x >= m_bannerRect.left && localPt.x <= m_bannerRect.right &&
		    localPt.y >= m_bannerRect.top && localPt.y <= m_bannerRect.bottom)
		{
			if (m_bannerCollapsed)
			{
				m_bannerCollapsed = false;
				m_btnBannerToggle->setText(L"收起");
				const auto [ctrlW, ctrlH] = size();
				onResize(ctrlW, ctrlH);
				update();
				return;
			}
			if (!m_notice.empty())
			{
				openNoticeDetail();
			}
			return;
		}
		if (m_envDetail.hasDetail())
		{
			return; // 非空态：点击交由环境详情处理
		}
		openAgreementDialogFromNotice(localPt);
	}

	void RightContent::openNoticeDetail()
	{
		if (m_notice.content.empty())
		{
			// 没有正文：有链接直接跳转浏览器，否则不做处理
			if (!m_notice.link.empty())
			{
				ui::open_external_url(m_notice.link);
			}
			return;
		}
		ui::show_notice_dialog(owner() ? owner()->nativeHandle() : nullptr,
		                       m_notice.title, m_notice.content, m_notice.link, m_notice.strong);
		// 模态弹窗关闭后重绘，避免窗口残留旧内容
		updateWholeWnd();
	}

	void RightContent::openAgreementDialogFromNotice(D2D1_POINT_2F localPt)
	{
		if (m_noticeLayouts.empty() || m_noticeBodyRect.right <= m_noticeBodyRect.left)
		{
			return;
		}
		const float textX = m_noticeBodyRect.left;
		const float bodyTop = m_noticeBodyRect.top;
		const float bodyBottom = m_noticeBodyRect.bottom;
		if (localPt.y < bodyTop || localPt.y > bodyBottom ||
		    localPt.x < textX || localPt.x > m_noticeBodyRect.right)
		{
			return; // 点击非用户须知正文区域
		}

		const float scrollOffset = m_noticesScrollBar->getScrollOffset();
		float itemY = bodyTop - scrollOffset;
		for (std::size_t i = 0; i < m_noticeLayouts.size(); ++i)
		{
			UniqueComPtr<IDWriteTextLayout>& layout = m_noticeLayouts[i];
			layout->SetMaxWidth(m_noticeBodyRect.right - m_noticeBodyRect.left);
			DWRITE_TEXT_METRICS metrics{};
			if (FAILED(layout->GetMetrics(&metrics)))
			{
				break;
			}
			const float itemHeight = std::max(metrics.height, 20.f);
			if (localPt.y < itemY || localPt.y > itemY + itemHeight)
			{
				itemY += itemHeight + ITEM_GAP;
				continue;
			}
			if (i != NOTICE_LINK_INDEX)
			{
				return; // 命中非链接条目，不做处理
			}
			// 命中第6条：将点击点换算到该条目文本布局的局部坐标，再通过命中测试判断落在哪个协议名上
			const D2D1_POINT_2F layoutPt{ localPt.x - textX, localPt.y - itemY };
			DWRITE_HIT_TEST_METRICS hit{};
			BOOL isTrailing = FALSE;
			BOOL isInside = FALSE;
			if (FAILED(layout->HitTestPoint(layoutPt.x, layoutPt.y, &isTrailing, &isInside, &hit)))
			{
				return;
			}
			const UINT32 pos = hit.textPosition;
			const bool inUA = (m_noticeUARange.length > 0 &&
			                   pos >= m_noticeUARange.startPosition &&
			                   pos < m_noticeUARange.startPosition + m_noticeUARange.length);
			const bool inPA = (m_noticePARange.length > 0 &&
			                   pos >= m_noticePARange.startPosition &&
			                   pos < m_noticePARange.startPosition + m_noticePARange.length);
			if (inUA)
			{
				ui::show_user_agreement_dialog(owner()->nativeHandle());
			}
			else if (inPA)
			{
				ui::show_privacy_agreement_dialog(owner()->nativeHandle());
			}
			return;
		}
	}
}
