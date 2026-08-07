module UI.FeaturesArea;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

#include <pdh.h>
#include <dxgi.h>
#include <psapi.h>

// 个别 Windows SDK 头在模块单元中宏不可见时兜底
#ifndef PDH_MORE_DATA
#define PDH_MORE_DATA ((PDH_STATUS)0x800007D2L)
#endif

import MainApp;

namespace
{
	constexpr float PADDING = 14.f;
	constexpr float CARD_GAP = 10.f;
	constexpr float CARD_RADIUS = 10.f;
	constexpr float TITLE_LINE_HEIGHT = 16.f;
	constexpr float VALUE_LINE_HEIGHT = 30.f;
	constexpr float DETAIL_LINE_HEIGHT = 14.f;

	// 看板卡片配色（青蓝系，与应用主色调一致；浅底区分背景）
	// 注意：D2D1_COLOR_F 是 {r,g,b,a} 四元结构体，聚合初始化必须给 4 个浮点分量，
	// 不能像 D2D1::ColorF(0xRRGGBB, alpha) 那样传整数色值（那会把整数当 r 导致超界变黄）。
	constexpr D2D1_COLOR_F COLOR_CPU{0.118f, 0.533f, 0.898f, 1.f};    // #1e88e5 蓝
	constexpr D2D1_COLOR_F COLOR_MEM{0.149f, 0.651f, 0.604f, 1.f};    // #26a69a 青绿
	constexpr D2D1_COLOR_F COLOR_GPU{0.361f, 0.420f, 0.753f, 1.f};    // #5c6bc0 靛蓝
	constexpr D2D1_COLOR_F COLOR_VRAM{0.012f, 0.608f, 0.898f, 1.f};   // #039be5 天蓝
	constexpr D2D1_COLOR_F COLOR_DISK{0.161f, 0.714f, 0.965f, 1.f};   // #29b6f6 亮蓝
	// 饼状图：已用=红、未用=蓝
	constexpr D2D1_COLOR_F COLOR_DISK_USED{0.898f, 0.223f, 0.208f, 1.f};   // #e53935 红
	constexpr D2D1_COLOR_F COLOR_DISK_FREE{0.333f, 0.588f, 0.894f, 1.f};   // #5596e4 蓝
	// 卡片统一浅青蓝底色
	constexpr D2D1_COLOR_F CARD_TINT{0.95f, 0.98f, 1.0f, 1.f};

	// 将字节数格式化为可读文本（自动 B/KB/MB/GB/TB）
	std::wstring format_bytes(std::uint64_t bytes)
	{
		constexpr double KB = 1024.0;
		constexpr double MB = KB * 1024.0;
		constexpr double GB = MB * 1024.0;
		constexpr double TB = GB * 1024.0;
		if (bytes >= static_cast<std::uint64_t>(TB))
		{
			return std::format(L"{:.1f} TB", bytes / TB);
		}
		if (bytes >= static_cast<std::uint64_t>(GB))
		{
			return std::format(L"{:.1f} GB", bytes / GB);
		}
		if (bytes >= static_cast<std::uint64_t>(MB))
		{
			return std::format(L"{:.1f} MB", bytes / MB);
		}
		if (bytes >= static_cast<std::uint64_t>(KB))
		{
			return std::format(L"{:.1f} KB", bytes / KB);
		}
		return std::format(L"{} B", bytes);
	}
}

namespace ui
{
	void FeaturesArea::initialize()
	{
		// 立即采样一次拿到初始值，随后周期性刷新（采样线程已在主循环内）
		sampleAndUpdate();
		m_timerStopSource = std::stop_source{};
		app().get_scheduler().addPeriodicTimer(SAMPLE_INTERVAL, [this]
		{
			sampleAndUpdate();
		}, m_timerStopSource.get_token());
	}

