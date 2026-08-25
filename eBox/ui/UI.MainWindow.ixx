export module UI.MainWindow;

import "sys_defs.h";
import std;
import UI.Core;
import UI.Page;
import UI.Button;
import Coroutine;
import biz.Update;

namespace ui
{
	export class MainWindow final : public WindowBase
	{
	public:
		MainWindow();
		virtual ~MainWindow();

		coro::LazyTask<void> cliCreateProcess(std::wstring exePath, std::wstring params) const;

	public:
		virtual HResult onCreateDeviceResources(ID2D1HwndRenderTarget* renderTarget) override;
		virtual void onDiscardDeviceResources() override;
		virtual void draw(const RenderContext& renderCtx) override;

	protected:
		void drawToTryBtn(const RenderContext& renderCtx, Button::EState state) const;
		void drawToLicenseBtn(const RenderContext& renderCtx, Button::EState state) const;
		void drawToUpdateBtn(const RenderContext& renderCtx, Button::EState state) const;
		void drawToHelpBtn(const RenderContext& renderCtx, Button::EState state) const;
		virtual void onResize(float width, float height) override;
		virtual void onActivate(WParam wParam, LParam lParam) override;
		virtual bool onClose() override;
		virtual void onBeforeWindowDestroy() override;
		virtual bool onNcCalcSize(WParam wParam, LParam lParam) override;
		virtual LResult onNcHitTest(WPARAM wParam, LParam lParam, LResult dwmProcessedResult) override;
		virtual void onNcPaint(WParam wParam, LParam lParam) override;
		virtual void onDropFiles(WParam wParam) override;
		virtual void onDwmCompositionChanged() override;
		virtual void onUserMsg(UINT message, WParam wParam, LParam lParam) override;

	private:
		void initWindow();
		void reinitWindow();
		void initWindowPosition();
		void initTitleIcon();
		void initTray() const;
		void destroyTray() const;
		void killAllEnvProcesses() const;
		ID2D1Bitmap* getTitleIconBitmap(ID2D1HwndRenderTarget* renderTarget);
		// coro::LazyTask<void> initSymbols();
		bool ncBtnHitTest(POINT pt) const;

		// ===== 自动升级 =====
		void startUpdateCheck();
		coro::LazyTask<void> checkUpdateTask();
		void onCheckUpdateDone(biz::update::CheckOutcome outcome);
		void showUpdateDialog();
		void startDownloadAndApply(biz::update::UpdateManifest manifest);
		static HRESULT CALLBACK downloadDlgCallback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LONG_PTR lpRefData);

	private:
		template <typename PageType>
		void changePageTo()
		{
			m_pages = std::make_unique<PageType>(this);
			if (isCompositionEnabled())
			{
				getPage<PageType>().setMargins(m_margins);
			}
			invalidateRect();
		}

		template <typename PageType>
		bool isPage() const
		{
			return std::holds_alternative<std::unique_ptr<PageType>>(m_pages);
		}

		template <typename PageType>
		PageType& getPage() const
		{
			if (std::holds_alternative<std::unique_ptr<PageType>>(m_pages))
			{
				return *std::get<std::unique_ptr<PageType>>(m_pages).get();
			}
			throw std::runtime_error("page not exists");
		}

		RendererInterface* currentRenderer() const
		{
			// if (std::holds_alternative<std::unique_ptr<DownloadPage>>(m_pages))
			// {
			// 	return std::get<std::unique_ptr<DownloadPage>>(m_pages).get();
			// }
			if (std::holds_alternative<std::unique_ptr<HomePage>>(m_pages))
			{
				return std::get<std::unique_ptr<HomePage>>(m_pages).get();
			}
			throw std::runtime_error("unknown state");
		}

	private:
		// std::variant<std::monostate, std::unique_ptr<DownloadPage>, std::unique_ptr<HomePage>> m_pages;
		std::variant<std::monostate, std::unique_ptr<HomePage>> m_pages;

	private:
		MARGINS m_physicalMargins{};
		D2D1_RECT_F m_margins{};
		HICON m_hIcon{};
		BITMAP m_bmIcon;
		std::vector<std::byte> m_bmpIconData;
		UniqueComPtr<ID2D1Bitmap> m_pD2D1Bitmap;
		UniqueComPtr<IDWriteTextLayout> m_pTitleLayout;
		float m_titleTextHeight{};
		float m_captionBtnWidth{};
		Button m_btnToTray{this};
		Button m_btnLicense{this};
		Button m_btnUpdate{this};
		Button m_btnHelp{this};
		HWND m_hLicenseTooltip{nullptr};
		HWND m_hUpdateTooltip{nullptr};
		HWND m_hHelpTooltip{nullptr};

		// ===== 自动升级状态 =====
		coro::AsyncScope m_updateScope;
		std::optional<biz::update::UpdateManifest> m_pendingUpdate;  // 有更新时存 manifest
		bool m_hasUpdate{false};          // 红点是否亮起
		int m_licenseRemindDays{0};       // 距离到期剩余天数（1~7 时"授权"按钮亮红点提醒；0/负=不提醒）
		std::stop_source m_updateTimerStop;  // 6小时周期复检的取消令牌
		std::stop_source m_dlStopSource;     // 下载协程的取消令牌（关窗/点取消时 request_stop）
		biz::update::CheckResult m_lastCheckResult{biz::update::CheckResult::NoUpdate};  // 最近一次检查结果（用于区分提示）
		bool m_manualCheckPending{false};  // 用户点击"更新"触发实时检查：检查完成后自动弹窗展示结果

		// ===== 下载进度（跨线程 atomic，供进度弹窗轮询）=====
		std::atomic<std::uint64_t> m_dlDownloaded{0};
		std::atomic<std::uint64_t> m_dlTotal{0};
		std::atomic<int> m_dlState{0};   // 0=进行中 1=成功 2=失败 3=用户取消
		std::optional<biz::update::DownloadOutcome> m_dlOutcome;  // 下载完成结果（主线程读）
	};

	export MainWindow* g_main_wnd{nullptr};

	export MainWindow& main_wnd()
	{
		return *g_main_wnd;
	}
}
