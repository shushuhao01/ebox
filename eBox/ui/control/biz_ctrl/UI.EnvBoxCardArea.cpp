module;
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
module UI.EnvBoxCardArea;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import MainApp;
import Scheduler;

namespace
{
	constexpr float CARD_HEIGHT = 97.f; //132.f;
	constexpr float CARD_MARGIN_BOTTOM = 12.f;
	constexpr float LIST_HEIGHT = 40.f;
	constexpr float LIST_MARGIN_BOTTOM = 8.f;
	constexpr float WHEEL_SCROLL_SIZE = 24.f;
}

namespace ui
{
	float EnvBoxCardArea::getCardHeight() const
	{
		return m_viewMode == EViewMode::List ? LIST_HEIGHT : CARD_HEIGHT;
	}

	float EnvBoxCardArea::getItemHeight() const
	{
		return m_viewMode == EViewMode::List ? LIST_HEIGHT + LIST_MARGIN_BOTTOM : CARD_HEIGHT + CARD_MARGIN_BOTTOM;
	}

	EnvBoxCardArea::~EnvBoxCardArea()
	{
		// 不再接收通知，且会等待已经通知的回调结束
		biz::env_mgr().setEnvChangeNotify(nullptr);
		// 之后就绝对不会spawn新的协程，才可以安全等待所有协程结束
		m_asyncScope.join();
	}

	std::shared_ptr<biz::Env> EnvBoxCardArea::selectSuitableEnvAndSetItBusyTemp(const std::wstring& procFullPath)
	{
		std::shared_ptr<biz::Env> result;
		namespace fs = std::filesystem;
		fs::path path{procFullPath};
		const bool isExe = path.extension().native() == L".exe";

		// 1. 优先复用上次启动该程序使用的环境（“老环境再次启动”）
		if (isExe)
		{
			if (std::shared_ptr<biz::Env> lastEnv = biz::env_mgr().getLastEnvForProc(procFullPath); lastEnv)
			{
				const auto it = m_envs.find(lastEnv->getIndex());
				if (it != m_envs.end())
				{
					EnvBoxCard* box = it->second.get();
					if (box->isIdle() && !box->contains(procFullPath))
					{
						result = box->getEnv();
						box->setBusyTemp();
						return result;
					}
				}
			}
		}

		// 2. 回退：挑第一个“空闲且没在跑该exe”的环境
		for (auto it = m_envs.begin(); it != m_envs.end(); ++it)
		{
			EnvBoxCard* box = it->second.get();
			if (!box->isIdle())
			{
				continue;
			}
			if (isExe)
			{
				if (box->contains(procFullPath))
				{
					continue;
				}
			}
			else
			{
				if (!box->isEmpty())
				{
					continue;
				}
			}
			result = box->getEnv();
			box->setBusyTemp();
			break;
		}
		return result;
	}

	void EnvBoxCardArea::launchProcess(const std::wstring& procFullPath, std::wstring_view params /*= L""*/)
	{
		if (const std::shared_ptr<biz::Env> suitableEnv = selectSuitableEnvAndSetItBusyTemp(procFullPath))
		{
			// 复用合适环境启动 → 选中该环境，右侧进程区联动显示
			selectEnvByIndex(suitableEnv->getIndex());
			biz::launcher().run(suitableEnv, procFullPath, params);
		}
		else
		{
			// 没有合适的env，则创建新的
			// 这里不考虑m_envs了，env的创建回调中会加入m_envs，这里直接使用launcher接口启动进程
			m_bAutoSelectNextNewEnv = true;
			biz::launcher().runInNewEnv(procFullPath, params);
		}
	}

	void EnvBoxCardArea::launchProcessInNewEnv(const std::wstring& procFullPath, std::wstring_view params /*= L""*/)
	{
		// 多开入口：每次启动都新建一个独立环境，登录态/数据与其他环境完全隔离。
		// 新环境创建后自动选中（onEnvCountChange 里处理），右侧进程区直接显示该环境
		m_bAutoSelectNextNewEnv = true;
		biz::launcher().runInNewEnv(procFullPath, params);
	}

	void EnvBoxCardArea::selectEnvByIndex(std::uint32_t index)
	{
		if (const auto it = m_envs.find(index); it != m_envs.end())
		{
			onEnvSelected(it->second.get(), true);
		}
	}

	bool EnvBoxCardArea::hasAnyProcesses() const
	{
		for (auto it = m_envs.begin(); it != m_envs.end(); ++it)
		{
			if (!it->second->isEmpty())
			{
				return true;
			}
		}
		return false;
	}

