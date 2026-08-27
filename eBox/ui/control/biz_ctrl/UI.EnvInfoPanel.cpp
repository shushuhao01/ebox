module UI.EnvInfoPanel;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

#include <psapi.h>

import MainApp;

namespace
{
	constexpr float PADDING = 8.f;
	constexpr float BTN_WIDTH = 52.f;
	constexpr float BTN_HEIGHT = 20.f;
	constexpr float FIELD_LINE_HEIGHT = 22.f;
	// 值文本最大显示宽度，超出省略号（复制时仍是完整内容）
	constexpr float FIELD_VALUE_MAX_WIDTH = 430.f;
	// 查找 Local State 的最大目录深度（环境目录层级较深）
	constexpr std::size_t MAX_SEARCH_DEPTH = 8;

	// 可靠地写入剪贴板（带窗口句柄 + 正确的 null 终止，与 FileStatusCtrl 一致）
	bool copy_text_to_clipboard(HWND hWnd, std::wstring_view text)
	{
		if (!OpenClipboard(hWnd))
		{
			return false;
		}
		EmptyClipboard();
		const HGLOBAL hCopy = GlobalAlloc(GMEM_MOVEABLE, (text.length() + 1) * sizeof(wchar_t));
		if (hCopy == nullptr)
		{
			CloseClipboard();
			return false;
		}
		wchar_t* pStrCopy = static_cast<wchar_t*>(GlobalLock(hCopy));
		if (!pStrCopy)
		{
			GlobalFree(hCopy);
			CloseClipboard();
			return false;
		}
		memcpy(pStrCopy, text.data(), text.length() * sizeof(wchar_t));
		pStrCopy[text.length()] = 0;
		GlobalUnlock(hCopy);
		const HANDLE hData = SetClipboardData(CF_UNICODETEXT, hCopy);
		CloseClipboard();
		return hData != nullptr;
	}

	std::string read_file_bytes(const std::filesystem::path& filePath)
	{
		std::ifstream in{filePath, std::ios::binary};
		if (!in)
		{
			return {};
		}
		return std::string{(std::istreambuf_iterator<char>(in)), {}};
	}

	// 宽松解析 JSON 中的字符串值：找 "key" 后的 "value"
	std::wstring extract_json_string(const std::string& text, std::string_view key)
	{
		const auto keyPos = text.find(key);
		if (keyPos == std::string::npos)
		{
			return {};
		}
		// key 后的第一个引号是 key 自身的闭合引号（如 "machine_id"），
		// 再往后才是值的起始引号，必须跳过闭合引号
		const auto keyEndQuote = text.find('"', keyPos + key.size());
		if (keyEndQuote == std::string::npos)
		{
			return {};
		}
		const auto q1 = text.find('"', keyEndQuote + 1);
		if (q1 == std::string::npos)
		{
			return {};
		}
		const auto q2 = text.find('"', q1 + 1);
		if (q2 == std::string::npos)
		{
			return {};
		}
		std::string_view value{text.data() + q1 + 1, q2 - q1 - 1};
		if (value.size() > 256)
		{
			return {};
		}
		return std::wstring{value.begin(), value.end()};
	}

