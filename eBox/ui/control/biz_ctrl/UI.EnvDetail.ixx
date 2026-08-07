export module UI.EnvDetail;

import "sys_defs.h";
import std;
import UI.Core;
import UI.Button;
import UI.ScrollBar;
import UI.EnvLogPanel;
import UI.EnvInfoPanel;
import Biz.Core;

namespace ui
{
	// 三卡片手风琴：进程记录 / 环境信息 / 环境日志 互斥展开，同一时刻只有一个展开
	export enum class ExpandArea
	{
		Process,
		EnvInfo,
		Log,
	};

	export class ProcessList final : public ControlBase
	{
	public:
		template <typename... Args>
		explicit ProcessList(Args&&... args) noexcept : ControlBase(std::forward<Args>(args)...)
		{
			initialize();
		}

	private:
		void initialize();

	public:
		bool hasEnv() const noexcept { return m_env != nullptr; }
		const std::shared_ptr<biz::Env>& getEnv() const noexcept { return m_env; }
		void setEnv(const std::shared_ptr<biz::Env>& env);
		void clearEnv();
		void procCountChange(biz::Env::EProcEvent e, const std::shared_ptr<biz::ProcessInfo>& proc);
		bool hasAnyProcesses() const noexcept { return m_processes.size() > 0; }

	protected:
		virtual void onResize(float width, float height) override;
		virtual void onMouseEnter(const MouseEvent& e) override;
		virtual void onMouseLeave(const MouseEvent& e) override;
		virtual void onMouseWheel(const MouseWheelEvent& e) override;

	private:
		virtual void drawImpl(const RenderContext& renderCtx) override;

	private:
		void updateAllItemPos();

	private:
		ScrollBar m_scrollBar{this};
		bool m_isHovered = false;

	private:
		std::shared_ptr<biz::Env> m_env;

		struct ListItem
		{
			std::shared_ptr<biz::ProcessInfo> process;
			D2D1_RECT_F rect;
		};

		std::unordered_map<std::uint32_t, ListItem> m_processes;
		std::vector<ListItem*> m_processesToDraw;
	};

	export class EnvDetail final : public ControlBase
	{
	public:
		template <typename... Args>
		explicit EnvDetail(Args&&... args) noexcept : ControlBase(std::forward<Args>(args)...)
		{
			initialize();
		}

	private:
		void initialize();

	public:
		ProcessList& getProcessList() { return m_processList; }
		// 选中环境：进程列表 / 环境日志 / 环境信息 一并联动
		void setEnv(const std::shared_ptr<biz::Env>& env);
		void clearEnv();
		bool hasDetail() const noexcept { return m_processList.hasEnv(); }

	protected:
		virtual void onResize(float width, float height) override;

	private:
		virtual void drawImpl(const RenderContext& renderCtx) override;

	private:
		void setProcPath(std::wstring_view path);
		void onLaunchBtnClick();
		// 手风琴：切换展开区域，其余卡片自动收起
		void setExpandArea(ExpandArea area);

	private:
		UniqueComPtr<IDWriteTextLayout> m_procPathTextLayout;
		std::wstring m_strProcPath;
		float m_pathTextHeight{0};
		UniqueComPtr<IDWriteTextLayout> m_noProcTextLayout;
		float m_noProcTextHeight{0};
		// 当前选中环境名称（显示在"进程记录"标题前）
		std::wstring m_selectedEnvName;
		// 环境名称实际文本宽度（加粗显示，按测量值布局，路径框动态后移）
		float m_envNameWidth{0.f};
		Button m_btnClear{this};
		Button m_btnLaunch{this};
		// 进程卡片“折叠/展开”按钮：折叠即把展开区让给环境信息/环境日志
		Button m_btnCollapse{this};
		ExpandArea m_expandArea{ExpandArea::Process};
		ProcessList m_processList{this};
		// 中间：环境信息折叠卡片（默认折叠，展示当前环境的设备指纹信息）
		EnvInfoPanel m_infoPanel{this};
		// 底部：环境日志折叠卡片（默认折叠，记录该环境 24h 内一切动作）
		EnvLogPanel m_logPanel{this};
	};
}
