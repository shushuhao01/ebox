module;
#include <windows.h>
module FileRedirect;

namespace
{
	// 数据库家族（SQLite/LevelDB）文件：绝不排除，必须继承拷贝
	bool is_db_file_lower(const std::wstring& lower)
	{
		return lower.ends_with(L".db") || lower.ends_with(L".db-wal") || lower.ends_with(L".db-journal")
			|| lower.ends_with(L".sqlite") || lower.ends_with(L".sqlite3")
			|| lower.ends_with(L".ldb") || lower.ends_with(L".manifest") || lower.ends_with(L".current");
	}

	// LevelDB 写日志：纯数字.log（000003.log）是数据库写日志（真数据），不是普通日志
	bool is_leveldb_log(const std::wstring& lower)
	{
		const std::size_t dot = lower.rfind(L'.');
		if (dot == std::wstring::npos || dot == 0)
		{
			return false;
		}
		if (lower.compare(dot, 4, L".log") != 0)
		{
			return false;
		}
		for (std::size_t i = 0; i < dot; ++i)
		{
			if (lower[i] < L'0' || lower[i] > L'9')
			{
				return false;
			}
		}
		return true;
	}

	// 不拷贝清单：指纹敏感文件 + 运行时可再生的缓存/日志/临时文件。
	// 1) 指纹文件（Local State / qimei）：不继承原机数据，让应用在环境内自生成，
	//    否则所有环境共享同一份设备指纹（machine_id/qimei），多开多账号会被风控
	//    判定为同一台设备，存在封号风险。
	// 2) 缓存/日志/临时类：源机运行中的 WXWork 独占写这些文件（日志/缓存），继承
	//    拷贝必然冲突重试（最长 ~1.5s/个），拖慢登录与切换；且缺失不影响数据完整性，
	//    在环境内自建即可。
	// 注意：数据库家族（.db/.db-wal/.db-journal/.sqlite/.ldb/LevelDB 数字.log）优先
	// 保护，绝不排除——排除会直接导致 WXWork "本地数据加载异常"。
	bool is_skip_copy_file(const std::wstring& redirectFile)
	{
		namespace fs = std::filesystem;
		const fs::path filePath{redirectFile};
		const std::wstring fileName = filePath.filename().native();
		// Chromium/CEF 设备指纹：Local State（含 machine_id、hardware 等信息）
		if (fileName == L"Local State")
		{
			return true;
		}
		// 腾讯 qimei 设备指纹：ProgramData\Tencent\qimei 目录下的文件
		std::wstring lower = filePath.native();
		std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		if (lower.find(L"tencent\\qimei") != std::wstring::npos)
		{
			return true;
		}
		if (is_db_file_lower(lower))
		{
			return false;
		}
		if (is_leveldb_log(lower))
		{
			return false;
		}
		// 缓存/日志/临时目录名单
		static constexpr std::wstring_view dirPatterns[] = {
			L"graphitedawncache", L"shadercache", L"wxworkcefcache", L"cefcache",
			L"gpucache", L"code cache", L"cachestorage",
			L"\\cache", L"\\caches", L"\\log\\", L"\\logs\\", L"tmlog",
			L"crashdump", L"segmentation_platform", L"\\cdn",
			L"browsermetrics", L"\\update", L"\\.trash",
		};
		for (const std::wstring_view p : dirPatterns)
		{
			if (lower.find(p) != std::wstring::npos)
			{
				return true;
			}
		}
		// Chromium 临时文件；.db-wal.NNNN.tmp 这类数据库写临时保留
		if (lower.ends_with(L".tmp") && lower.find(L".db-") == std::wstring::npos)
		{
			return true;
		}
		return false;
	}

	// 判断是否为 SQLite 主库文件（.db / .sqlite / .sqlite3）
	bool is_db_main_file(const std::wstring& filePath)
	{
		std::wstring lower = filePath;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		return lower.ends_with(L".db") || lower.ends_with(L".sqlite") || lower.ends_with(L".sqlite3");
	}