	// BFS 查找根目录下指定文件名的文件（限制深度，避免遍历过深）
	std::optional<std::filesystem::path> find_file_by_name(const std::filesystem::path& root, std::wstring_view fileName, std::size_t maxDepth)
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		if (!fs::exists(root, ec) || ec)
		{
			return std::nullopt;
		}
		struct Node
		{
			fs::path path;
			std::size_t depth;
		};
		std::vector<Node> queue{{root, 0}};
		std::size_t head = 0;
		while (head < queue.size())
		{
			const Node cur = queue[head++];
			for (const fs::directory_entry& entry : fs::directory_iterator(cur.path, ec))
			{
				if (ec)
				{
					ec.clear();
					break;
				}
				if (entry.is_directory(ec) && !ec)
				{
					if (cur.depth + 1 <= maxDepth)
					{
						queue.push_back({entry.path(), cur.depth + 1});
					}
				}
				else if (entry.is_regular_file(ec) && !ec && entry.path().filename() == fileName)
				{
					return entry.path();
				}
			}
		}
		return std::nullopt;
	}

	std::wstring format_size(std::uint64_t bytes)
	{
		if (bytes < 1024)
		{
			return std::format(L"{} B", bytes);
		}
		if (bytes < 1024 * 1024)
		{
			return std::format(L"{:.1f} KB", static_cast<double>(bytes) / 1024.0);
		}
		return std::format(L"{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
	}

	// 读取 qimei 目录下文件内容（腾讯 qimei 设备标识）
	std::wstring read_qimei_value(const std::filesystem::path& envDir)
	{
		namespace fs = std::filesystem;
		const fs::path qimeiDir = envDir / L"programdata" / L"Tencent" / L"qimei";
		std::error_code ec;
		if (!fs::exists(qimeiDir, ec) || ec)
		{
			return {};
		}
		for (const fs::directory_entry& entry : fs::directory_iterator(qimeiDir, ec))
		{
			if (ec)
			{
				break;
			}
			if (entry.is_regular_file(ec) && !ec)
			{
				const std::string content = read_file_bytes(entry.path());
				if (!content.empty())
				{
					// qimei 值一般是文件内容（纯字符串）
					std::wstring value{content.begin(), content.end()};
					if (value.size() > 64)
					{
						value.resize(64);
					}
					return value;
				}
			}
		}
		return {};
	}
}

namespace ui
{
	EnvInfoPanel::~EnvInfoPanel()
	{
		// 通知后台统计/清理线程：面板已销毁，回投结果不得再触碰 this
		m_alive->store(false);
	}

	void EnvInfoPanel::initialize()
	{
		// 清理按钮：位于复制按钮左侧，点击弹窗选择清理范围
		m_btnClean.setText(L"清理");
		m_btnClean.setBackgroundColor(D2D1::ColorF(0xfff3e0));
		m_btnClean.setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnClean.setBorderColor(D2D1::ColorF(0xfb8c00));
		m_btnClean.setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnClean.setTextColor(D2D1::ColorF(0x555555));
		m_btnClean.setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnClean.setOnClick([this] { onCleanBtnClick(); });

		m_btnCopy.setText(L"复制");
		m_btnCopy.setBackgroundColor(D2D1::ColorF(0xe8f5e9));
		m_btnCopy.setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
		m_btnCopy.setBorderColor(D2D1::ColorF(0x4caf50));
		m_btnCopy.setBorderColor(D2D1::ColorF(0xe0e0e0), Button::EState::Normal);
		m_btnCopy.setTextColor(D2D1::ColorF(0x2e7d32));
		m_btnCopy.setTextFormat(app().textFormat().pToolBtnFormat);
		m_btnCopy.setOnClick([this]
		{
			if (copyInfo())
			{
				// 复制成功：按钮变“已复制”，2 秒后恢复，给用户明确反馈
				m_btnCopy.setText(L"已复制");
				m_btnCopy.setBackgroundColor(D2D1::ColorF(0xa5d6a7), Button::EState::Normal);
				m_btnCopy.update();
				m_copyTimerStopSource.request_stop();
				m_copyTimerStopSource = std::stop_source{};
				app().get_scheduler().addTimer(std::chrono::seconds(2), [this]
				{
					m_btnCopy.setText(L"复制");
					m_btnCopy.setBackgroundColor(D2D1::ColorF(0x000000, 0.f), Button::EState::Normal);
					m_btnCopy.update();
				}, m_copyTimerStopSource.get_token());
			}
		});
	}

	void EnvInfoPanel::setEnv(const std::shared_ptr<biz::Env>& env)
	{
		m_env = env;
		// 切换环境后旧的重字段缓存不再适用：失效并立即后台重新统计
		m_heavyValid = false;
		refreshInfo();
		update();
	}

	void EnvInfoPanel::clearEnv()
	{
		m_env.reset();
		m_fields.clear();
		m_copyText.clear();
		m_expanded = false;
		update();
	}

	void EnvInfoPanel::setExpanded(bool expanded)
	{
		if (m_expanded == expanded)
		{
			return;
		}
		m_expanded = expanded;
		onResize(size().width, size().height);
		update();
	}

