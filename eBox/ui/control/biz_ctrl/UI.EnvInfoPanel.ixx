export module UI.EnvInfoPanel;

import "sys_defs.h";
import std;
import UI.Core;
import UI.Button;
import Scheduler;
import Biz.Core;
import EnvLog;

namespace ui
{
	// 环境信息折叠卡片：显示当前选中环境的设备指纹信息
	// （环境名称 / 设备码 machine_id / 注册表 hive / 腾讯 qimei 等）。
	// 同一环境内所有进程共享同一份指纹（文件落在环境目录、注册表落在 HKU\eBox_Env_<idx>），
	// 因此这里展示的是“整个环境”的信息，而非某个进程。
	export class EnvInfoPanel final : public ControlBase
	{
	public:
		template <typename... Args>
		explicit EnvInfoPanel(Args&&... args) noexcept : ControlBase(std::forward<Args>(args)...)
		{
			initialize();
		}

		~EnvInfoPanel() override;

	public:
		void setEnv(const std::shared_ptr<biz::Env>& env);
		void clearEnv();
		bool hasEnv() const noexcept { return m_env != nullptr; }
		bool isExpanded() const noexcept { return m_expanded; }
		void setExpanded(bool expanded);
		// 展开时内容区高度（由外层按手风琴布局分配）
		void setContentHeight(float contentHeight) { m_contentHeight = std::max(0.f, contentHeight); }
		float getHeaderHeight() const noexcept { return HEADER_HEIGHT; }
		// 点击标题栏请求展开/收起（交给外层统一做手风琴互斥）
		std::function<void(bool)> onExpandRequest;

	protected:
		virtual void onResize(float width, float height) override;
		virtual void onMouseEnter(const MouseEvent& e) override;
		virtual void onMouseLeave(const MouseEvent& e) override;
		virtual void onClick(const MouseEvent& e) override;

	private:
		virtual void drawImpl(const RenderContext& renderCtx) override;

	private:
		void initialize();
		void toggleExpand();
		bool copyInfo();
		void refreshInfo();
		// 后台线程统计重字段（环境大小/缓存/聊天记录/machine_id/qimei），
		// 完成后经 UI 调度器回投结果，避免 GB 级目录遍历阻塞 UI 线程（未响应）
		void startHeavyScanAsync();
		void applyHeavyResult(std::uint64_t total, std::uint64_t cache, std::uint64_t chat,
		                      std::wstring machineId, std::wstring qimei);
		void onCleanBtnClick();

	private:
		static constexpr float HEADER_HEIGHT = 28.f;
		Button m_btnCopy{this};
		// 清理环境数据按钮：弹窗选择（仅缓存/仅聊天记录/都清理），确认后执行
		Button m_btnClean{this};
		std::stop_source m_copyTimerStopSource;
		bool m_isHovered{false};
		bool m_expanded{false};
		float m_contentHeight{0.f};
		std::shared_ptr<biz::Env> m_env;
		// 待展示字段：{标签, 值}
		std::vector<std::pair<std::wstring, std::wstring>> m_fields;
		std::wstring m_copyText;
		// ===== 异步统计（防 UI 卡顿）=====
		// 面板存活标记：后台线程回投结果前检查，面板销毁后不得触碰 this
		std::shared_ptr<std::atomic_bool> m_alive{std::make_shared<std::atomic_bool>(true)};
		std::uint32_t m_scanGen{0};   // 扫描代次：仅应用最新一次扫描结果，过期结果丢弃
		bool m_heavyValid{false};     // 已有缓存的重字段值（复制/展示直接复用）
		std::uint64_t m_hTotal{0};
		std::uint64_t m_hCache{0};
		std::uint64_t m_hChat{0};
		std::wstring m_hMachineId;
		std::wstring m_hQimei;
		bool m_cleaning{false};       // 清理进行中（防重入）
	};
}
