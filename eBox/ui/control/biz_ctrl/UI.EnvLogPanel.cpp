module UI.EnvLogPanel;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import MainApp;

namespace
{
	constexpr float HEADER_HEIGHT = 28.f;
	constexpr float PADDING = 8.f;
	constexpr float BTN_WIDTH = 52.f;
	constexpr float BTN_HEIGHT = 20.f;
	constexpr float BTN_GAP = 6.f;
	constexpr float LOG_LINE_HEIGHT = 20.f;
	constexpr float SCROLL_WIDTH = 8.f;

	// 记录时间放在最前面（HH:MM:SS），供日志行展示
	std::wstring format_time(std::int64_t unixSeconds)
	{
		// 转成 Windows FILETIME（1601-01-01 起 100ns），再转本地时间，避免 CRT time 函数
		ULONGLONG fileTime100ns = static_cast<ULONGLONG>(unixSeconds) * 10'000'000ULL + 116'444'736'000'000'000ULL;
		FILETIME ftUtc{};
		ftUtc.dwLowDateTime = static_cast<DWORD>(fileTime100ns & 0xFFFFFFFFULL);
		ftUtc.dwHighDateTime = static_cast<DWORD>(fileTime100ns >> 32);
		FILETIME ftLocal{};
		FileTimeToLocalFileTime(&ftUtc, &ftLocal);
		SYSTEMTIME st{};
		FileTimeToSystemTime(&ftLocal, &st);
		return std::format(L"{:02}:{:02}:{:02}", st.wHour, st.wMinute, st.wSecond);
	}

	std::wstring format_log_line(const biz::EnvLogEntry& entry)
	{
		using namespace std::string_view_literals;
		std::wstring_view typeStr = L"信息"sv;
		std::wstring_view statusStr = L""sv;
		switch (entry.type)
		{
			case biz::EnvLogType::Info:
				typeStr = L"环境"sv;
				break;
			case biz::EnvLogType::Process:
				typeStr = L"进程"sv;
				break;
			case biz::EnvLogType::Login:
				typeStr = L"登录"sv;
				break;
			case biz::EnvLogType::Message:
				typeStr = L"提示"sv;
				break;
			case biz::EnvLogType::Error:
				typeStr = L"错误"sv;
				break;
		}
		switch (entry.status)
		{
			case biz::EnvLogStatus::Success:
				statusStr = L"成功"sv;
				break;
			case biz::EnvLogStatus::Fail:
				statusStr = L"失败"sv;
				break;
			case biz::EnvLogStatus::Info:
				statusStr = L"信息"sv;
				break;
		}
		// 时间在最前面，随后是动作、类型、状态、描述
		return std::format(L"[{}] {} {} {} {}", format_time(entry.timestamp), entry.action, typeStr, statusStr, entry.detail);
	}
}

namespace ui
{
	void EnvLogPanel::initialize()
	{
		m_btnToggle.setText(L"查看");
		m_btnToggle.setBackgroundColor(D2D1::ColorF(0xe3f2fd));
		m_btnToggle.setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnToggle.setBorderColor(D2D1::ColorF(0x0078d4));
		m_btnToggle.setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnToggle.setTextColor(D2D1::ColorF(0x0078d4));
		m_btnToggle.setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnToggle.setOnClick([this] { toggleExpand(); });

		m_btnCopy.setText(L"复制");
		m_btnCopy.setBackgroundColor(D2D1::ColorF(0xe8f5e9));
		m_btnCopy.setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnCopy.setBorderColor(D2D1::ColorF(0x4caf50));
		m_btnCopy.setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnCopy.setTextColor(D2D1::ColorF(0x2e7d32));
		m_btnCopy.setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnCopy.setOnClick([this] { copyAllLogs(); });

		// 清理按钮：清空该环境缓存的日志（内存 + 磁盘文件），放在复制按钮之后（最右）
		m_btnClear.setText(L"清理");
		m_btnClear.setBackgroundColor(D2D1::ColorF(0xfdecec));
		m_btnClear.setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnClear.setBorderColor(D2D1::ColorF(0xe57373));
		m_btnClear.setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnClear.setTextColor(D2D1::ColorF(0xc62828));
		m_btnClear.setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnClear.setOnClick([this] { clearLogs(); });
	}

