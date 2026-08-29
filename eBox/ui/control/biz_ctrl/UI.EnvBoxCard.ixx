export module UI.EnvBoxCard;

import "sys_defs.h";
import std;
import UI.Core;
import UI.Button;
import Coroutine;
import Env;

namespace ui
{
	// 环境列表展示模式：卡片（默认） / 紧凑列表（一屏显示更多环境）
	export enum class EViewMode
	{
		Card,
		List,
	};

	export class EnvBoxCard final : public ControlBase
	{
	public:
		template <typename... Args>
		explicit EnvBoxCard(bool initialIdle, Args&&... args) noexcept : ControlBase(std::forward<Args>(args)...)
		                                                                 , m_bIdle(initialIdle)
		{
			initialize();
		}

		virtual ~EnvBoxCard();

	public:
		void setEnv(const std::shared_ptr<biz::Env>& env);
		std::shared_ptr<biz::Env> getEnv() const { return m_env; }
		bool isHovered() const noexcept { return m_isHovered; }
		bool isIdle() const noexcept { return m_bIdle; }
		bool isEmpty() const noexcept { return m_env->getAllProcessesCount() == 0; }
		bool contains(const std::wstring& procFullPath) const { return m_env->contains(procFullPath); }
		void setBusyTemp();
		void setViewMode(EViewMode mode);

		using OnSelected = std::function<void(bool)>;
		void setOnSelect(OnSelected fn) { m_pfnOnSelect = std::move(fn); }
		void programmaticDeselect();

		using OnProcCountChange = std::function<void(biz::Env::EProcEvent, const std::shared_ptr<biz::ProcessInfo>&)>;
		void setOnProcCountChange(OnProcCountChange fn) { m_pfnOnProcCountChange = std::move(fn); }

		// 独立于选中状态、始终触发的汇总刷新回调（用于左侧“环境 x 个，在线 x 个”实时刷新）
		using OnSummaryChange = std::function<void()>;
		void setOnSummaryChange(OnSummaryChange fn) { m_pfnOnSummaryChange = std::move(fn); }

		// 长按拖拽排序回调（由宿主 EnvBoxCardArea 注入）
		using OnDragStart = std::function<void(EnvBoxCard* card, float ownerX, float ownerY)>;
		using OnDragMove = std::function<void(EnvBoxCard* card, float ownerX, float ownerY)>;
		using OnDragEnd = std::function<void(EnvBoxCard* card, bool cancelled)>;
		void setOnDragCallbacks(OnDragStart onStart, OnDragMove onMove, OnDragEnd onEnd)
		{
			m_pfnOnDragStart = std::move(onStart);
			m_pfnOnDragMove = std::move(onMove);
			m_pfnOnDragEnd = std::move(onEnd);
		}

	private:
		void initialize();
		void updateNameLayout();

	protected:
		virtual void onResize(float width, float height) override;
		virtual void onMouseEnter(const MouseEvent& e) override;
		virtual void onMouseLeave(const MouseEvent& e) override;
		virtual void onMouseDown(const MouseEvent& e) override;
		virtual void onMouseMove(const MouseEvent& e) override;
		virtual void onMouseUp(const MouseEvent& e) override;
		virtual void onClick(const MouseEvent& e) override;

	private:
		virtual void drawImpl(const RenderContext& renderCtx) override;

	private:
		coro::LazyTask<void> resetToIdleLater();
		coro::LazyTask<void> onProcessCountChange(biz::Env::EProcEvent e, std::shared_ptr<biz::ProcessInfo> proc, std::size_t count);
		coro::LazyTask<void> tickRuntime();
		// 首次启动（新建环境）期间每秒刷新，驱动“首次初始化中”提示的重绘与自动消失
		coro::LazyTask<void> tickFirstLaunch();
		// 将本环境已运行应用的主窗口置前（多环境多实例时可区分每个环境的窗口）；
		// 找到有效窗口并成功置前返回 true，否则返回 false（此时应走启动流程）
		bool activateEnvWindows();
		// 针对“隐藏到托盘(SW_HIDE)”的主窗口：不强制显示（避免触发 WXWork 自绘黑屏），
		// 而是等待 WXWork 自行把主窗显示出来后立即置前；限时未显示则放弃（保持不黑屏）。
		coro::LazyTask<void> activateHiddenWhenVisible(HWND wnd);

	private:
		std::unique_ptr<Button> m_btnStart;
		std::unique_ptr<Button> m_btnRename;
		std::unique_ptr<Button> m_btnClose;
		EViewMode m_viewMode{EViewMode::Card};
		UniqueComPtr<IDWriteTextFormat> m_pListIconFormat;
		UniqueComPtr<IDWriteTextLayout> m_pNameLayout;
		float m_nameAreaWidth{0.f};
		bool m_bNameOverflow{false};
		bool m_isHovered = false;
		bool m_isSelected = false;
		bool m_isBright = false;
		coro::AsyncScope m_asyncScope;

	private:
		std::shared_ptr<biz::Env> m_env;
		std::wstring m_name;
		std::size_t m_procCount{0};
		std::wstring m_strProcCount{L"0"};
		std::chrono::steady_clock::time_point m_startTime{};
		// 该环境是否处于“首次初始化中”（新建环境启动后 pending 未清除），
		// 为 true 且在卡片模式时显示“首次初始化中，请稍候”提示
		bool m_bFirstLaunchPending{false};
		bool m_bIdle;
		// 注意：不能用 std::nostopstate 初始化——无共享状态时 stop_possible() 为 false，
		// request_stop() 是空操作，transfer_after/transfer_to 走非取消路径，
		// 析构时的取消将完全失效（退出残留根因之一）。
		std::stop_source m_stopSource{};
		// 仅用于卡片销毁时取消 onProcessCountChange / tickRuntime 这两个跨线程协程。
		// 与 m_stopSource 分开：后者会被 setBusyTemp 复用（启动选中环境时重置），
		// 若混用会把运行中环境的“已运行 X 分钟”计时和进程计数更新误取消。
		std::stop_source m_lifeStopSource{};
		OnSelected m_pfnOnSelect;
		OnProcCountChange m_pfnOnProcCountChange;
		OnSummaryChange m_pfnOnSummaryChange;

		// 长按拖拽状态
		std::chrono::steady_clock::time_point m_pressTime{};
		bool m_pressing = false;
		bool m_dragging = false;
		bool m_justDragged = false;
		float m_pressOwnerX = 0.f;
		float m_pressOwnerY = 0.f;
		OnDragStart m_pfnOnDragStart;
		OnDragMove m_pfnOnDragMove;
		OnDragEnd m_pfnOnDragEnd;
	};
}