	std::size_t EnvBoxCardArea::getOnlineEnvCount() const
	{
		std::size_t count = 0;
		for (auto it = m_envs.begin(); it != m_envs.end(); ++it)
		{
			if (!it->second->isEmpty())
			{
				++count;
			}
		}
		return count;
	}

	void EnvBoxCardArea::setViewMode(EViewMode mode)
	{
		if (m_viewMode == mode)
		{
			return;
		}
		m_viewMode = mode;
		for (auto it = m_envs.begin(); it != m_envs.end(); ++it)
		{
			it->second->setViewMode(mode);
		}
		m_scrollBar->setTotalSize(m_envs.size() * getItemHeight());
		updateAllEnvPos();
	}

	void EnvBoxCardArea::onResize(float width, float height)
	{
		m_scrollBar->setVisibleSize(height);
		m_scrollBar->setBounds(D2D1::RectF(width - shadowSize - scrollWidth, 0.f,
		                                   width - shadowSize, height));
	}

	void EnvBoxCardArea::onMouseEnter(const MouseEvent& e)
	{
		m_isHovered = true;
		update();
	}

	void EnvBoxCardArea::onMouseLeave(const MouseEvent& e)
	{
		// 1.进入子控件会触发leave
		// 2.离开子控件也会触发leave(除非子控件拦截)
		if (!hitTest(e.point))
		{
			m_isHovered = false;
			update();
		}
		e.accept = true;
	}

	void EnvBoxCardArea::onMouseWheel(const MouseWheelEvent& e)
	{
		e.accept = true;

		const float wheelCount = e.zDelta / 120.f;
		m_scrollBar->scroll(-wheelCount * WHEEL_SCROLL_SIZE);
	}

	void EnvBoxCardArea::initialize()
	{
		biz::env_mgr().setEnvChangeNotify([this](biz::EnvManager::EChangeType changeType, const std::shared_ptr<biz::Env>& env)
		{
			m_asyncScope.spawn(onEnvCountChange(changeType, env));
		});
		std::vector<std::shared_ptr<biz::Env>> allEnv = biz::env_mgr().getAllEnv();
		for (auto it = allEnv.begin(); it != allEnv.end(); ++it)
		{
			addEnv(*it, true);
		}

		m_scrollBar = std::make_unique<ScrollBar>(this);
		m_scrollBar->setThumbPosChangeNotify([this] { updateAllEnvPos(); });
		m_scrollBar->setTotalSize(m_envs.size() * getItemHeight());

		// 恢复持久化的卡片显示顺序（长按拖拽排序结果）
		loadEnvOrder();
		updateAllEnvPos();
	}

	coro::LazyTask<void> EnvBoxCardArea::onEnvCountChange(biz::EnvManager::EChangeType changeType, std::shared_ptr<biz::Env> env)
	{
		// 转到主线程
		co_await sched::transfer_to(app().get_scheduler());

		if (changeType == biz::EnvManager::EChangeType::Create)
		{
			addEnv(env);
			// 用户通过"启动新进程/总启动"主动新建的环境 → 自动选中，右侧进程区联动显示
			if (m_bAutoSelectNextNewEnv)
			{
				m_bAutoSelectNextNewEnv = false;
				selectEnvByIndex(env->getIndex());
			}
		}
		else if (changeType == biz::EnvManager::EChangeType::Delete)
		{
			removeEnv(env->getIndex());
		}

		m_scrollBar->setTotalSize(m_envs.size() * getItemHeight());
		if (m_pfnOnSummaryChange)
		{
			m_pfnOnSummaryChange();
		}
	}

	void EnvBoxCardArea::addEnv(const std::shared_ptr<biz::Env>& env, bool initialIdle /*= false*/)
	{
		if (m_envs.contains(env->getIndex()))
		{
			return;
		}
		std::unique_ptr<EnvBoxCard> card = std::make_unique<EnvBoxCard>(initialIdle, this);
		card->setEnv(env);
		// 新建卡片必须继承当前视图模式（列表/卡片），否则列表视图下新环境仍按卡片布局绘制：
		// 高度只有 40px 的列表项里画卡片内容，名称不居中、竖排按钮超出高度显示不出、圆点错位。
		card->setViewMode(m_viewMode);
		card->setOnSelect([this, rawPtr = card.get()](bool b) { onEnvSelected(rawPtr, b); });
		card->setOnSummaryChange([this]()
		{
			if (m_pfnOnSummaryChange)
			{
				m_pfnOnSummaryChange();
			}
		});
		// 长按拖拽排序回调
		card->setOnDragCallbacks(
			[this](EnvBoxCard* c, float x, float y) { onDragStart(c, x, y); },
			[this](EnvBoxCard* c, float x, float y) { onDragMove(c, x, y); },
			[this](EnvBoxCard* c, bool cancelled) { onDragEnd(c, cancelled); });
		m_envs.insert(std::make_pair(env->getIndex(), std::move(card)));
		m_displayOrder.push_back(env->getIndex());
	}