	void FeaturesArea::sampleAndUpdate()
	{
		// ---- CPU 使用率（GetSystemTimes 差值法）----
		{
			FILETIME idle{};
			FILETIME kernel{};
			FILETIME user{};
			if (GetSystemTimes(&idle, &kernel, &user))
			{
				const auto fileTimeToU64 = [](const FILETIME& ft)
				{
					return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
				};
				const std::uint64_t idleTicks = fileTimeToU64(idle);
				const std::uint64_t kernelTicks = fileTimeToU64(kernel);
				const std::uint64_t userTicks = fileTimeToU64(user);
				if (m_cpuValid)
				{
					const std::uint64_t idleDelta = idleTicks - m_lastIdleTicks;
					const std::uint64_t kernelDelta = kernelTicks - m_lastKernelTicks;
					const std::uint64_t userDelta = userTicks - m_lastUserTicks;
					const std::uint64_t totalDelta = kernelDelta + userDelta;
					if (totalDelta > 0)
					{
						// kernel 中已含 idle，占用 = (total - idle) / total
						const float usage = 100.f * static_cast<float>(totalDelta - idleDelta) / static_cast<float>(totalDelta);
						m_cpuPercent = std::clamp(usage, 0.f, 100.f);
					}
				}
				m_lastIdleTicks = idleTicks;
				m_lastKernelTicks = kernelTicks;
				m_lastUserTicks = userTicks;
				m_cpuValid = true;
			}
			m_cpuHistory.push_back(m_cpuPercent);
			if (m_cpuHistory.size() > HISTORY_SIZE)
			{
				m_cpuHistory.pop_front();
			}
		}

		// ---- 内存（GlobalMemoryStatusEx）----
		{
			MEMORYSTATUSEX msx{sizeof(msx)};
			if (GlobalMemoryStatusEx(&msx))
			{
				m_memUsedBytes = msx.ullTotalPhys - msx.ullAvailPhys;
				m_memTotalBytes = msx.ullTotalPhys;
				m_memPercent = msx.ullTotalPhys ? 100.f * static_cast<float>(m_memUsedBytes) / static_cast<float>(msx.ullTotalPhys) : 0.f;
			}
			m_memHistory.push_back(m_memPercent);
			if (m_memHistory.size() > HISTORY_SIZE)
			{
				m_memHistory.pop_front();
			}
		}

		// ---- GPU 利用率 + 显存（PDH 计数器，失败则标记不可用）----
		{
			static HQUERY hQuery = nullptr;
			static HCOUNTER hGpuCounter = nullptr;
			static HCOUNTER hVramCounter = nullptr;
			static bool queried = false;
			static bool firstCollect = true;
			if (!queried)
			{
				queried = true;
				PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &hQuery);
				if (status != ERROR_SUCCESS)
				{
					hQuery = nullptr;
				}
				else
				{
					// 英文计数器路径在任意语言系统上均可解析（PdhAddEnglishCounter）
					status = PdhAddEnglishCounterW(hQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &hGpuCounter);
					if (status != ERROR_SUCCESS)
					{
						hGpuCounter = nullptr;
					}
					else
					{
						m_gpuAvailable = true;
					}
					status = PdhAddEnglishCounterW(hQuery, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &hVramCounter);
					if (status != ERROR_SUCCESS)
					{
						hVramCounter = nullptr;
					}
					else
					{
						m_vramAvailable = true;
					}
				}
			}

			if (hQuery)
			{
				const PDH_STATUS collectStatus = PdhCollectQueryData(hQuery);
				if (!firstCollect)
				{
					if (collectStatus == ERROR_SUCCESS && hGpuCounter)
					{
						DWORD bufferSize = 0;
						DWORD itemCount = 0;
						PDH_FMT_COUNTERVALUE_ITEM_W* items = nullptr;
						if (PdhGetFormattedCounterArrayW(hGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr) == PDH_MORE_DATA &&
						    bufferSize > 0)
						{
							std::vector<std::byte> buffer(bufferSize);
							items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
							if (PdhGetFormattedCounterArrayW(hGpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items) == ERROR_SUCCESS)
							{
								double sum = 0.0;
								for (DWORD i = 0; i < itemCount; ++i)
								{
									if (items[i].FmtValue.CStatus == ERROR_SUCCESS)
									{
										sum += items[i].FmtValue.doubleValue;
									}
								}
								// 多引擎求和可能超过 100，clamp 到 100 表示满载
								m_gpuPercent = std::clamp(static_cast<float>(sum), 0.f, 100.f);
							}
						}
					}
					if (collectStatus == ERROR_SUCCESS && hVramCounter)
					{
						DWORD bufferSize = 0;
						DWORD itemCount = 0;
						if (PdhGetFormattedCounterArrayW(hVramCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr) == PDH_MORE_DATA &&
						    bufferSize > 0)
						{
							std::vector<std::byte> buffer(bufferSize);
							auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
							if (PdhGetFormattedCounterArrayW(hVramCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items) == ERROR_SUCCESS)
							{
								double sum = 0.0;
								for (DWORD i = 0; i < itemCount; ++i)
								{
									if (items[i].FmtValue.CStatus == ERROR_SUCCESS)
									{
										sum += items[i].FmtValue.doubleValue;
									}
								}
								m_vramUsedBytes = static_cast<std::uint64_t>(sum);
							}
						}
					}
				}
				else
				{
					firstCollect = false;
				}
			}

			// 显存总量兜底：优先尝试 PDH “GPU Adapter Memory\Dedicated Usage” 读出预算
			if (m_vramTotalBytes == 0 && hQuery)
			{
				static HCOUNTER hVramTotalCounter = nullptr;
				if (hVramTotalCounter == nullptr)
				{
					PdhAddEnglishCounterW(hQuery, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &hVramTotalCounter);
				}
				if (hVramTotalCounter)
				{
					DWORD bufferSize = 0;
					DWORD itemCount = 0;
					if (PdhGetFormattedCounterArrayW(hVramTotalCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr) == PDH_MORE_DATA &&
					    bufferSize > 0)
					{
						std::vector<std::byte> buffer(bufferSize);
						auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
						if (PdhGetFormattedCounterArrayW(hVramTotalCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items) == ERROR_SUCCESS)
						{
							double max = 0.0;
							for (DWORD i = 0; i < itemCount; ++i)
							{
								if (items[i].FmtValue.CStatus == ERROR_SUCCESS)
								{
									max = std::max(max, items[i].FmtValue.doubleValue);
								}
							}
							if (max > 0)
							{
								m_vramTotalBytes = static_cast<std::uint64_t>(max);
							}
						}
					}
				}
			}

			m_vramPercent = m_vramTotalBytes ? 100.f * static_cast<float>(m_vramUsedBytes) / static_cast<float>(m_vramTotalBytes) : 0.f;
			m_gpuHistory.push_back(m_gpuPercent);
			if (m_gpuHistory.size() > HISTORY_SIZE)
			{
				m_gpuHistory.pop_front();
			}
			m_vramHistory.push_back(m_vramPercent);
			if (m_vramHistory.size() > HISTORY_SIZE)
			{
				m_vramHistory.pop_front();
			}
		}