	// 获取与主库文件同目录的伴随文件（-journal / -wal / -shm）的源文件名。
	// 伴随文件与主库必须一起拷贝，否则 SQLite 打开时主库与日志校验不一致，
	// 会判定"database disk image is malformed"，进而触发应用的本地数据加载异常。
	std::vector<std::wstring> get_db_companion_file_names(const std::wstring& originalFile)
	{
		namespace fs = std::filesystem;
		std::vector<std::wstring> result;
		const fs::path srcPath{originalFile};
		std::wstring lower = srcPath.native();
		std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });

		const auto tryAdd = [&](const std::wstring& candidateSrc)
		{
			if (candidateSrc != originalFile && fs::exists(candidateSrc))
			{
				result.push_back(fs::path{candidateSrc}.filename().native());
			}
		};

		static constexpr std::wstring_view companionSuffixes[] = {L"-journal", L"-wal", L"-shm"};
		// 请求的是伴随文件（如 xxx.db-journal）：连带主库 + 主库的其他伴随
		for (const std::wstring_view suffix : companionSuffixes)
		{
			if (lower.ends_with(suffix))
			{
				const std::wstring mainSrc = srcPath.native().substr(0, srcPath.native().size() - suffix.size());
				tryAdd(mainSrc);
				for (const std::wstring_view other : companionSuffixes)
				{
					tryAdd(mainSrc + std::wstring{other});
				}
				break;
			}
		}
		// 请求的是主库（如 xxx.db）：连带全部伴随文件
		if (is_db_main_file(srcPath.native()))
		{
			for (const std::wstring_view suffix : companionSuffixes)
			{
				tryAdd(srcPath.native() + std::wstring{suffix});
			}
		}
		return result;
	}

	// 拷贝单个文件，源被短暂占用（共享冲突/锁冲突/拒绝访问）时重试若干次。
	// 源机 QYWX 正常运行时，其数据文件可能被独占，首次拷贝失败后等待重试
	// 能显著提升成功率，避免环境内静默缺失数据。
	bool copy_file_with_retry(const std::wstring& originalFile, const std::wstring& redirectFile)
	{
		namespace fs = std::filesystem;
		constexpr int maxRetries = 6;
		for (int attempt = 0; attempt < maxRetries; ++attempt)
		{
			try
			{
				const fs::path dst{redirectFile};
				{
					std::error_code existsEc;
					const bool dstExists = fs::exists(dst, existsEc);
					if (dstExists && !existsEc)
					{
						// 目标已存在：非空视为已有完整数据（上次拷贝完成或应用已写入新内容），跳过；
						// 0 字节视为继承超时残留的空文件，删除后重新原子拷贝——否则源机数据永远
						// 拷不进来，WXWork 基于空文件初始化（Local Storage/LevelDB）→ "本地数据加载异常"。
						std::error_code sizeEc;
						const auto dstSize = fs::file_size(dst, sizeEc);
						if (!sizeEc && dstSize > 0)
						{
							return true;
						}
						fs::remove(dst, sizeEc);
					}
				}
				const fs::path src{originalFile};
				if (!fs::exists(src))
				{
					return false;
				}
				fs::create_directories(dst.parent_path());
				// 同卷 temp + rename 原子落盘，避免拷贝中途被读到半截文件。
				// temp 名带线程 ID 唯一化：并发拷贝同一目标文件时（伴随文件不走任务去重），
				// 不同线程写各自 temp，避免两线程写同一 temp 交错损坏，最终 rename 覆盖为完整内容。
				const fs::path tempPath{std::format(L"{}.{}.tmp", redirectFile, GetCurrentThreadId())};
				fs::copy_file(src, tempPath, fs::copy_options::overwrite_existing);
				fs::rename(tempPath, dst);
				return true;
			}
			catch (const fs::filesystem_error& e)
			{
				const int err = e.code().value();
				// ERROR_SHARING_VIOLATION(32) / ERROR_LOCK_VIOLATION(33) / ERROR_ACCESS_DENIED(5)
				// 表示源文件正被其他进程占用，短暂等待后重试
				if ((err == 32 || err == 33 || err == 5) && attempt + 1 < maxRetries)
				{
					Sleep(100 * (attempt + 1));
					continue;
				}
				OutputDebugStringW(std::format(L"[eBox] copy redirect file failed: {} -> {} (err={})\n",
				                               originalFile, redirectFile, err).c_str());
				return false;
			}
			catch (...)
			{
				OutputDebugStringW(std::format(L"[eBox] copy redirect file failed (unknown): {} -> {}\n",
				                               originalFile, redirectFile).c_str());
				return false;
			}
		}
		return false;
	}

	void create_redirect_file(const std::wstring& originalFile, const std::wstring& redirectFile)
	{
		try
		{
			// 指纹/缓存/日志文件不拷贝：环境首次启动时由应用自行生成并持久化在环境内
			if (is_skip_copy_file(redirectFile))
			{
				return;
			}
			namespace fs = std::filesystem;
			if (!fs::exists(originalFile))
			{
				return;
			}
			// 1. 拷贝主文件（带重试）
			copy_file_with_retry(originalFile, redirectFile);
			// 2. 数据库伴随文件连带拷贝：-journal/-wal/-shm 与主库必须保持一致，
			//    否则 SQLite 打开校验不一致会判定数据库损坏 → 应用报"本地数据加载异常"。
			//    伴随文件与主库同目录，重定向路径 = 主库重定向目录 + 伴随文件名。
			const fs::path redirectDir = fs::path{redirectFile}.parent_path();
			for (const std::wstring& companionName : get_db_companion_file_names(originalFile))
			{
				const fs::path companionRedirect = redirectDir / companionName;
				// 不做 exists 短路：由 copy_file_with_retry 内部"非空才跳过"接管，
				// 0 字节残留（继承超时产生）会被源机数据覆盖，避免伴随文件与主库校验
				// 不一致触发 SQLite "database disk image is malformed" → 本地数据加载异常。
				copy_file_with_retry((fs::path{originalFile}.parent_path() / companionName).native(),
				                     companionRedirect.native());
			}
		}
		catch (...)
		{
		}
	}
}