	void EnvInfoPanel::toggleExpand()
	{
		if (onExpandRequest)
		{
			onExpandRequest(!m_expanded);
			return;
		}
		setExpanded(!m_expanded);
	}

	void EnvInfoPanel::refreshInfo()
	{
		m_fields.clear();
		m_copyText.clear();
		if (!m_env)
		{
			return;
		}
		namespace fs = std::filesystem;
		const fs::path envDir = fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", m_env->getIndex())};

		// 逐字段组装；任一字段读取失败不影响其它字段（复制内容始终完整）
		auto addField = [this](std::wstring label, std::wstring value)
		{
			// 防御：剥离字符串中的 \0（如注册表 REG_SZ 带回来的 null 终止符），
			// 否则复制到剪贴板时 CF_UNICODETEXT 读到第一个 \0 就截断，只复制出第一行。
			if (!value.empty())
			{
				std::erase(value, L'\0');
				std::erase(label, L'\0');
			}
			m_fields.emplace_back(std::move(label), std::move(value));
		};

		try
		{
			// 1. 环境名称
			addField(L"环境名称", std::wstring{m_env->getName()});
			// 2. 环境索引
			addField(L"环境索引", std::format(L"{}", m_env->getIndex()));
			// 3. 应用路径
			std::wstring appPath{m_env->getAppPath()};
			addField(L"应用路径", appPath.empty() ? L"未绑定（启动后自动绑定）" : std::move(appPath));
			// 4. 环境目录
			addField(L"环境目录", envDir.native());
		}
		catch (...)
		{
		}

		// 5. 进程内存占用（环境内全部进程工作集之和，反映实际内存压力）
		try
		{
			std::uint64_t totalWorkingSet = 0;
			for (const std::shared_ptr<biz::ProcessInfo>& proc : m_env->getAllProcesses())
			{
				PROCESS_MEMORY_COUNTERS pmc{};
				if (GetProcessMemoryInfo(proc->getHandle(), &pmc, sizeof(pmc)))
				{
					totalWorkingSet += pmc.WorkingSetSize;
				}
			}
			addField(L"进程内存占用", totalWorkingSet ? std::format(L"{}（{} 个进程）", format_size(totalWorkingSet), m_env->getAllProcessesCount()) : L"无运行进程");
		}
		catch (...)
		{
			addField(L"进程内存占用", L"读取失败");
		}

		// 6. 环境数据大小：总大小 / 缓存 / 聊天记录。
		//    重字段（GB 级目录树递归）绝不能在 UI 线程同步扫描——那是点环境卡片/
		//    启动按钮卡“未响应”的根因。这里展示缓存值或“统计中…”占位，
		//    由 startHeavyScanAsync 后台统计完成后回填。
		try
		{
			addField(L"环境数据总大小", m_heavyValid ? format_size(m_hTotal) : std::wstring{L"统计中…"});
			addField(L"环境缓存大小", m_heavyValid ? format_size(m_hCache) : std::wstring{L"统计中…"});
			addField(L"聊天记录大小", m_heavyValid ? format_size(m_hChat) : std::wstring{L"统计中…"});
		}
		catch (...)
		{
			addField(L"环境数据大小", L"读取失败");
		}

		// 5. 设备码 machine_id（Local State，Chromium/CEF 设备指纹；文件查找/读取移至后台）
		try
		{
			addField(L"设备码 machine_id", m_heavyValid ? m_hMachineId : std::wstring{L"读取中…"});
		}
		catch (...)
		{
			addField(L"设备码 machine_id", L"读取失败");
		}

		// 6. 注册表 hive（环境注册表指纹）
		try
		{
			const fs::path hivePath = envDir / m_env->getFlagName();
			std::wstring hiveStatus;
			std::error_code ec;
			if (fs::exists(hivePath, ec) && !ec)
			{
				hiveStatus = std::format(L"{}（{}）", m_env->isRegistryHiveLoaded() ? L"已加载" : L"未加载",
				                         format_size(fs::file_size(hivePath, ec)));
			}
			else
			{
				hiveStatus = L"未创建（启动后自动创建）";
			}
			addField(L"注册表 hive", std::format(L"{}｜{}", hivePath.native(), hiveStatus));
		}
		catch (...)
		{
			addField(L"注册表 hive", L"读取失败");
		}

		// 7. 腾讯 qimei（设备标识；文件读取移至后台）
		try
		{
			if (m_heavyValid)
			{
				// qimei 由腾讯会议(WeMeet)生成，企微主程序不生成，不影响企微登录免验证码
				addField(L"腾讯 qimei", m_hQimei.empty() ? std::wstring{L"企微不生成（不影响登录）"} : m_hQimei);
			}
			else
			{
				addField(L"腾讯 qimei", L"读取中…");
			}
		}
		catch (...)
		{
			addField(L"腾讯 qimei", L"读取失败");
		}

		// 组装复制文本（所有字段）
		for (const auto& [label, value] : m_fields)
		{
			m_copyText.append(label);
			m_copyText.append(L": ");
			m_copyText.append(value);
			m_copyText.push_back(L'\n');
		}

		// 后台统计重字段（大小/缓存/聊天记录/machine_id/qimei），完成后回填 UI
		startHeavyScanAsync();
	}