	void EnvBoxCardArea::removeEnv(std::uint32_t envIndex)
	{
		if (const auto it = m_envs.find(envIndex); it != m_envs.end())
		{
			onEnvSelected(it->second.get(), false);
			m_envs.erase(it);
		}
		// 同步移除显示顺序
		m_displayOrder.erase(std::remove(m_displayOrder.begin(), m_displayOrder.end(), envIndex), m_displayOrder.end());
		if (m_dragCard && m_dragCard->getEnv()->getIndex() == envIndex)
		{
			m_dragCard = nullptr;
			m_dragging = false;
		}
	}

	void EnvBoxCardArea::onEnvSelected(EnvBoxCard* card, bool bSelected)
	{
		if (bSelected)
		{
			if (m_currentSelectedEnv)
			{
				m_currentSelectedEnv->programmaticDeselect();
				m_currentSelectedEnv->setOnProcCountChange(nullptr);
			}
			m_currentSelectedEnv = card;
			m_currentSelectedEnv->setOnProcCountChange(m_pfnOnProcCountChange);
			if (m_pfnOnSelect)
			{
				m_pfnOnSelect(card->getEnv(), true);
			}
		}
		else
		{
			if (m_currentSelectedEnv == card)
			{
				m_currentSelectedEnv = nullptr;

				card->setOnProcCountChange(nullptr);
				if (m_pfnOnSelect)
				{
					m_pfnOnSelect(card->getEnv(), false);
				}
			}
		}

		update();
	}

	void EnvBoxCardArea::updateAllEnvPos()
	{
		if (m_envs.empty())
		{
			m_envsToDraw.clear();
			updateWholeWnd();
			return;
		}

		const float itemHeight = getItemHeight();
		const float cardHeight = getCardHeight();
		const float scrollOffset = m_scrollBar->getScrollOffset();
		const std::uint32_t startIndex = static_cast<std::uint32_t>(scrollOffset / itemHeight);
		const std::uint32_t endIndex = std::min(
			static_cast<std::uint32_t>((scrollOffset + m_scrollBar->getVisibleSize()) / itemHeight),
			static_cast<std::uint32_t>(m_envs.size() - 1));
		m_envsToDraw.clear();

		if (startIndex > endIndex)
		{
			updateWholeWnd();
			return;
		}

		m_envsToDraw.reserve(endIndex - startIndex);

		const std::uint32_t startDrawOffsetY = static_cast<std::uint32_t>(scrollOffset) % static_cast<std::uint32_t>(itemHeight);
		float startYPos = shadowSize - shadowOffsetY - startDrawOffsetY;
		const auto drawSize = size();
		// 按显示顺序（m_displayOrder，支持长按拖拽重排）迭代布局
		for (std::size_t index = 0; index < m_displayOrder.size(); ++index)
		{
			const std::uint32_t envIndex = m_displayOrder[index];
			const auto it = m_envs.find(envIndex);
			if (it == m_envs.end())
			{
				continue;
			}
			EnvBoxCard* card = it->second.get();
			if (index < startIndex || index > endIndex)
			{
				card->setBounds(D2D1::RectF(shadowSize, drawSize.height + itemHeight,
				                            drawSize.width - shadowSize - scrollAreaWidth, drawSize.height + itemHeight + cardHeight));
				continue;
			}
			float cardTop = startYPos;
			// 拖拽中的卡片跟随鼠标（悬浮显示），其它卡片正常排布
			if (m_dragging && card == m_dragCard)
			{
				cardTop = m_dragCurY;
			}
			card->setBounds(D2D1::RectF(shadowSize, cardTop,
			                            drawSize.width - shadowSize - scrollAreaWidth, cardTop + cardHeight));
			m_envsToDraw.push_back(card);
			startYPos += itemHeight;
		}
		updateWholeWnd();
	}

	void EnvBoxCardArea::onDragStart(EnvBoxCard* card, float ownerX, float ownerY)
	{
		if (!card)
		{
			return;
		}
		m_dragging = true;
		m_dragCard = card;
		const D2D1_RECT_F cardBounds = card->getBoundsInOwner();
		m_dragGrabOffsetY = ownerY - cardBounds.top;
		m_dragCurY = cardBounds.top - m_boundsInOwner.top;
		updateAllEnvPos();
	}

