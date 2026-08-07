export module UI.RightContent;

import "sys_defs.h";
import std;
import UI.Core;
import UI.Button;
import UI.ScrollBar;
import UI.FeaturesArea;
export import UI.EnvDetail;
import Biz.Core;

namespace ui
{
	export class RightContent final : public ControlBase
	{
	public:
		template <typename... Args>
		explicit RightContent(Args&&... args) noexcept : ControlBase(std::forward<Args>(args)...)
		{
			initialize();
		}

	private:
		void initialize();

	public:
		EnvDetail& getEnvDetail() { return m_envDetail; }
		ProcessList& getProcessList() { return m_envDetail.getProcessList(); }

	protected:
		virtual void onResize(float width, float height) override;
		virtual void onMouseWheel(const MouseWheelEvent& e) override;

	private:
		virtual void drawImpl(const RenderContext& renderCtx) override;

	private:
		// 使用事项逐条文本布局（空态页面列表项）
		std::vector<UniqueComPtr<IDWriteTextLayout>> m_tipLayouts;
		float m_contentTop{0};
		// 公告栏
		D2D1_RECT_F m_bannerRect{};
		bool m_bannerVisible{true};
		bool m_bannerCollapsed{false};
		std::unique_ptr<Button> m_btnBannerToggle;
		std::unique_ptr<Button> m_btnBannerClose;
		FeaturesArea m_featuresArea{this};
		EnvDetail m_envDetail{this};
		// 空态使用事项卡片滚动条（内容超出可视区时支持滑动查看）
		std::unique_ptr<ScrollBar> m_tipsScrollBar;
	};
}