	void EnvLogPanel::setEnvIndex(std::uint32_t envIndex)
	{
		m_envIndex = envIndex;
		m_hasEnv = true;
		refreshLogs();
		update();
	}

	void EnvLogPanel::clearEnv()
	{
		m_hasEnv = false;
		m_logs.clear();
		m_logLines.clear();
		m_scrollBar.setTotalSize(0.f);
		update();
	}

	void EnvLogPanel::setExpanded(bool expanded)
	{
		if (m_expanded == expanded)
		{
			return;
		}
		m_expanded = expanded;
		m_btnToggle.setText(m_expanded ? L"收起" : L"查看");
		refreshLogs();
		// 展开时回到顶部（最新日志在最上面），方便直接浏览
		if (m_expanded)
		{
			m_scrollBar.scroll(-1000000.f);
		}
		onResize(size().width, size().height);
		update();
	}

	void EnvLogPanel::toggleExpand()
	{
		// 有外部协调者（手风琴）时，把展开请求交给外部统一互斥管理
		if (onExpandRequest)
		{
			onExpandRequest(!m_expanded);
			return;
		}
		setExpanded(!m_expanded);
	}

	void EnvLogPanel::setMaxExpandHeight(float maxHeight)
	{
		m_maxExpandHeight = std::max(60.f, maxHeight);
	}

	void EnvLogPanel::refreshLogs()
	{
		if (!m_hasEnv)
		{
			return;
		}
		m_logs = biz::env_logger().getRecentLogs(m_envIndex);
		// 最新日志在最上面
		std::reverse(m_logs.begin(), m_logs.end());
		m_logLines.clear();
		m_logLines.reserve(m_logs.size());
		for (const biz::EnvLogEntry& entry : m_logs)
		{
			m_logLines.push_back(format_log_line(entry));
		}
		if (m_expanded)
		{
			m_scrollBar.setTotalSize(m_logLines.size() * LOG_LINE_HEIGHT);
		}
		else
		{
			m_scrollBar.setTotalSize(0.f);
		}
	}

	void EnvLogPanel::copyAllLogs()
	{
		refreshLogs();
		if (m_logLines.empty())
		{
			return;
		}
		// 弹出选项：复制最近 10 条 / 最近 50 条 / 全部
		HMENU hMenu = CreatePopupMenu();
		AppendMenuW(hMenu, MF_STRING, 1, L"复制最近 10 条");
		AppendMenuW(hMenu, MF_STRING, 2, L"复制最近 50 条");
		AppendMenuW(hMenu, MF_STRING, 3, L"复制全部（当前缓存）");
		POINT pt{};
		GetCursorPos(&pt);
		SetForegroundWindow(m_ownerWnd->nativeHandle());
		const UINT id = TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_NONOTIFY | TPM_RETURNCMD,
		                                 pt.x, pt.y, m_ownerWnd->nativeHandle(), nullptr);
		DestroyMenu(hMenu);

