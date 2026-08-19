export module UI.EnvLogPanel;

import "sys_defs.h";
import std;
import UI.Core;
import UI.Button;
import UI.ScrollBar;
import EnvLog;
import Biz.Core;

namespace ui
{
	// 环境日志折叠卡片：默认折叠，点击标题栏展开；展开后显示环境最近 24h 动作日志，
	// 支持滚动查看与“一键复制”全部日志；离线后历史日志仍保留（从磁盘恢复）。
	export class EnvLogPanel final : public ControlBase
	{
	public:
		template <typename... Args>
		explicit EnvLogPanel(Args&&... args) noexcept : ControlBase(std::forward<Args>(args)...)
		{
			initialize();
		}

	public:
		// 绑定当前选中的环境（index 对应的日志）；envIndex 无效则显示“未选择环境”
		void setEnvIndex(std::uint32_t envIndex);
		// 设置环境显示名称（改名后的名称；与左侧环境卡片一致，未改名时为默认名）
		void setEnvName(std::wstring_view envName);
		void clearEnv();
		bool hasEnv() const noexcept { return m_hasEnv; }
		bool isExpanded() const noexcept { return m_expanded; }
		void setExpanded(bool expanded);
		// 展开时日志区可占用的最大高度（由外层按可用空间动态设置）
		void setMaxExpandHeight(float maxHeight);
		// 该面板当前希望占据的高度（折叠=标题栏，展开=标题栏+日志区）
		float getDesiredHeight() const;
		// 点击标题栏请求展开/收起（交给外层统一做手风琴互斥）
		std::function<void(bool)> onExpandRequest;

	protected:
		virtual void onResize(float width, float height) override;
		virtual void onMouseEnter(const MouseEvent& e) override;
		virtual void onMouseLeave(const MouseEvent& e) override;
		virtual void onMouseWheel(const MouseWheelEvent& e) override;
		virtual void onClick(const MouseEvent& e) override;

	private:
		virtual void drawImpl(const RenderContext& renderCtx) override;

	private:
		void initialize();
		void toggleExpand();
		void copyAllLogs();
		void clearLogs();
		void refreshLogs();
		void onToggle();
		void onCopy();
		float getHeaderHeight() const;
		float getLogAreaTop() const;
		float getLogLineHeight() const;

	private:
		Button m_btnToggle{this};
		Button m_btnCopy{this};
		Button m_btnClear{this};
		ScrollBar m_scrollBar{this};
		bool m_isHovered{false};
		bool m_expanded{false};
		bool m_hasEnv{false};
		float m_maxExpandHeight{180.f};
		std::uint32_t m_envIndex{0};
		std::wstring m_envName;

		// 渲染用日志行缓存（展开时刷新）
		std::vector<biz::EnvLogEntry> m_logs;
		std::vector<std::wstring> m_logLines;
		// 日志版本号与条数缓存：日志无新增时跳过 O(500) 的格式化重建（每秒心跳重绘不再重复拉日志）
		std::uint64_t m_lastAppendVersion{std::numeric_limits<std::uint64_t>::max()};
		std::size_t m_lastLogCount{0};
	};
}
