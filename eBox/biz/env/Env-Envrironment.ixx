export module Env:Envrironment;

import "sys_defs.h";
import std;

namespace biz
{
	class WaitObject
	{
	public:
		WaitObject();
		~WaitObject();

		WaitObject(const WaitObject&) = delete;
		WaitObject(WaitObject&&) = delete;
		WaitObject& operator=(const WaitObject&) = delete;
		WaitObject& operator=(WaitObject&&) = delete;

		using WaitCallback = std::function<void()>;
		void setWait(HANDLE handle, WaitCallback cb);

	private:
		static VOID CALLBACK onHandleNotify(_Inout_ PTP_CALLBACK_INSTANCE, _Inout_opt_ PVOID Context, _Inout_ PTP_WAIT, _In_ TP_WAIT_RESULT);

	private:
		TP_WAIT* m_pWait;
		WaitCallback m_cb;
	};

	class HandleWaiter
	{
	public:
		void addWait(HANDLE handle, WaitObject::WaitCallback cb);

	private:
		struct WaitObjectWrapper
		{
			WaitObject obj;
			size_t useIndex;
		};

		WaitObjectWrapper* getObject();
		void releaseObject(const WaitObjectWrapper* obj);

	private:
		std::mutex m_mutex;
		std::vector<std::unique_ptr<WaitObjectWrapper>> m_frees;
		std::vector<std::unique_ptr<WaitObjectWrapper>> m_inUse;
	};

	class ProcessHandle
	{
	public:
		explicit ProcessHandle(HANDLE handle) noexcept : m_handle(handle)
		{
		}

		explicit ProcessHandle(DWORD pid)
		{
			m_handle = OpenProcess(PROCESS_ALL_ACCESS, false, pid);
			if (!m_handle)
			{
				throw std::runtime_error(std::format("Failed to open process, error code:{}", GetLastError()));
			}
		}

		operator HANDLE() const { return m_handle; }

		~ProcessHandle()
		{
			CloseHandle(m_handle);
		}

		ProcessHandle(const ProcessHandle&) = delete;
		ProcessHandle(ProcessHandle&&) = delete;
		ProcessHandle& operator=(const ProcessHandle&) = delete;
		ProcessHandle& operator=(ProcessHandle&&) = delete;

		explicit operator bool() const noexcept
		{
			return m_handle != nullptr;
		}

	private:
		HANDLE m_handle;
	};

	export class ProcessInfo
	{
	public:
		explicit ProcessInfo(HANDLE handle);
		explicit ProcessInfo(DWORD pid);
		ProcessInfo(HANDLE handle, DWORD pid);

	public:
		HANDLE getHandle() const noexcept { return m_hProcess; }
		DWORD getProcessId() const noexcept { return m_processId; }
		std::wstring_view getProcessFullPath() const noexcept { return m_fullPath; }

		void setDenseIndex(size_t index) noexcept { m_indexInDense = index; }
		size_t getDenseIndex() const noexcept { return m_indexInDense; }

		void addToplevelWindow(void* hWnd);
		void removeToplevelWindow(void* hWnd);
		const std::unordered_set<void*>& getToplevelWindows() const noexcept { return m_toplevelWindows; }

	private:
		void initializeFullPath();

	private:
		ProcessHandle m_hProcess;
		std::wstring m_fullPath;
		size_t m_indexInDense{0};
		DWORD m_processId;
		std::unordered_set<void*> m_toplevelWindows;
	};

	class ProcessDenseMap
	{
	public:
		bool addProcessInfo(const std::shared_ptr<ProcessInfo>& procInfo);
		bool removeProcessInfoById(DWORD pid);

	public:
		std::size_t getCount() const;
		std::vector<std::shared_ptr<ProcessInfo>> getAllProcesses() const;
		std::shared_ptr<ProcessInfo> getProcessInfo(DWORD pid) const;
		const std::vector<DWORD>& getPids() const { return m_densePids; }
		bool contains(const std::wstring& procFullName) const { return m_procNames.contains(procFullName); }