		std::size_t count = m_logLines.size();
		if (id == 1)
		{
			count = std::min<std::size_t>(10, m_logLines.size());
		}
		else if (id == 2)
		{
			count = std::min<std::size_t>(50, m_logLines.size());
		}
		else if (id == 0 || id > 3)
		{
			return;
		}
		// m_logLines 已按“最新在最上”排列，取前 count 条即为最新日志
		std::wstring text;
		text.reserve(count * 64);
		for (std::size_t i = 0; i < count; ++i)
		{
			text.append(m_logLines[i]);
			text.push_back(L'\n');
		}
		if (OpenClipboard(nullptr))
		{
			EmptyClipboard();
			const std::size_t byteSize = (text.size() + 1) * sizeof(wchar_t);
			if (HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, byteSize))
			{
				if (void* pMem = GlobalLock(hMem))
				{
					memcpy(pMem, text.data(), byteSize);
					GlobalUnlock(hMem);
					SetClipboardData(CF_UNICODETEXT, hMem);
				}
				else
				{
					GlobalFree(hMem);
				}
			}
			CloseClipboard();
		}
	}

	void EnvLogPanel::clearLogs()
	{
		if (!m_hasEnv)
		{
			return;
		}
		// 清理该环境的全部日志（内存缓存 + 磁盘文件）
		biz::env_logger().clear(m_envIndex);
		refreshLogs();
		update();
	}

	float EnvLogPanel::getHeaderHeight() const
	{
		return HEADER_HEIGHT;
	}

	float EnvLogPanel::getLogAreaTop() const
	{
		return HEADER_HEIGHT;
	}

	float EnvLogPanel::getLogLineHeight() const
	{
		return LOG_LINE_HEIGHT;
	}

	float EnvLogPanel::getDesiredHeight() const
	{
		return m_expanded ? HEADER_HEIGHT + std::min(m_maxExpandHeight, static_cast<float>(m_logLines.size() * LOG_LINE_HEIGHT)) + PADDING
		                  : HEADER_HEIGHT + PADDING;
	}

	void EnvLogPanel::onResize(float width, float height)
	{
		const float btnY = (HEADER_HEIGHT - BTN_HEIGHT) * 0.5f;
		// 三个按钮从右到左：清理 / 复制 / 查看
		const float clearLeft = width - PADDING - BTN_WIDTH;
		const float copyLeft = clearLeft - BTN_GAP - BTN_WIDTH;
		const float toggleLeft = copyLeft - BTN_GAP - BTN_WIDTH;
		m_btnToggle.setBounds(D2D1::RectF(toggleLeft, btnY, toggleLeft + BTN_WIDTH, btnY + BTN_HEIGHT));
		m_btnCopy.setBounds(D2D1::RectF(copyLeft, btnY, copyLeft + BTN_WIDTH, btnY + BTN_HEIGHT));
		m_btnClear.setBounds(D2D1::RectF(clearLeft, btnY, clearLeft + BTN_WIDTH, btnY + BTN_HEIGHT));
		const float logTop = getLogAreaTop();
		const float logHeight = std::max(0.f, height - logTop - PADDING);
		m_scrollBar.setBounds(D2D1::RectF(width - PADDING - SCROLL_WIDTH, logTop,
		                                  width - PADDING, logTop + logHeight));
		m_scrollBar.setVisibleSize(logHeight);
		m_scrollBar.setTotalSize(m_expanded ? m_logLines.size() * LOG_LINE_HEIGHT : 0.f);
	}

	void EnvLogPanel::onMouseEnter(const MouseEvent& e)
	{
		m_isHovered = true;
		update();
	}

	void EnvLogPanel::onMouseLeave(const MouseEvent& e)
	{
		if (!hitTest(e.point))
		{
			m_isHovered = false;
			update();
		}
	}

	void EnvLogPanel::onMouseWheel(const MouseWheelEvent& e)
	{
		e.accept = true;
		if (!m_expanded)
		{
			return;
		}
		const float wheelCount = e.zDelta / 120.f;
		m_scrollBar.scroll(-wheelCount * 24.f);
		update();
	}

	void EnvLogPanel::onClick(const MouseEvent& e)
	{
		// 点击标题栏空白区域（非按钮）也可展开/收起
		if (e.point.y >= m_boundsInOwner.top && e.point.y <= m_boundsInOwner.top + getHeaderHeight())
		{
			toggleExpand();
		}
	}

	void EnvLogPanel::drawImpl(const RenderContext& renderCtx)
	{
		// 每次绘制前刷新日志，保证新动作实时出现
		refreshLogs();

		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const auto [width, height] = size();

		// 卡片底色（轻微不同以区分于进程列表）
		const D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(D2D1::RectF(0.f, 0.f, width, height), 6.f, 6.f);
		solidBrush->SetColor(D2D1::ColorF(0xf7f9fc));
		renderTarget->FillRoundedRectangle(cardRect, solidBrush);
		solidBrush->SetColor(m_isHovered ? D2D1::ColorF(0xbbdefb) : D2D1::ColorF(0xd7dde4));
		renderTarget->DrawRoundedRectangle(cardRect, solidBrush);

		// 标题
		std::wstring title;
		if (m_hasEnv)
		{
			title = std::format(L"环境{} 日志（{} 条）{}", m_envIndex, m_logs.size(), m_expanded ? L"（滚轮查看）" : L"");
		}
		else
		{
			title = L"环境日志（未选择环境）";
		}
		// 标题（pTipsFormat 12px 行高约 16px，垂直居中避免偏上）
		constexpr float TITLE_LINE_HEIGHT = 16.f;
		const float titleY = (HEADER_HEIGHT - TITLE_LINE_HEIGHT) * 0.5f;
		solidBrush->SetColor(D2D1::ColorF(0x333333));
		renderTarget->DrawTextW(title.c_str(),
		                        static_cast<UINT32>(title.size()),
		                        app().textFormat().pTipsFormat,
		                        D2D1::RectF(PADDING, titleY, width - PADDING - BTN_WIDTH * 3 - BTN_GAP * 3, titleY + TITLE_LINE_HEIGHT),
		                        solidBrush);

		m_btnToggle.draw(renderCtx);
		m_btnCopy.draw(renderCtx);
		m_btnClear.draw(renderCtx);

		if (!m_expanded || !m_hasEnv)
		{
			return;
		}

		// 标题栏与日志区分隔线（灰色）
		solidBrush->SetColor(D2D1::ColorF(0xe0e0e0));
		renderTarget->DrawLine(D2D1::Point2F(PADDING, HEADER_HEIGHT), D2D1::Point2F(width - PADDING, HEADER_HEIGHT), solidBrush);

		// 日志区域
		const float logTop = getLogAreaTop();
		const float logBottom = height - PADDING;
		const float visibleHeight = logBottom - logTop;

		if (m_logLines.empty())
		{
			solidBrush->SetColor(D2D1::ColorF(0x999999));
			renderTarget->DrawTextW(L"暂无日志",
			                        static_cast<UINT32>(std::wstring_view(L"暂无日志").size()),
			                        app().textFormat().pTipsFormat,
			                        D2D1::RectF(PADDING, logTop + 8.f, width - PADDING, logTop + 28.f),
			                        solidBrush);
			return;
		}

		const float scrollOffset = m_scrollBar.getScrollOffset();
		const std::uint32_t startIndex = static_cast<std::uint32_t>(scrollOffset / LOG_LINE_HEIGHT);
		const std::uint32_t endIndex = std::min(
			static_cast<std::uint32_t>((scrollOffset + visibleHeight) / LOG_LINE_HEIGHT),
			static_cast<std::uint32_t>(m_logLines.size()));

		// 裁剪到日志区域
		renderTarget->PushAxisAlignedClip(D2D1::RectF(0.f, logTop, width, logBottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		float yPos = logTop - scrollOffset + startIndex * LOG_LINE_HEIGHT;
		for (std::uint32_t i = startIndex; i < endIndex && i < m_logLines.size(); ++i)
		{
			solidBrush->SetColor(i % 2 == 0 ? D2D1::ColorF(0xf7f9fc) : D2D1::ColorF(0xf0f3f7));
			renderTarget->FillRectangle(D2D1::RectF(0.f, yPos, width, yPos + LOG_LINE_HEIGHT), solidBrush);

			solidBrush->SetColor(D2D1::ColorF(0x444444));
			renderTarget->DrawTextW(m_logLines[i].c_str(),
			                        static_cast<UINT32>(m_logLines[i].size()),
			                        app().textFormat().pTipsFormat,
			                        D2D1::RectF(PADDING, yPos, width - PADDING - SCROLL_WIDTH - 4.f, yPos + LOG_LINE_HEIGHT),
			                        solidBrush);
			yPos += LOG_LINE_HEIGHT;
		}
		renderTarget->PopAxisAlignedClip();

		if (m_isHovered || m_scrollBar.isThumbPressed())
		{
			m_scrollBar.draw(renderCtx);
		}
	}
}
