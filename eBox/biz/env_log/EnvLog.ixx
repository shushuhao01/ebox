export module EnvLog;

import "sys_defs.h";
import std;

namespace biz
{
	// 日志类型：用于 UI 上区分不同类别的动作
	export enum class EnvLogType : std::uint8_t
	{
		Info,      // 环境操作（创建/加载/改名/删除）
		Process,   // 进程动作（启动/退出/注入）
		Login,     // 登录/离线
		Message,   // 闪烁提示等 UI 消息
		Error,     // 错误
	};

	// 日志状态
	export enum class EnvLogStatus : std::uint8_t
	{
		Success,
		Fail,
		Info,
	};

	// 一条环境日志
	export struct EnvLogEntry
	{
		std::int64_t timestamp{0}; // Unix 秒
		EnvLogType type{EnvLogType::Info};
		EnvLogStatus status{EnvLogStatus::Info};
		std::wstring action;    // 动作，如 L"启动进程"
		std::wstring detail;    // 详细描述，如进程路径
	};

	export class EnvLogger
	{
	public:
		EnvLogger();

		// 追加一条日志（内存 + 持久化）
		void append(std::uint32_t envIndex, EnvLogType type, EnvLogStatus status,
		            std::wstring_view action, std::wstring_view detail = {});

		// 返回该环境最近 24 小时内的日志（按时间升序）
		std::vector<EnvLogEntry> getRecentLogs(std::uint32_t envIndex) const;

		// 返回该环境全部内存日志（用于复制，含 24h 之前但未清理的）
		std::vector<EnvLogEntry> getAllLogs(std::uint32_t envIndex) const;

		// 返回日志追加总次数（单调递增，用于 UI 判断日志是否有新增，避免每次绘制都重建格式化行）
		std::uint64_t appendVersion() const;

		// 清理该环境的全部日志（内存缓存 + 磁盘文件）
		void clear(std::uint32_t envIndex);

		// 启动时调用：从磁盘恢复最近 24h 日志并清理过期文件
		void loadFromDisk();

	private:
		struct LogFileData
		{
			std::deque<EnvLogEntry> entries; // 按时间升序
		};

		// 一条待写盘日志（异步批写队列元素）
		struct PendingWrite
		{
			std::uint32_t envIndex;
			std::wstring line; // 已格式化的一行（含换行符）
		};

		std::filesystem::path logDirPath() const;
		std::filesystem::path logFilePath(std::uint32_t envIndex) const;
		void pruneExpiredLocked(LogFileData& data) const;

		// 后台写线程：批量 flush 队列（合并多条日志为一次磁盘写，复用文件句柄）
		void writerLoop(std::stop_token stopToken);
		// 把当前队列全部写出；前提：已持有 m_writeMutex
		void flushPendingLocked();

	private:
		mutable std::mutex m_mutex;
		std::unordered_map<std::uint32_t, LogFileData> m_logs;
		std::atomic<std::uint64_t> m_appendCount{0};  // 追加计数，仅增不减

		// 异步持久化：append 只入队（不碰磁盘），后台线程批量写盘，
		// 避免启动风暴期数百上千次 CreateFile/WriteFile/CloseHandle 的同步 I/O 尖峰
		mutable std::mutex m_writeMutex;
		std::condition_variable m_writeCv;
		std::deque<PendingWrite> m_writeQueue;
		std::unordered_map<std::uint32_t, HANDLE> m_fileHandles; // 每环境复用文件句柄
		std::jthread m_writeThread;
	};

	// 全局单例访问器（由 Biz.Core 初始化）
	export inline EnvLogger* g_env_logger{nullptr};
	export inline EnvLogger& env_logger()
	{
		return *g_env_logger;
	}
}