		// ---- 磁盘（当前选中盘使用量；首次枚举系统所有盘符）----
		{
			// 首次：枚举系统全部盘符（GetLogicalDrives），选系统盘（或首盘）为默认
			if (m_diskDrives.empty())
			{
				const DWORD mask = GetLogicalDrives();
				for (int i = 0; i < 26; ++i)
				{
					if (mask & (1u << i))
					{
						const wchar_t drive = static_cast<wchar_t>(L'A' + i);
						m_diskDrives.emplace_back(std::wstring(1, drive) + L":\\");
					}
				}
				// 默认选系统盘（环境变量 SystemDrive，如 C:\）
				const DWORD sysLen = GetEnvironmentVariableW(L"SystemDrive", nullptr, 0);
				if (sysLen > 0)
				{
					std::wstring sysDrive(sysLen, L'\0');
					GetEnvironmentVariableW(L"SystemDrive", sysDrive.data(), sysLen);
					if (!sysDrive.empty())
					{
						for (size_t i = 0; i < m_diskDrives.size(); ++i)
						{
							if (_wcsicmp(m_diskDrives[i].c_str(), sysDrive.c_str()) == 0)
							{
								m_diskIndex = i;
								break;
							}
						}
					}
				}
			}

			if (!m_diskDrives.empty() && m_diskIndex < m_diskDrives.size())
			{
				ULARGE_INTEGER totalBytes{};
				ULARGE_INTEGER freeBytes{};
				if (GetDiskFreeSpaceExW(m_diskDrives[m_diskIndex].c_str(), nullptr, &totalBytes, &freeBytes))
				{
					m_diskTotalBytes = totalBytes.QuadPart;
					m_diskUsedBytes = totalBytes.QuadPart - freeBytes.QuadPart;
				}
			}
			// 记录当前盘曲线历史
			const float diskPercent = m_diskTotalBytes ? 100.f * static_cast<float>(m_diskUsedBytes) / static_cast<float>(m_diskTotalBytes) : 0.f;
			m_diskHistory.push_back(diskPercent);
			if (m_diskHistory.size() > HISTORY_SIZE)
			{
				m_diskHistory.pop_front();
			}
			// 标题显示文本（如 "C盘使用 ⇄"）
			if (!m_diskDrives.empty() && m_diskIndex < m_diskDrives.size())
			{
				const std::wstring& drv = m_diskDrives[m_diskIndex];
				if (drv.size() >= 3 && drv[1] == L':')
				{
					m_diskDisplay = std::format(L"{}盘使用 \u21C4", drv[0]);
				}
				else
				{
					m_diskDisplay = L"磁盘使用 \u21C4";
				}
			}
		}