namespace biz
{
	FileRedirect::FileRedirect()
	{
		m_workers.reserve(WorkerCount);
		for (std::size_t i = 0; i < WorkerCount; ++i)
		{
			m_workers.emplace_back([this] { workerLoop(); });
		}
	}

	FileRedirect::~FileRedirect()
	{
		{
			std::lock_guard lock(m_queueMutex);
			m_stop = true;
			// 停止后立即退出，丢弃队列中的积压任务（登录/切换瞬间可能堆积大量
			// 继承拷贝请求；逐一处理会让析构 join 卡数秒到数分钟 → eBox 关闭未响应）
			m_queue.clear();
		}
		m_queueCv.notify_all();
		for (auto& worker : m_workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}
	}

	void FileRedirect::requestCreateRedirectFile(const std::wstring& originalFile, const std::wstring& redirectFile)
	{
		// 去重：同一目标文件的拷贝任务已在排队/执行中则不再重复入队；
		// 执行完毕后从 m_tasks 移除，后续请求可重试（如上次拷贝失败）。
		{
			std::lock_guard lock(m_mutex);
			if (!m_tasks.contains(redirectFile))
			{
				m_tasks.emplace(redirectFile, std::uint8_t{0});
			}
		}
		{
			std::lock_guard lock(m_queueMutex);
			if (m_queue.size() >= MaxQueueSize)
			{
				// 队列已满：放弃本次拷贝（客户端有界轮询超时后回退自建文件，
				// 与拷贝失败时的既有降级行为一致），避免内存无界增长。
				finishTask(redirectFile);
				return;
			}
			m_queue.push_back(Task{std::wstring{originalFile}, std::wstring{redirectFile}});
		}
		m_queueCv.notify_one();
	}

	void FileRedirect::workerLoop()
	{
		for (;;)
		{
			Task task;
			{
				std::unique_lock lock(m_queueMutex);
				m_queueCv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
				if (m_stop)
				{
					// 停止后丢弃剩余任务直接退出，不再处理积压队列
					break;
				}
				task = std::move(m_queue.front());
				m_queue.pop_front();
			}
			create_redirect_file(task.originalFile, task.redirectFile);
			finishTask(task.redirectFile);
		}
	}

	void FileRedirect::finishTask(const std::wstring& redirectFile)
	{
		std::lock_guard lock(m_mutex);
		m_tasks.erase(redirectFile);
	}
}