	private:
		std::unordered_multiset<std::wstring> m_procNames;
		std::vector<DWORD> m_densePids;
		std::unordered_map<DWORD, std::shared_ptr<ProcessInfo>> m_sparse;
	};

	export class TopLevelWindow
	{
	public:
		explicit TopLevelWindow(void* hWnd) noexcept : m_hWnd(hWnd)
		{
		}

	public:
		void* getHandle() const noexcept { return m_hWnd; }

		void setDenseIndex(size_t index) noexcept { m_indexInDense = index; }
		size_t getDenseIndex() const noexcept { return m_indexInDense; }

	private:
		void* m_hWnd;
		size_t m_indexInDense{0};
	};

	class TopLevelWindowDenseMap
	{
	public:
		bool addTopLevelWindow(void* hWnd);
		bool removeTopLevelWindow(void* hWnd);

	public:
		const std::vector<void*>& getAllHandles() const { return m_denseHandles; }
		bool contains(void* handle) const { return m_sparse.contains(handle); }

	private:
		std::vector<void*> m_denseHandles;
		std::unordered_map<void*, TopLevelWindow> m_sparse;
	};

	export class Env
	{
	public:
		Env(std::uint32_t index, std::uint64_t flag,
		    std::wstring_view flagName, std::wstring_view name,
		    std::wstring_view appPath = {})
			: m_index(index), m_flag(flag), m_flagName(flagName), m_name(name), m_appPath(appPath)
		{
		}

	public:
		std::uint32_t getIndex() const { return m_index; }
		std::uint64_t getFlag() const { return m_flag; }
		std::wstring_view getFlagName() const { return m_flagName; }
		std::wstring_view getName() const { return m_name; }
		void setName(std::wstring_view name) { m_name = name; }
		// 该环境首次启动时绑定的应用路径（环境卡片“启动”按钮直接启动它）
		std::wstring_view getAppPath() const { return m_appPath; }
		void setAppPath(std::wstring_view appPath) { m_appPath = appPath; }
		// 确保注入 DLL 已写入设备并返回其完整路径。bit 指定目标进程位宽：
		// 启动父进程（cmd）时传 CURRENT_ARCH_BIT（与 eBox 同架构）即可；
		// 32/64 两个位宽的 bin 文件都会写入，具体注入哪个由注入端按目标位数选择。
		std::string ensureDllInDeviceAndReturnPath(ArchBit bit = CURRENT_ARCH_BIT) const;
		void deleteDllFromDevice() const;
		// 清理环境内 QYWX 的 CEF 渲染缓存（qtCef/WXWorkCefCache/Default 框架/GPU 着色器等），
		// 不动聊天记录、登录状态、企业配置；返回实际释放的字节数（被占用未删的不计入）。
		std::uint64_t cleanWxworkCache() const;
		// 清理环境内 QYWX 的聊天记录（各企业数据目录下的 Data 消息库 / Index 搜索索引），
		// 不影响登录状态与企业配置；返回实际释放的字节数（被占用未删的不计入）。
		std::uint64_t cleanWxworkChatData() const;
		// 统计（不删除）：环境内 CEF 缓存总大小（用于界面展示是否需要清理）
		std::uint64_t getWxworkCacheSize() const;
		// 统计（不删除）：环境内聊天记录总大小
		std::uint64_t getWxworkChatDataSize() const;
		// 统计（不删除）：环境数据目录总大小（含注册表 hive / 日志 / 缓存 / 聊天记录等）
		std::uint64_t getEnvDataSize() const;

	public:
		// 环境虚拟注册表 hive：启动进程前加载，进程全部退出后保存并卸载。
		// 各环境拥有独立的 hive（RegLoadKeyW 挂到 HKU\eBox_Env_<index>），
		// 注册表指纹随环境持久化且环境间互不共享。
		bool loadRegistryHive();
		// 仅确保 hive 文件存在（不加载），用于创建环境时预生成
		bool ensureRegistryHiveFile();
		bool saveRegistryHive();
		bool unloadRegistryHive();
		bool isRegistryHiveLoaded() const { return m_hiveLoaded; }

	public:
		std::shared_ptr<ProcessInfo> addProcess(DWORD pid);