	void EnvInfoPanel::startHeavyScanAsync()
	{
		if (!m_env)
		{
			return;
		}
		const std::uint32_t gen = ++m_scanGen;
		namespace fs = std::filesystem;
		const fs::path envDir = fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", m_env->getIndex())};
		// env 以 shared_ptr 保活：面板/环境销毁后扫描仍可安全读完（目录没了按 0 统计，error_code 兜底）
		std::shared_ptr<biz::Env> env = m_env;
		const auto alive = m_alive;
		std::thread([this, alive, gen, env, envDir]()
		{
			std::uint64_t total = 0;
			std::uint64_t cache = 0;
			std::uint64_t chat = 0;
			std::wstring machineId;
			std::wstring qimei;
			try
			{
				total = env->getEnvDataSize();
			}
			catch (...) {}
			try
			{
				cache = env->getWxworkCacheSize();
			}
			catch (...) {}
			try
			{
				chat = env->getWxworkChatDataSize();
			}
			catch (...) {}
			try
			{
				machineId = L"未生成（启动后自动生成）";
				if (const std::optional<fs::path> localState = find_file_by_name(envDir, L"Local State", MAX_SEARCH_DEPTH))
				{
					const std::string content = read_file_bytes(localState.value());
					const std::wstring id = extract_json_string(content, "machine_id");
					machineId = id.empty() ? std::wstring{L"已生成但未找到 machine_id"} : id;
				}
			}
			catch (...) {}
			try
			{
				qimei = read_qimei_value(envDir);
			}
			catch (...) {}

			// 回到 UI 线程应用结果：alive 守卫防面板已销毁，gen 守卫丢弃过期扫描
			app().get_scheduler().addTask([this, alive, gen, total, cache, chat,
			                               machineId = std::move(machineId), qimei = std::move(qimei)]() mutable
			{
				if (!alive->load() || gen != m_scanGen)
				{
					return;
				}
				applyHeavyResult(total, cache, chat, std::move(machineId), std::move(qimei));
			});
		}).detach();
	}

	void EnvInfoPanel::applyHeavyResult(std::uint64_t total, std::uint64_t cache, std::uint64_t chat,
	                                    std::wstring machineId, std::wstring qimei)
	{
		m_hTotal = total;
		m_hCache = cache;
		m_hChat = chat;
		m_hMachineId = std::move(machineId);
		m_hQimei = std::move(qimei);
		m_heavyValid = true;
		// 就地回填字段值（标签与 refreshInfo 保持一致），找不到则追加兜底
		auto replaceField = [this](std::wstring_view label, const std::wstring& value)
		{
			for (auto& [l, v] : m_fields)
			{
				if (l == label)
				{
					v = value;
					return;
				}
			}
			m_fields.emplace_back(std::wstring{label}, value);
		};
		replaceField(L"环境数据总大小", format_size(m_hTotal));
		replaceField(L"环境缓存大小", format_size(m_hCache));
		replaceField(L"聊天记录大小", format_size(m_hChat));
		replaceField(L"设备码 machine_id", m_hMachineId);
		replaceField(L"腾讯 qimei", m_hQimei.empty() ? std::wstring{L"企微不生成（不影响登录）"} : m_hQimei);
		// 重建复制文本，保证“复制”内容包含最新统计
		m_copyText.clear();
		for (const auto& [label, value] : m_fields)
		{
			m_copyText.append(label);
			m_copyText.append(L": ");
			m_copyText.append(value);
			m_copyText.push_back(L'\n');
		}
		update();
	}

