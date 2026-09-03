export module UI.RightContent;

import "sys_defs.h";
import std;
import UI.Core;
import UI.Button;
import UI.ScrollBar;
import UI.FeaturesArea;
export import UI.EnvDetail;
import Biz.Core;
import biz.LicenseServerClient;

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
		// 刷新服务端系统公告（显示最新一条；心跳线程收到公告后由主窗口调用）
		void refreshNotice();

	protected:
		virtual void onResize(float width, float height) override;
		virtual void onMouseWheel(const MouseWheelEvent& e) override;
		virtual void onClick(const MouseEvent& e) override;

	private:
		virtual void drawImpl(const RenderContext& renderCtx) override;
		// 按当前公告文本重建公告栏文本布局（超宽省略号，单行）
		void rebuildBannerLayout();
		// 由用户须知第6条协议名命中区域分派弹窗
		void openAgreementDialogFromNotice(D2D1_POINT_2F localPt);

	private:
		// 使用事项逐条文本布局（空态页面列表项）
		std::vector<UniqueComPtr<IDWriteTextLayout>> m_tipLayouts;
		// 用户须知逐条文本布局（按换行后实际高度排布，避免文字重叠）
		std::vector<UniqueComPtr<IDWriteTextLayout>> m_noticeLayouts;
		float m_contentTop{0};
		// 用户须知正文可视区（控件局部坐标）与其内容/可视高度
		D2D1_RECT_F m_noticeBodyRect{};
		float m_noticeTotalHeight{0.f};
		float m_noticeBodyHeight{0.f};
		// 第6条《用户协议》《隐私协议》子串字符范围（用于下划线高亮与点击命中）
		DWRITE_TEXT_RANGE m_noticeUARange{};
		DWRITE_TEXT_RANGE m_noticePARange{};
		// 公告栏
		D2D1_RECT_F m_bannerRect{};
		bool m_bannerVisible{true};
		bool m_bannerCollapsed{false};
		std::wstring m_noticeText;                       // 服务端系统公告（最新一条，空=无公告）
		UniqueComPtr<IDWriteTextLayout> m_bannerLayout;  // 公告文本布局（单行省略号）
		std::unique_ptr<Button> m_btnBannerToggle;
		std::unique_ptr<Button> m_btnBannerClose;
		FeaturesArea m_featuresArea{this};
		EnvDetail m_envDetail{this};
		// 空态使用事项卡片滚动条（内容超出可视区时支持滑动查看）
		std::unique_ptr<ScrollBar> m_tipsScrollBar;
		// 空态用户须知卡片滚动条（内容超出可视区时支持滑动查看）
		std::unique_ptr<ScrollBar> m_noticesScrollBar;
	};
}
