export module UI.Page:Home;

import "sys_defs.h";
import std;
import UI.Core;
export import UI.LeftSidebar;
import UI.RightContent;
import Biz.Core;

namespace ui
{
	namespace
	{
		constexpr float splitterWidth = 6.f;

		// 左右分割线：可拖动以调整左侧环境区域宽度
		class SplitterBar final : public ControlBase
		{
		public:
			using OnDrag = std::function<void(float deltaX)>;

			explicit SplitterBar(WindowBase* owner, OnDrag onDrag)
				: ControlBase(owner), m_pfnOnDrag(std::move(onDrag))
			{
			}

		protected:
			bool wantsResizeCursor() const noexcept override { return true; }

			void onMouseDown(const MouseEvent& e) override
			{
				if (e.button == MouseEvent::ButtonType::Left)
				{
					m_bDragging = true;
					m_lastX = e.point.x;
					SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
				}
			}

			void onMouseMove(const MouseEvent& e) override
			{
				SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
				if (m_bDragging && m_pfnOnDrag)
				{
					m_pfnOnDrag(e.point.x - m_lastX);
					m_lastX = e.point.x;
				}
			}

			void onMouseUp(const MouseEvent& e) override
			{
				m_bDragging = false;
			}

			void onMouseLeave(const MouseEvent& e) override
			{
				m_bDragging = false;
			}

		private:
			void drawImpl(const RenderContext& renderCtx) override
			{
				const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
				const UniqueComPtr<ID2D1SolidColorBrush>& solidBrush = renderCtx.brush;
				const auto drawSize = size();
				// 整个分割条填充灰色背景，避免透出窗口底色（透明/黑色），保证环境卡片区域与进程区域之间是灰色分割线
				solidBrush->SetColor(D2D1::ColorF(0xe8eaed));
				renderTarget->FillRectangle(D2D1::RectF(0.f, 0.f, drawSize.width, drawSize.height), solidBrush);
				const float cx = drawSize.width * 0.5f;
				solidBrush->SetColor(D2D1::ColorF(0xd5d8dd));
				renderTarget->DrawLine(D2D1::Point2F(cx, 0.f), D2D1::Point2F(cx, drawSize.height), solidBrush, 1.f);
			}

		private:
			OnDrag m_pfnOnDrag;
			bool m_bDragging{false};
			float m_lastX{0.f};
		};
	}

	export class HomePage final : public RendererInterface
	{
	public:
		static constexpr float sidebarWidth = 280.0f;

		explicit HomePage(WindowBase* owner) : m_ownerWnd(owner)
		{
			owner->addRenderer(this);

			m_leftSidebar = std::make_unique<LeftSidebar>(owner);
			m_rightContent = std::make_unique<RightContent>(owner);
			m_splitterBar = std::make_unique<SplitterBar>(owner, [this](float deltaX)
			{
				setSidebarWidth(m_sidebarWidth + deltaX);
			});

			m_leftSidebar->getEnvBoxCardArea()->setOnSelect([this](const std::shared_ptr<biz::Env>& env, bool bSelected)
			{
				if (bSelected)
				{
					m_rightContent->getEnvDetail().setEnv(env);
				}
				else
				{
					m_rightContent->getEnvDetail().clearEnv();
				}
				m_rightContent->getEnvDetail().update();
			});
			m_leftSidebar->getEnvBoxCardArea()->setOnProcCountChange([this](biz::Env::EProcEvent e, const std::shared_ptr<biz::ProcessInfo>& p)
			{
				m_rightContent->getProcessList().procCountChange(e, p);
			});
		}

		virtual ~HomePage()
		{
			m_ownerWnd->removeRenderer(this);
		}

		void setMargins(const D2D1_RECT_F& margins)
		{
			m_frameMargins = margins;
		}

		float getSidebarWidth() const noexcept { return m_sidebarWidth; }

		void setSidebarWidth(float width)
		{
			const float availWidth = m_size.width - m_frameMargins.left - m_frameMargins.right;
			// 左侧最小 160，右侧进程区域至少保留 400
			const float maxSidebar = std::max(160.f, availWidth - 400.f);
			m_sidebarWidth = std::clamp(width, 160.f, maxSidebar);
			updateLayout();
		}

		virtual void onResize(float width, float height) override
		{
			m_size = {width, height};
			updateLayout();
		}

	private:
		void updateLayout()
		{
			const float sbLeft = m_frameMargins.left;
			m_leftSidebar->setBounds(D2D1::RectF(sbLeft, m_frameMargins.top,
			                                     sbLeft + m_sidebarWidth, m_size.height - m_frameMargins.bottom));
			m_splitterBar->setBounds(D2D1::RectF(sbLeft + m_sidebarWidth, m_frameMargins.top,
			                                     sbLeft + m_sidebarWidth + splitterWidth, m_size.height - m_frameMargins.bottom));
			m_rightContent->setBounds(D2D1::RectF(sbLeft + m_sidebarWidth + splitterWidth, m_frameMargins.top,
			                                      m_size.width - m_frameMargins.right, m_size.height - m_frameMargins.bottom));
		}

	public:
		virtual void draw(const RenderContext& renderCtx) override
		{
			const UniqueComPtr<ID2D1HwndRenderTarget>& renderTarget = renderCtx.renderTarget;
			D2D1_RECT_F rect = D2D1::RectF(0.f, 0.f, m_size.width, m_size.height);
			rect.left += m_frameMargins.left;
			rect.top += m_frameMargins.top;
			rect.right -= m_frameMargins.right;
			rect.bottom -= m_frameMargins.bottom;
			renderTarget->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

			m_rightContent->draw(renderCtx);

			draw_box_shadow(renderCtx, m_leftSidebar->getBounds(), {.offset = D2D1::Point2F(0.f, 1.f)});
			m_leftSidebar->draw(renderCtx);
			m_splitterBar->draw(renderCtx);

			renderTarget->PopAxisAlignedClip();
		}

		LeftSidebar* getLeftSidebar() const { return m_leftSidebar.get(); }

	private:
		WindowBase* m_ownerWnd{nullptr};
		D2D1_RECT_F m_frameMargins{};
		D2D1_SIZE_F m_size{};
		float m_sidebarWidth{sidebarWidth};
		std::unique_ptr<LeftSidebar> m_leftSidebar;
		std::unique_ptr<RightContent> m_rightContent;
		std::unique_ptr<SplitterBar> m_splitterBar;
	};
}