	bool EnvInfoPanel::copyInfo()
	{
		// 无论当前字段是否齐全，都基于最新数据组装并复制（“未生成”等占位也会复制）
		refreshInfo();
		std::wstring text = m_copyText;
		// 兜底：若缓存文本为空但字段已就绪，直接从字段重建，保证复制内容完整
		if (text.empty() && !m_fields.empty())
		{
			for (const auto& [label, value] : m_fields)
			{
				text.append(label);
				text.append(L": ");
				text.append(value);
				text.push_back(L'\n');
			}
		}
		if (text.empty())
		{
			text = L"（暂无环境信息）";
		}
		return copy_text_to_clipboard(owner()->nativeHandle(), text);
	}

	void EnvInfoPanel::onResize(float width, float height)
	{
		const float btnY = (HEADER_HEIGHT - BTN_HEIGHT) * 0.5f;
		const float btnW = BTN_WIDTH;
		constexpr float BTN_GAP = 4.f;
		// 复制按钮靠右，清理按钮在其左侧
		const float copyX = width - PADDING - btnW;
		m_btnCopy.setBounds(D2D1::RectF(copyX, btnY, copyX + btnW, btnY + BTN_HEIGHT));
		const float cleanX = copyX - BTN_GAP - btnW;
		m_btnClean.setBounds(D2D1::RectF(cleanX, btnY, cleanX + btnW, btnY + BTN_HEIGHT));
	}

	void EnvInfoPanel::onMouseEnter(const MouseEvent& e)
	{
		m_isHovered = true;
		update();
	}

	void EnvInfoPanel::onMouseLeave(const MouseEvent& e)
	{
		if (!hitTest(e.point))
		{
			m_isHovered = false;
			update();
		}
	}

	void EnvInfoPanel::onClick(const MouseEvent& e)
	{
		// 点击标题栏空白区域（非按钮）展开/收起
		if (e.point.y >= m_boundsInOwner.top && e.point.y <= m_boundsInOwner.top + HEADER_HEIGHT)
		{
			toggleExpand();
		}
	}