		std::size_t getAllProcessesCount() const;
		std::vector<std::shared_ptr<ProcessInfo>> getAllProcesses() const;
		std::shared_ptr<ProcessInfo> getProcess(DWORD pid) const;
		std::vector<DWORD> getAllProcessIds() const;
		bool contains(const std::wstring& procFullName) const;

		enum class EProcEvent:std::uint8_t
		{
			Create,
			Terminate,
		};

		using ProcCountChangeNotify = std::function<void(EProcEvent, const std::shared_ptr<ProcessInfo>&, std::size_t)>;
		void setProcCountChangeNotify(ProcCountChangeNotify notify);

		void addToplevelWindow(DWORD pid, void* hWnd);
		void removeToplevelWindow(DWORD pid, void* hWnd);
		bool containsToplevelWindow(void* hWnd) const;
		std::vector<void*> getAllToplevelWindows() const;

	public:
		// 首次启动提示：新建环境时创建 pending 标记文件；应用窗口出现或进程全部
		// 退出后立即清除。UI 据此在进程区/卡片显示"首次初始化中，请稍候"。
		std::filesystem::path firstLaunchFilePath() const;
		bool isFirstLaunchPending() const;
		void markFirstLaunchDone();
		// 首次启动提示是否满足清除条件：进程已全部退出或主窗口已出现
		// （窗口出现说明首次数据初始化已完成）
		bool shouldClearFirstLaunchPending() const;
		// 进程区"首次启动温馨提示"展示判定：首次数据初始化期间持续展示；
		// 窗口已出现后继续展示满 30 秒再消失；进程全部退出立即消失。
		// 与 shouldClearFirstLaunchPending 分开：卡片区在窗口出现时立即清除
		// pending（保持现状），进程区提示依赖本方法展示满 30 秒。
		bool isFirstLaunchTipActive() const;

	private:
		bool addProcessInternal(const std::shared_ptr<ProcessInfo>& procInfo);
		bool removeProcessInternal(const std::shared_ptr<ProcessInfo>& procInfo);
		void removeToplevelWindowWhenProcessTerminate(const std::shared_ptr<ProcessInfo>& procInfo);
		std::wstring registryHiveSubKey() const;
		std::filesystem::path registryHivePath() const;
		void commitPendingHiveFile();

	private:
		std::uint32_t m_index{0};
		std::uint64_t m_flag{0};
		std::wstring m_flagName;
		std::wstring m_name;
		std::wstring m_appPath;
		bool m_hiveLoaded{false};
		std::wstring m_pendingHiveFile;
		// 首次启动计时起点（首个进程加入环境的时刻），用于判定首次启动是否完成
		std::uint64_t m_firstProcStartTick{0};
		// 进程区温馨提示计时起点：与 m_firstProcStartTick 同刻设置但不受
		// markFirstLaunchDone 清零影响（窗口出现后进程区提示仍需展示满 30 秒）
		std::uint64_t m_firstLaunchTipStartTick{0};

		HandleWaiter m_waiter;
		// 【锁使用铁律】m_mutex 是读写锁（shared_mutex → SRW）：写者（unique_lock）会
		// 阻塞全部读者（shared_lock），读者含 UI 线程与 RPC 热路径（getAllProcessIds 等）。
		// 因此 unique_lock 临界区内【只允许】容器操作，严禁：
		//   1. 文件系统 I/O（fs::exists / weakly_canonical / CreateFile 等）
		//   2. 用户回调（m_notify / 任何 std::function，其内部会 spawn 协程/addTask/写日志）
		//   3. RPC / 网络请求 / Sleep / 消息等待
		// 正确姿势：锁内更新容器 + 拷贝回调副本 → 锁外执行 I/O 与回调。
		// 违反此铁律曾导致启动环境卡片"未响应"（见 logs/eBox-卡死诊断与修复报告.md）。
		// CI 检查脚本：tools/check-locks.ps1
		mutable std::shared_mutex m_mutex;
		ProcessDenseMap m_processes;
		ProcCountChangeNotify m_notify;

		mutable std::shared_mutex m_wndMutex;
		TopLevelWindowDenseMap m_toplevelWindows;
	};
}
