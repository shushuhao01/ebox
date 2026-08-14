export module UI.FeaturesArea;

import "sys_defs.h";
import std;
import UI.Core;
import Scheduler;

namespace ui
{
	export class FeaturesArea final : public ControlBase
	{
	public:
		template <typename... Args>
		explicit FeaturesArea(Args&&... args) noexcept : ControlBase(std::forward<Args>(args)...)
		{
			initialize();
		}

	private:
		void initialize();
		void sampleAndUpdate();

	protected:
		virtual void onResize(float width, float height) override;

	private:
		virtual void drawImpl(const RenderContext& renderCtx) override;
		void drawMetricCard(const RenderContext& renderCtx, const D2D1_RECT_F& card,
		                    std::wstring_view title, const std::wstring& valueText,
		                    const std::deque<float>& history, float maxValue, D2D1_COLOR_F accent) const;
		// 磁盘卡：环形饼图（红=已用 / 蓝=未用），标题带盘符切换
		void drawDiskCard(const RenderContext& renderCtx, const D2D1_RECT_F& card,
		                  float usedPercent, std::uint64_t usedBytes, std::uint64_t totalBytes) const;
		// 磁盘卡右下角"清理垃圾"按钮：弹确认框后启动全盘垃圾清理脚本
		void onCleanBtnClick();
		void drawChart(const RenderContext& renderCtx, const D2D1_RECT_F& area,
		               const std::deque<float>& history, float maxValue, D2D1_COLOR_F accent) const;

	protected:
		// 鼠标：点击磁盘卡标题区域切换盘符
		virtual void onClick(const MouseEvent& e) override;
		virtual void onMouseMove(const MouseEvent& e) override;
		virtual void onMouseLeave(const MouseEvent& e) override;

	private:
		// 采样周期（秒）与保留的历史点数
		static constexpr std::chrono::seconds SAMPLE_INTERVAL{1};
		static constexpr size_t HISTORY_SIZE = 90;

		std::stop_source m_timerStopSource;

		// 各指标历史曲线数据（最新在尾部）
		std::deque<float> m_cpuHistory;
		std::deque<float> m_memHistory;
		std::deque<float> m_gpuHistory;
		std::deque<float> m_vramHistory;

		// 当前读数
		float m_cpuPercent{0.f};
		float m_memPercent{0.f};
		float m_gpuPercent{0.f};
		float m_vramPercent{0.f};
		std::uint64_t m_memUsedBytes{0};
		std::uint64_t m_memTotalBytes{0};
		std::uint64_t m_vramUsedBytes{0};
		std::uint64_t m_vramTotalBytes{0};
		std::uint64_t m_diskUsedBytes{0};
		std::uint64_t m_diskTotalBytes{0};

		// 磁盘卡：当前盘符索引 + 可选盘符列表（如 C:\ D:\ E:\）+ 各盘曲线历史
		std::vector<std::wstring> m_diskDrives;
		size_t m_diskIndex{0};
		std::wstring m_diskDisplay;   // 标题栏显示文本（如 "D盘使用"）
		std::deque<float> m_diskHistory;
		bool m_diskTitleHovered{false};
		bool m_cleanHovered{false};   // 磁盘卡右下角"清理垃圾"按钮悬停

		// GPU/显存数据源是否可用（PDH 计数器不存在时降级为“--”）
		bool m_gpuAvailable{false};
		bool m_vramAvailable{false};

		// 上次 CPU 采样时间点
		std::uint64_t m_lastIdleTicks{0};
		std::uint64_t m_lastKernelTicks{0};
		std::uint64_t m_lastUserTicks{0};
		bool m_cpuValid{false};
	};
}