	void EnvInfoPanel::drawImpl(const RenderContext& renderCtx)
	{
		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const auto [width, height] = size();

		// 卡片底色与边框（与日志卡片一致的风格）
		const D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(D2D1::RectF(0.f, 0.f, width, height), 6.f, 6.f);
		solidBrush->SetColor(D2D1::ColorF(0xf7f9fc));
		renderTarget->FillRoundedRectangle(cardRect, solidBrush);
		solidBrush->SetColor(m_isHovered ? D2D1::ColorF(0x90caf9) : D2D1::ColorF(0xb0bec5));
		renderTarget->DrawRoundedRectangle(cardRect, solidBrush);

		// 标题：环境信息（含当前环境名）
		std::wstring title;
		if (m_env)
		{
			title = std::format(L"环境信息（{}）{}", m_env->getName(), m_expanded ? L"｜点击标题收起" : L"｜点击展开");
		}
		else
		{
			title = L"环境信息（未选择环境）";
		}
		constexpr float TITLE_LINE_HEIGHT = 16.f;
		const float titleY = (HEADER_HEIGHT - TITLE_LINE_HEIGHT) * 0.5f;
		solidBrush->SetColor(D2D1::ColorF(0x333333));
		// 标题右边界预留复制+清理两个按钮宽度
		renderTarget->DrawTextW(title.c_str(),
		                        static_cast<UINT32>(title.size()),
		                        app().textFormat().pTipsFormat,
		                        D2D1::RectF(PADDING, titleY, width - PADDING - BTN_WIDTH * 2.f - PADDING * 2.f, titleY + TITLE_LINE_HEIGHT),
		                        solidBrush);

		m_btnClean.draw(renderCtx);
		m_btnCopy.draw(renderCtx);

		if (!m_expanded || !m_env)
		{
			return;
		}

		// 标题栏与内容区分隔线
		solidBrush->SetColor(D2D1::ColorF(0xe0e0e0));
		renderTarget->DrawLine(D2D1::Point2F(PADDING, HEADER_HEIGHT), D2D1::Point2F(width - PADDING, HEADER_HEIGHT), solidBrush);

		// 字段行：标签（固定色）+ 值（可省略）
		constexpr float LABEL_WIDTH = 150.f;
		float yPos = HEADER_HEIGHT + 4.f;
		app().textFormat().setTipsEllipsisTrimming();
		for (const auto& [label, value] : m_fields)
		{
			if (yPos + FIELD_LINE_HEIGHT > height - PADDING)
			{
				break;
			}
			solidBrush->SetColor(D2D1::ColorF(0x888888));
			renderTarget->DrawTextW(label.c_str(),
			                        static_cast<UINT32>(label.size()),
			                        app().textFormat().pTipsFormat,
			                        D2D1::RectF(PADDING + 2.f, yPos, PADDING + 2.f + LABEL_WIDTH, yPos + FIELD_LINE_HEIGHT),
			                        solidBrush);
			solidBrush->SetColor(D2D1::ColorF(0x1a1a1a));
			renderTarget->DrawTextW(value.c_str(),
			                        static_cast<UINT32>(value.size()),
			                        app().textFormat().pTipsFormat,
			                        D2D1::RectF(PADDING + 2.f + LABEL_WIDTH, yPos,
			                                    PADDING + 2.f + LABEL_WIDTH + FIELD_VALUE_MAX_WIDTH, yPos + FIELD_LINE_HEIGHT),
			                        solidBrush);
			yPos += FIELD_LINE_HEIGHT;
		}
		app().textFormat().clearTipsEllipsisTrimming();
	}

	void EnvInfoPanel::onCleanBtnClick()
	{
		if (!m_env || m_cleaning)
		{
			return;
		}
		const std::optional<ECleanOption> option = confirm_clean_dialog(owner(), m_env->getName());
		if (!option.has_value())
		{
			return;
		}
		// 后台清理：GB 级缓存/聊天记录的遍历+删除绝不能阻塞 UI 线程（未响应）
		const ECleanOption opt = option.value();
		m_cleaning = true;
		m_btnClean.setText(L"清理中…");
		m_btnClean.update();
		std::shared_ptr<biz::Env> env = m_env;
		const auto alive = m_alive;
		std::thread([this, alive, env, opt]()
		{
			std::uint64_t freedBytes = 0;
			std::wstring what;
			switch (opt)
			{
			case ECleanOption::Cache:
				what = L"缓存";
				freedBytes = env->cleanWxworkCache();
				break;
			case ECleanOption::ChatData:
				what = L"聊天记录";
				freedBytes = env->cleanWxworkChatData();
				break;
			case ECleanOption::Both:
				what = L"缓存与聊天记录";
				freedBytes = env->cleanWxworkCache() + env->cleanWxworkChatData();
				break;
			}
			// 回 UI 线程：刷新统计 + 提示（alive 守卫防面板已销毁）
			app().get_scheduler().addTask([this, alive, env, what = std::move(what), freedBytes]() mutable
			{
				if (!alive->load())
				{
					return;
				}
				m_cleaning = false;
				m_btnClean.setText(L"清理");
				if (m_env == env)
				{
					// 数据已变化：失效缓存并重新统计（后台）
					m_heavyValid = false;
					refreshInfo();
				}
				update();
				biz::env_logger().append(env->getIndex(), biz::EnvLogType::Info, biz::EnvLogStatus::Success,
				                         L"清理环境数据", std::format(L"已清理{}：{}字节", what, freedBytes));
				MessageBoxW(owner()->nativeHandle(),
				            std::format(L"已清理{}，释放空间 {:.1f} MB。\n\n（若环境正在运行，被占用的文件将保留，下次启动自动重建。）", what, freedBytes / 1024.0 / 1024.0).c_str(),
				            MainApp::appName.data(), MB_OK | MB_ICONINFORMATION);
			});
		}).detach();
	}
}
