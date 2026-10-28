export module UI.StartAppDiv;

import "sys_defs.h";
import std;
import UI.Core;
import UI.Button;

namespace ui
{
	export class StartAppDiv final : public ControlBase
	{
	public:
		template <typename... Args>
		explicit StartAppDiv(Args&&... args) noexcept : ControlBase(std::forward<Args>(args)...)
		{
			initialize();
		}

		using UpdateBoundsFunc = std::function<void()>;
		void setUpdateBounds(UpdateBoundsFunc fn) noexcept { m_updateBoundsFunc = std::move(fn); }

		using LaunchProcessFunc = std::function<void(const std::wstring&)>;
		void setLaunchProcess(LaunchProcessFunc fn) noexcept { m_launchProcessFunc = std::move(fn); }

		float getPathAreaHeight() const;

		std::wstring_view getCurrentSelectedPath() const { return m_strExePath; }

		void launchFile(std::wstring_view path);

	private:
		void initialize();

	protected:
		virtual void onResize(float width, float height) override;

	private:
		void updateBoundsWhenPathChanged();
		// 按控件实际宽度重建路径文本布局（换行后的行高决定路径区高度）
		void rebuildPathLayout(float width);
		virtual void drawImpl(const RenderContext& renderCtx) override;

	private:
		void onBtnStartPressed();

	private:
		UniqueComPtr<IDWriteTextLayout> m_pExePathTextLayout;
		std::wstring m_strExePath;
		float m_pathTextHeight{0};
		// 当前路径布局所依据的控件宽度；-1 表示尚未按真实宽度布局过
		float m_layoutWidth{-1.f};
		std::unique_ptr<Button> m_btnClear;
		std::unique_ptr<Button> m_btnStart;
		UpdateBoundsFunc m_updateBoundsFunc;
		LaunchProcessFunc m_launchProcessFunc;
	};
}