		update();
	}

	void FeaturesArea::onResize(float width, float height)
	{
		(void)width;
		(void)height;
		// 卡片尺寸在 drawImpl 中按当前宽高动态计算，无需额外布局
	}

	void FeaturesArea::drawImpl(const RenderContext& renderCtx)
	{
		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const auto [width, height] = size();

		// 卡片背景（整体浅色底 + 圆角）
		const D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(D2D1::RectF(0.f, 0.f, width, height), CARD_RADIUS, CARD_RADIUS);
		solidBrush->SetColor(D2D1::ColorF(0xffffff));
		renderTarget->FillRoundedRectangle(roundedRect, solidBrush);
		solidBrush->SetColor(D2D1::ColorF(0xe4e7eb));
		renderTarget->DrawRoundedRectangle(roundedRect, solidBrush);

		// 卡片区域计算：5 张指标卡横排（CPU / 内存 / GPU / 显存 / 磁盘）
		const float cardTop = PADDING;
		const float cardBottom = height - PADDING;
		const float totalGap = CARD_GAP * 4.f;
		const float cardWidth = (width - PADDING * 2.f - totalGap) / 5.f;

		const auto buildValue = [](float percent, std::uint64_t used, std::uint64_t total) -> std::wstring
		{
			return std::format(L"{:.0f}%  {}/{}", percent, format_bytes(used), format_bytes(total));
		};

		D2D1_RECT_F card{};
		card.top = cardTop;
		card.bottom = cardBottom;
		const auto placeCard = [&](size_t index)
		{
			card.left = PADDING + index * (cardWidth + CARD_GAP);
			card.right = card.left + cardWidth;
		};

		placeCard(0);
		drawMetricCard(renderCtx, card, L"CPU 使用率", std::format(L"{:.0f}%", m_cpuPercent), m_cpuHistory, 100.f, COLOR_CPU);
		placeCard(1);
		drawMetricCard(renderCtx, card, L"内存使用", buildValue(m_memPercent, m_memUsedBytes, m_memTotalBytes), m_memHistory, 100.f, COLOR_MEM);
		placeCard(2);
		drawMetricCard(renderCtx, card, L"GPU 使用率", m_gpuAvailable ? std::format(L"{:.0f}%", m_gpuPercent) : L"--", m_gpuHistory, 100.f, COLOR_GPU);
		placeCard(3);
		drawMetricCard(renderCtx, card, L"显存使用", m_vramTotalBytes ? buildValue(m_vramPercent, m_vramUsedBytes, m_vramTotalBytes) : L"--", m_vramHistory, 100.f, COLOR_VRAM);
		placeCard(4);
		// 磁盘卡：环形饼图（红=已用 / 蓝=未用），直观显示占用比例
		drawDiskCard(renderCtx, card, m_diskTotalBytes ? 100.f * static_cast<float>(m_diskUsedBytes) / static_cast<float>(m_diskTotalBytes) : 0.f,
		             m_diskUsedBytes, m_diskTotalBytes);
	}

	void FeaturesArea::drawDiskCard(const RenderContext& renderCtx, const D2D1_RECT_F& card,
	                                float usedPercent, std::uint64_t usedBytes, std::uint64_t totalBytes) const
	{
		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const float cardWidth = card.right - card.left;
		const float cardHeight = card.bottom - card.top;

		// 卡片底色（统一浅青蓝）
		solidBrush->SetColor(CARD_TINT);
		const D2D1_ROUNDED_RECT cardRounded = D2D1::RoundedRect(card, 8.f, 8.f);
		renderTarget->FillRoundedRectangle(cardRounded, solidBrush);

		// 顶部：标题（含盘符切换）+ 左侧主色竖条
		const float titleY = card.top + 12.f;
		solidBrush->SetColor(COLOR_DISK);
		renderTarget->FillRectangle(D2D1::RectF(card.left + 12.f, titleY, card.left + 15.f, titleY + TITLE_LINE_HEIGHT), solidBrush);
		solidBrush->SetColor(m_diskTitleHovered ? COLOR_DISK : D2D1::ColorF(0x6b7280));
		const std::wstring& title = m_diskDisplay;
		renderTarget->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()),
		                        app().textFormat().pTipsFormat,
		                        D2D1::RectF(card.left + 22.f, titleY, card.right - 8.f, titleY + TITLE_LINE_HEIGHT), solidBrush);
		// 标题下划线提示（悬停可点击切换盘符）
		if (m_diskTitleHovered)
		{
			solidBrush->SetColor(D2D1::ColorF(COLOR_DISK.r, COLOR_DISK.g, COLOR_DISK.b, 0.5f));
			renderTarget->DrawLine(D2D1::Point2F(card.left + 22.f, titleY + TITLE_LINE_HEIGHT + 1.f),
			                       D2D1::Point2F(card.left + 22.f + title.size() * 7.f, titleY + TITLE_LINE_HEIGHT + 1.f),
			                       solidBrush, 1.f);
		}

		// 环形饼图区域（卡片右下角，圆心下移到 0.70 高度避免与上方明细文字重叠）
		const float pieDiameter = std::min(cardWidth * 0.5f, cardHeight * 0.42f);
		const D2D1_POINT_2F pieCenter{card.right - pieDiameter * 0.5f - 10.f, card.top + cardHeight * 0.70f};
		const float pieRadius = pieDiameter * 0.5f;
		const float pieRing = std::max(8.f, pieRadius * 0.38f); // 环宽

		// 扇形角度参数（红色扇形与百分比标签共用）
		constexpr float PI = 3.14159265358979f;
		const float sweepRad = usedPercent * 0.01f * 2.f * PI; // 已用扇区扫过弧度
		const float startRad = -PI / 2.f;                      // 从 12 点方向开始
		const float midRad = startRad + sweepRad * 0.5f;       // 扇形中心角（角平分线）

		// 未用（蓝）整圆
		solidBrush->SetColor(COLOR_DISK_FREE);
		renderTarget->FillEllipse(D2D1::Ellipse(pieCenter, pieRadius, pieRadius), solidBrush);
		// 已用（红）扇形：从 12 点方向顺时针扫 usedPercent*3.6 度
		if (usedPercent > 0.5f)
		{
			ID2D1Factory* factory = nullptr;
			renderTarget->GetFactory(&factory);
			if (factory)
			{
				UniqueComPtr<ID2D1PathGeometry> sectorPath;
				if (SUCCEEDED(factory->CreatePathGeometry(&sectorPath)))
				{
					UniqueComPtr<ID2D1GeometrySink> sink;
					if (SUCCEEDED(sectorPath->Open(&sink)))
					{
						const float endRad = startRad + sweepRad;
						const D2D1_POINT_2F startPt{pieCenter.x + pieRadius * std::cos(startRad), pieCenter.y + pieRadius * std::sin(startRad)};
						const D2D1_POINT_2F endPt{pieCenter.x + pieRadius * std::cos(endRad), pieCenter.y + pieRadius * std::sin(endRad)};
						sink->BeginFigure(startPt, D2D1_FIGURE_BEGIN_FILLED);
						// 顺时针方向绘制弧，扫过角度超过 180° 时标记为大弧
						sink->AddArc(D2D1::ArcSegment(endPt, D2D1::SizeF(pieRadius, pieRadius), 0.f,
						                              D2D1_SWEEP_DIRECTION_CLOCKWISE,
						                              sweepRad > PI ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
						sink->AddLine(pieCenter);
						sink->EndFigure(D2D1_FIGURE_END_CLOSED);
						sink->Close();
						solidBrush->SetColor(COLOR_DISK_USED);
						renderTarget->FillGeometry(sectorPath.get(), solidBrush);
					}
				}
			}
		}

		// 内圈挖空（露出卡片底色，形成环形）
		solidBrush->SetColor(CARD_TINT);
		renderTarget->FillEllipse(D2D1::Ellipse(pieCenter, pieRadius - pieRing, pieRadius - pieRing), solidBrush);

		// 百分比标签：跟随已用红色扇区的中心（环带中心 + 角平分线方向），白描边 + 黑字
		const std::wstring percentText = std::format(L"{:.0f}%", usedPercent);
		const float labelRadius = pieRadius - pieRing * 0.5f; // 红色环带中心半径
		const D2D1_POINT_2F labelCenter{pieCenter.x + labelRadius * std::cos(midRad),
		                                pieCenter.y + labelRadius * std::sin(midRad)};
		const D2D1_RECT_F labelRc = D2D1::RectF(labelCenter.x - 32.f, labelCenter.y - 12.f,
		                                        labelCenter.x + 32.f, labelCenter.y + 12.f);
		IDWriteTextFormat* const boldFmt = app().textFormat().pBoldFormat.get();
		const auto oldAlign = boldFmt->GetTextAlignment();
		boldFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
		// 白色描边（8 方向偏移 1px 模拟轮廓）
		solidBrush->SetColor(D2D1::ColorF(0xffffff));
		for (int dx = -1; dx <= 1; ++dx)
		{
			for (int dy = -1; dy <= 1; ++dy)
			{
				if (dx == 0 && dy == 0)
				{
					continue;
				}
				renderTarget->DrawTextW(percentText.c_str(), static_cast<UINT32>(percentText.size()), boldFmt,
				                        D2D1::RectF(labelRc.left + dx, labelRc.top + dy, labelRc.right + dx, labelRc.bottom + dy),
				                        solidBrush);
			}
		}
		// 黑色文字本体
		solidBrush->SetColor(D2D1::ColorF(0x000000));
		renderTarget->DrawTextW(percentText.c_str(), static_cast<UINT32>(percentText.size()), boldFmt, labelRc, solidBrush);
		boldFmt->SetTextAlignment(oldAlign);

		// 中部：用量明细（小字两行显示，避免与饼图重叠）
		solidBrush->SetColor(D2D1::ColorF(0x1f2937));
		const std::wstring usedText = std::format(L"已用 {}", format_bytes(usedBytes));
		const std::wstring totalText = std::format(L"共 {}", format_bytes(totalBytes));
		const float detailY = card.top + 12.f + TITLE_LINE_HEIGHT + 6.f;
		renderTarget->DrawTextW(usedText.c_str(), static_cast<UINT32>(usedText.size()),
		                        app().textFormat().pTipsFormat,
		                        D2D1::RectF(card.left + 12.f, detailY, card.right - 12.f, detailY + DETAIL_LINE_HEIGHT), solidBrush);
		renderTarget->DrawTextW(totalText.c_str(), static_cast<UINT32>(totalText.size()),
		                        app().textFormat().pTipsFormat,
		                        D2D1::RectF(card.left + 12.f, detailY + DETAIL_LINE_HEIGHT, card.right - 12.f, detailY + DETAIL_LINE_HEIGHT * 2.f), solidBrush);
	}

	void FeaturesArea::onClick(const MouseEvent& e)
	{
		// 点击磁盘卡标题区域：循环切换下一个盘符
		// 注意：e.point 是 owner 窗口坐标系，须用 m_boundsInOwner 计算磁盘卡在 owner 中的位置
		const D2D1_RECT_F& owner = getBoundsInOwner();
		const float w = owner.right - owner.left;
		const float h = owner.bottom - owner.top;
		const float cardWidth = (w - PADDING * 2.f - CARD_GAP * 4.f) / 5.f;
		const D2D1_RECT_F diskCard = D2D1::RectF(owner.left + PADDING + 4.f * (cardWidth + CARD_GAP), owner.top + PADDING,
		                                         owner.left + PADDING + 4.f * (cardWidth + CARD_GAP) + cardWidth, owner.top + h - PADDING);
		if (e.point.x >= diskCard.left && e.point.x <= diskCard.right &&
		    e.point.y >= diskCard.top && e.point.y <= diskCard.top + 32.f)
		{
			if (!m_diskDrives.empty())
			{
				m_diskIndex = (m_diskIndex + 1) % m_diskDrives.size();
				m_diskHistory.clear(); // 切换盘后清空旧盘曲线，重新累计
				sampleAndUpdate();
			}
		}
	}

	void FeaturesArea::onMouseMove(const MouseEvent& e)
	{
		// 悬停磁盘卡标题区域高亮（提示可点击切换盘符）
		const D2D1_RECT_F& owner = getBoundsInOwner();
		const float w = owner.right - owner.left;
		const float h = owner.bottom - owner.top;
		const float cardWidth = (w - PADDING * 2.f - CARD_GAP * 4.f) / 5.f;
		const D2D1_RECT_F diskCard = D2D1::RectF(owner.left + PADDING + 4.f * (cardWidth + CARD_GAP), owner.top + PADDING,
		                                         owner.left + PADDING + 4.f * (cardWidth + CARD_GAP) + cardWidth, owner.top + h - PADDING);
		const bool overTitle = e.point.x >= diskCard.left && e.point.x <= diskCard.right &&
		                       e.point.y >= diskCard.top && e.point.y <= diskCard.top + 32.f;
		if (overTitle != m_diskTitleHovered)
		{
			m_diskTitleHovered = overTitle;
			update();
		}
	}

	void FeaturesArea::drawMetricCard(const RenderContext& renderCtx, const D2D1_RECT_F& card,
	                                  std::wstring_view title, const std::wstring& valueText,
	                                  const std::deque<float>& history, float maxValue, D2D1_COLOR_F accent) const
	{
		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const float cardWidth = card.right - card.left;
		const float cardHeight = card.bottom - card.top;

		// 卡片底色（统一浅青蓝）
		solidBrush->SetColor(CARD_TINT);
		const D2D1_ROUNDED_RECT cardRounded = D2D1::RoundedRect(card, 8.f, 8.f);
		renderTarget->FillRoundedRectangle(cardRounded, solidBrush);

		// 顶部：标题 + 左侧主色竖条
		const float titleY = card.top + 12.f;
		solidBrush->SetColor(accent);
		renderTarget->FillRectangle(D2D1::RectF(card.left + 12.f, titleY, card.left + 15.f, titleY + TITLE_LINE_HEIGHT), solidBrush);
		solidBrush->SetColor(D2D1::ColorF(0x6b7280));
		renderTarget->DrawTextW(title.data(), static_cast<UINT32>(title.size()),
		                        app().textFormat().pTipsFormat,
		                        D2D1::RectF(card.left + 22.f, titleY, card.right - 8.f, titleY + TITLE_LINE_HEIGHT), solidBrush);

		// 中部：大号数值
		if (!valueText.empty())
		{
			solidBrush->SetColor(D2D1::ColorF(0x1f2937));
			const float valueY = card.top + 12.f + TITLE_LINE_HEIGHT + 8.f;
			renderTarget->DrawTextW(valueText.c_str(), static_cast<UINT32>(valueText.size()),
			                        app().textFormat().pBoldFormat,
			                        D2D1::RectF(card.left + 12.f, valueY, card.right - 12.f, valueY + VALUE_LINE_HEIGHT), solidBrush);
		}

		// 底部：曲线图
		const float chartTop = card.bottom - 64.f;
		const D2D1_RECT_F chartArea = D2D1::RectF(card.left + 12.f, chartTop, card.right - 12.f, card.bottom - 10.f);
		drawChart(renderCtx, chartArea, history, maxValue, accent);
	}

	void FeaturesArea::drawChart(const RenderContext& renderCtx, const D2D1_RECT_F& area,
	                             const std::deque<float>& history, float maxValue, D2D1_COLOR_F accent) const
	{
		const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
		const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
		const float chartWidth = area.right - area.left;
		const float chartHeight = area.bottom - area.top;

		if (history.empty() || chartWidth < 2.f || chartHeight < 2.f || maxValue <= 0.f)
		{
			return;
		}

		// 浅色网格线（25% / 50% / 75% 三条水平基准线）
		solidBrush->SetColor(D2D1::ColorF(0x000000, 0.06f));
		for (int i = 1; i <= 3; ++i)
		{
			const float y = area.top + chartHeight * i / 4.f;
			renderTarget->DrawLine(D2D1::Point2F(area.left, y), D2D1::Point2F(area.right, y), solidBrush, 1.f);
		}

		// 曲线：将历史点映射到图表区域，逐段连线
		const size_t count = history.size();
		const float stepX = chartWidth / static_cast<float>(HISTORY_SIZE - 1);
		ID2D1Factory* factory = nullptr;
		renderTarget->GetFactory(&factory);
		UniqueComPtr<ID2D1PathGeometry> path;
		if (factory && SUCCEEDED(factory->CreatePathGeometry(&path)))
		{
			UniqueComPtr<ID2D1GeometrySink> sink;
			if (SUCCEEDED(path->Open(&sink)))
			{
				bool first = true;
				for (size_t i = 0; i < count; ++i)
				{
					const float x = area.right - (count - 1 - i) * stepX;
					const float ratio = std::clamp(history[i] / maxValue, 0.f, 1.f);
					const float y = area.bottom - ratio * chartHeight;
					if (first)
					{
						sink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_HOLLOW);
						first = false;
					}
					else
					{
						sink->AddLine(D2D1::Point2F(x, y));
					}
				}
				sink->EndFigure(D2D1_FIGURE_END_OPEN);
				sink->Close();
				UniqueComPtr<ID2D1SolidColorBrush> strokeBrush;
				if (SUCCEEDED(renderTarget->CreateSolidColorBrush(accent, &strokeBrush)))
				{
					renderTarget->DrawGeometry(path.get(), strokeBrush.get(), 2.f);
				}
			}
		}

		// 曲线下渐变填充（仅在有数据时绘制，增强可读性）
		if (count >= 2)
		{
			UniqueComPtr<ID2D1LinearGradientBrush> gradient;
			const D2D1_GRADIENT_STOP stops[2] = {
				{0.f, D2D1::ColorF(accent.r, accent.g, accent.b, 0.38f)},
				{1.f, D2D1::ColorF(accent.r, accent.g, accent.b, 0.05f)},
			};
			UniqueComPtr<ID2D1GradientStopCollection> stopCollection;
			if (SUCCEEDED(renderTarget->CreateGradientStopCollection(stops, 2, &stopCollection)))
			{
				if (SUCCEEDED(renderTarget->CreateLinearGradientBrush(
					    D2D1::LinearGradientBrushProperties(D2D1::Point2F(area.left, area.bottom), D2D1::Point2F(area.left, area.top)),
					    stopCollection.get(), &gradient)))
				{
					UniqueComPtr<ID2D1PathGeometry> fillPath;
					if (factory && SUCCEEDED(factory->CreatePathGeometry(&fillPath)))
					{
						UniqueComPtr<ID2D1GeometrySink> fillSink;
						if (SUCCEEDED(fillPath->Open(&fillSink)))
						{
							const float stepXF = chartWidth / static_cast<float>(HISTORY_SIZE - 1);
							bool firstF = true;
							for (size_t i = 0; i < count; ++i)
							{
								const float x = area.right - (count - 1 - i) * stepXF;
								const float ratio = std::clamp(history[i] / maxValue, 0.f, 1.f);
								const float y = area.bottom - ratio * chartHeight;
								if (firstF)
								{
									fillSink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_FILLED);
									firstF = false;
								}
								else
								{
									fillSink->AddLine(D2D1::Point2F(x, y));
								}
							}
							fillSink->AddLine(D2D1::Point2F(area.right, area.bottom));
							fillSink->AddLine(D2D1::Point2F(area.left, area.bottom));
							fillSink->EndFigure(D2D1_FIGURE_END_CLOSED);
							fillSink->Close();
							renderTarget->FillGeometry(fillPath.get(), gradient.get());
						}
					}
				}
			}
		}
	}
}
