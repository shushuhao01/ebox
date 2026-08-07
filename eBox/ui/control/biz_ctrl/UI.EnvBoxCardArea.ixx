export module UI.EnvBoxCardArea;

import std;
import UI.Core;
import UI.EnvBoxCard;
import UI.ScrollBar;
import Biz.Core;
import Coroutine;

namespace ui
{
	export class EnvBoxCardArea final : public ControlBase
	{
	public:
		static constexpr float shadowSize = 6.f;
		static constexpr float shadowOffsetY = 4.f;
		static constexpr float scrollWidth = 8.f;
		static constexpr float scrollAreaWidth = scrollWidth + 8.f;

		template <typename... Args>
		explicit EnvBoxCardArea(Args&&... args) noexcept : ControlBase(std::forward<Args>(args)...)
		{
			initialize();
		}

		virtual ~EnvBoxCardArea();

		bool isNoEnvs() const noexcept { return m_envs.empty(); }
		std::shared_ptr<biz::Env> selectSuitableEnvAndSetItBusyTemp(const std::wstring& procFullPath);
		void launchProcess(const std::wstring& procFullPath, std::wstring_view params = L"");
		// “启动新进程”：每次都新建独立环境运行，用于多开（每个账号一个隔离环境）
		void launchProcessInNewEnv(const std::wstring& procFullPath, std::wstring_view params = L"");
		// 编程选中指定环境（右侧进程区联动显示该环境）
		void selectEnvByIndex(std::uint32_t index);
		bool hasAnyProcesses() const;

		using OnSelected = std::function<void(const std::shared_ptr<biz::Env>&, bool)>;
		void setOnSelect(OnSelected fn) { m_pfnOnSelect = std::move(fn); }

		using OnProcCountChange = std::function<void(biz::Env::EProcEvent, const std::shared_ptr<biz::ProcessInfo>&)>;
		void setOnProcCountChange(OnProcCountChange fn) { m_pfnOnProcCountChange = std::move(fn); }

		// 环境汇总（总数/在线数）以及变化通知（用于左侧“环境 x 个，在线 x 个”实时刷新）
		std::size_t getEnvCount() const { return m_envs.size(); }
		std::size_t getOnlineEnvCount() const;
		using OnSummaryChange = std::function<void()>;
		void setOnSummaryChange(OnSummaryChange fn) { m_pfnOnSummaryChange = std::move(fn); }

		// 视图模式：卡片（默认） / 紧凑列表
		EViewMode getViewMode() const noexcept { return m_viewMode; }
		void setViewMode(EViewMode mode);

	protected:
		virtual void onResize(float width, float height) override;
		virtual void onMouseEnter(const MouseEvent& e) override;
		virtual void onMouseLeave(const MouseEvent& e) override;
		virtual void onMouseWheel(const MouseWheelEvent& e) override;

	private:
		void initialize();
		coro::LazyTask<void> onEnvCountChange(biz::EnvManager::EChangeType changeType, std::shared_ptr<biz::Env> env);
		void addEnv(const std::shared_ptr<biz::Env>& env, bool initialIdle = false);
		void removeEnv(std::uint32_t envIndex);
		void onEnvSelected(EnvBoxCard* card, bool bSelected);
		void updateAllEnvPos();
		float getCardHeight() const;
		float getItemHeight() const;

		// 长按拖拽排序
		void onDragStart(EnvBoxCard* card, float ownerX, float ownerY);
		void onDragMove(EnvBoxCard* card, float ownerX, float ownerY);
		void onDragEnd(EnvBoxCard* card, bool cancelled);
		void loadEnvOrder();
		void saveEnvOrder();

	private:
		virtual void drawImpl(const RenderContext& renderCtx) override;

	private:
		coro::AsyncScope m_asyncScope;

	private:
		std::map<std::uint32_t, std::unique_ptr<EnvBoxCard>> m_envs;
		std::vector<EnvBoxCard*> m_envsToDraw;
		EnvBoxCard* m_currentSelectedEnv{nullptr};
		OnSelected m_pfnOnSelect;
		OnProcCountChange m_pfnOnProcCountChange;
		OnSummaryChange m_pfnOnSummaryChange;
		EViewMode m_viewMode{EViewMode::Card};
		std::unique_ptr<ScrollBar> m_scrollBar;
		bool m_isHovered = false;
		// 通过"启动新进程/总启动"主动新建环境后，自动选中该环境（右侧联动显示）
		bool m_bAutoSelectNextNewEnv{false};

		// 环境卡片显示顺序（按环境 index），用于长按拖拽排序
		std::vector<std::uint32_t> m_displayOrder;
		EnvBoxCard* m_dragCard{nullptr};
		float m_dragGrabOffsetY{0.f};
		float m_dragCurY{0.f};
		bool m_dragging{false};
	};
}