	void EnvBoxCardArea::onDragMove(EnvBoxCard* card, float ownerX, float ownerY)
	{
		if (!m_dragging || card != m_dragCard || m_displayOrder.size() <= 1)
		{
			updateAllEnvPos();
			return;
		}
		const float itemHeight = getItemHeight();
		// 本地拖拽位置
		m_dragCurY = (ownerY - m_boundsInOwner.top) - m_dragGrabOffsetY;
		// 目标插槽（按卡片中线对齐）
		const std::size_t slotCount = m_displayOrder.size();
		long targetSlot = static_cast<long>(std::floor((m_dragCurY + itemHeight * 0.5f) / itemHeight));
		targetSlot = std::clamp<long>(targetSlot, 0, static_cast<long>(slotCount - 1));
		// 当前插槽
		const auto it = std::find(m_displayOrder.begin(), m_displayOrder.end(), card->getEnv()->getIndex());
		if (it == m_displayOrder.end())
		{
			updateAllEnvPos();
			return;
		}
		const long curSlot = static_cast<long>(it - m_displayOrder.begin());
		if (curSlot != targetSlot)
		{
			m_displayOrder.erase(it);
			m_displayOrder.insert(m_displayOrder.begin() + targetSlot, card->getEnv()->getIndex());
		}
		updateAllEnvPos();
	}

	void EnvBoxCardArea::onDragEnd(EnvBoxCard* card, bool cancelled)
	{
		(void)card;
		(void)cancelled;
		if (!m_dragging)
		{
			return;
		}
		m_dragging = false;
		m_dragCard = nullptr;
		// 持久化排序结果，下次启动保持
		saveEnvOrder();
		updateAllEnvPos();
	}

	void EnvBoxCardArea::loadEnvOrder()
	{
		HKEY hKey = nullptr;
		std::wstring orderText;
		// 优先读新键 Software\eBox；未命中则读旧键 Software\2Box（兼容老版本）
		if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\eBox", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		{
			RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\2Box", 0, KEY_READ, &hKey);
		}
		if (hKey)
		{
			wchar_t buf[2048]{};
			DWORD size = sizeof(buf);
			if (RegQueryValueExW(hKey, L"EnvOrder", nullptr, nullptr,
			                     reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS)
			{
				orderText = buf;
			}
			RegCloseKey(hKey);
		}
		if (orderText.empty())
		{
			return;
		}

		std::vector<std::uint32_t> newOrder;
		std::wstring token;
		std::wstringstream ss(orderText);
		while (std::getline(ss, token, L','))
		{
			if (token.empty())
			{
				continue;
			}
			wchar_t* end = nullptr;
			const long v = wcstol(token.c_str(), &end, 10);
			if (end && *end == L'\0' && v >= 0 && m_envs.contains(static_cast<std::uint32_t>(v)))
			{
				if (std::find(newOrder.begin(), newOrder.end(), static_cast<std::uint32_t>(v)) == newOrder.end())
				{
					newOrder.push_back(static_cast<std::uint32_t>(v));
				}
			}
		}
		if (newOrder.empty())
		{
			return;
		}
		// 补齐注册表中缺失（新创建）的环境，保证顺序完整
		for (const auto& [idx, card] : m_envs)
		{
			(void)card;
			if (std::find(newOrder.begin(), newOrder.end(), idx) == newOrder.end())
			{
				newOrder.push_back(idx);
			}
		}
		m_displayOrder = std::move(newOrder);
	}

	void EnvBoxCardArea::saveEnvOrder()
	{
		std::wstring text;
		for (std::size_t i = 0; i < m_displayOrder.size(); ++i)
		{
			if (i)
			{
				text += L',';
			}
			text += std::to_wstring(m_displayOrder[i]);
		}
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\eBox", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExW(hKey, L"EnvOrder", 0, REG_SZ,
			               reinterpret_cast<const BYTE*>(text.c_str()),
			               static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
			RegCloseKey(hKey);
		}
	}

	void EnvBoxCardArea::drawImpl(const RenderContext& renderCtx)
	{
		if (m_envsToDraw.empty())
		{
			return;
		}
		for (auto it = m_envsToDraw.begin(); it != m_envsToDraw.end(); ++it)
		{
			EnvBoxCard* card = *it;
			if (card->isHovered())
			{
				draw_box_shadow(renderCtx, card->getBounds(),
				                {
					                .offset = D2D1::Point2F(0.f, shadowOffsetY),
					                .size = shadowSize,
					                .layers = static_cast<int>(shadowSize),
					                .color = D2D1::ColorF{0x000000, 0.03f},
					                .radius = 12.f
				                });
			}
			else
			{
				draw_box_shadow(renderCtx, card->getBounds(), {.offset = D2D1::Point2F(0.f, 1.f), .radius = 12.f});
			}
			card->draw(renderCtx);
		}

		if (m_isHovered || m_scrollBar->isThumbPressed())
		{
			m_scrollBar->draw(renderCtx);
		}
	}
}
