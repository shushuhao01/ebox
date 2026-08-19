module EnvLog;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import MainApp;

namespace
{
	// 日志保留时长：24 小时
	constexpr std::int64_t LOG_RETENTION_SECONDS = 24 * 60 * 60;

	// 异步写：批量 flush 间隔（最多 500ms 合并一次磁盘写）
	constexpr auto LOG_FLUSH_INTERVAL = std::chrono::milliseconds(500);
	// 单个日志文件大小上限：超过自动重建，防日志无限增长
	constexpr std::int64_t LOG_MAX_FILE_BYTES = 2 * 1024 * 1024;
	// 写队列上限：写线程异常慢时丢弃最旧，防内存膨胀
	constexpr std::size_t LOG_QUEUE_MAX_ITEMS = 4096;

	std::int64_t now_unix_seconds()
	{
		return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	}

	std::wstring format_timestamp(std::int64_t unixSeconds)
	{
		// 转成 Windows FILETIME（1601-01-01 起 100ns），再转本地时间，避免 CRT time 函数
		ULONGLONG fileTime100ns = static_cast<ULONGLONG>(unixSeconds) * 10'000'000ULL + 116'444'736'000'000'000ULL;
		FILETIME ftUtc{};
		ftUtc.dwLowDateTime = static_cast<DWORD>(fileTime100ns & 0xFFFFFFFFULL);
		ftUtc.dwHighDateTime = static_cast<DWORD>(fileTime100ns >> 32);
		FILETIME ftLocal{};
		FileTimeToLocalFileTime(&ftUtc, &ftLocal);
		SYSTEMTIME st{};
		FileTimeToSystemTime(&ftLocal, &st);
		return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
		                   st.wYear, st.wMonth, st.wDay,
		                   st.wHour, st.wMinute, st.wSecond);
	}
}

namespace biz
{
	EnvLogger::EnvLogger()
	{
		// 后台写线程：append 只入队，批量写盘在独立线程进行（不阻塞调用方）
		m_writeThread = std::jthread([this](std::stop_token stopToken) { writerLoop(std::move(stopToken)); });
	}

	std::filesystem::path EnvLogger::logDirPath() const
	{
		namespace fs = std::filesystem;
		return fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env\\data\\logs"});
	}

	std::filesystem::path EnvLogger::logFilePath(std::uint32_t envIndex) const
	{
		return logDirPath() / std::format(L"env_{}.log", envIndex);
	}

	void EnvLogger::pruneExpiredLocked(LogFileData& data) const
	{
		const std::int64_t cutoff = now_unix_seconds() - LOG_RETENTION_SECONDS;
		while (!data.entries.empty() && data.entries.front().timestamp < cutoff)
		{
			data.entries.pop_front();
		}
	}

	void EnvLogger::append(std::uint32_t envIndex, EnvLogType type, EnvLogStatus status,
	                       std::wstring_view action, std::wstring_view detail)
	{
		EnvLogEntry entry{
			.timestamp = now_unix_seconds(),
			.type = type,
			.status = status,
			.action = std::wstring{action},
			.detail = std::wstring{detail},
		};

		{
			std::lock_guard lock(m_mutex);
			LogFileData& data = m_logs[envIndex];
			data.entries.push_back(entry);
			pruneExpiredLocked(data);
			if (data.entries.size() > 500)
			{
				data.entries.pop_front();
			}
		}
		m_appendCount.fetch_add(1, std::memory_order_relaxed);

		// 持久化：入队由后台线程批量写盘（避免每条日志同步 CreateFile/WriteFile/CloseHandle 的 I/O 尖峰）
		// 行格式：[时间] [类型:0-4] [状态:0-2] 动作\t描述（\t 分隔动作与描述，描述可能含空格）
		try
		{
			const std::wstring line = std::format(L"[{}] [{}] [{}] {}\t{}\n",
			                                      format_timestamp(entry.timestamp),
			                                      static_cast<int>(entry.type),
			                                      static_cast<int>(entry.status),
			                                      entry.action, entry.detail);
			{
				std::lock_guard wlock(m_writeMutex);
				if (m_writeQueue.size() >= LOG_QUEUE_MAX_ITEMS)
				{
					m_writeQueue.pop_front(); // 丢弃最旧，防内存膨胀
				}
				m_writeQueue.push_back(PendingWrite{envIndex, std::move(line)});
			}
			m_writeCv.notify_one();
		}
		catch (...)
		{
		}
	}

	// 后台写线程：批量 flush，复用每环境文件句柄；停止时把剩余队列写空再退出
	void EnvLogger::writerLoop(std::stop_token stopToken)
	{
		std::unique_lock lock(m_writeMutex);
		while (true)
		{
			m_writeCv.wait_for(lock, LOG_FLUSH_INTERVAL, [this] { return !m_writeQueue.empty(); });
			if (stopToken.stop_requested() && m_writeQueue.empty())
			{
				break;
			}
			flushPendingLocked();
		}
		// 析构 join 前把剩余队列写空，保证持久化一致
		flushPendingLocked();
		for (auto& [envIndex, h] : m_fileHandles)
		{
			if (h && h != INVALID_HANDLE_VALUE)
			{
				CloseHandle(h);
			}
		}
		m_fileHandles.clear();
	}

	// 把当前队列全部写出；前提：已持有 m_writeMutex
	void EnvLogger::flushPendingLocked()
	{
		if (m_writeQueue.empty())
		{
			return;
		}
		try
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			fs::create_directories(logDirPath(), ec);
			while (!m_writeQueue.empty())
			{
				PendingWrite item = std::move(m_writeQueue.front());
				m_writeQueue.pop_front();

				HANDLE& h = m_fileHandles[item.envIndex];
				if (h == nullptr || h == INVALID_HANDLE_VALUE)
				{
					h = CreateFileW(logFilePath(item.envIndex).native().c_str(), FILE_APPEND_DATA,
					                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
					                FILE_ATTRIBUTE_NORMAL, nullptr);
				}
				if (h == INVALID_HANDLE_VALUE)
				{
					continue; // 打开失败：跳过本条，下次 flush 重试
				}
				// 大小轮转：超过上限重建文件（丢弃最旧的历史，防日志无限增长）
				LARGE_INTEGER fileSize{};
				if (GetFileSizeEx(h, &fileSize) && fileSize.QuadPart > LOG_MAX_FILE_BYTES)
				{
					CloseHandle(h);
					h = INVALID_HANDLE_VALUE;
					fs::remove(logFilePath(item.envIndex), ec);
					h = CreateFileW(logFilePath(item.envIndex).native().c_str(), FILE_APPEND_DATA,
					                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
					                FILE_ATTRIBUTE_NORMAL, nullptr);
					if (h == INVALID_HANDLE_VALUE)
					{
						continue;
					}
				}
				DWORD written = 0;
				WriteFile(h, item.line.c_str(), static_cast<DWORD>(item.line.size() * sizeof(wchar_t)), &written, nullptr);
			}
		}
		catch (...)
		{
		}
	}

	std::vector<EnvLogEntry> EnvLogger::getRecentLogs(std::uint32_t envIndex) const
	{
		std::lock_guard lock(m_mutex);
		const auto it = m_logs.find(envIndex);
		if (it == m_logs.end())
		{
			return {};
		}
		return {it->second.entries.begin(), it->second.entries.end()};
	}

	std::vector<EnvLogEntry> EnvLogger::getAllLogs(std::uint32_t envIndex) const
	{
		return getRecentLogs(envIndex);
	}

	void EnvLogger::clear(std::uint32_t envIndex)
	{
		{
			std::lock_guard lock(m_mutex);
			m_logs.erase(envIndex);
		}
		// 内容已变，让 UI 感知变化（条数归零也会触发刷新，这里额外递增保证一致）
		m_appendCount.fetch_add(1, std::memory_order_relaxed);
		// 丢弃尚未写盘的条目并关闭文件句柄（先于删文件，避免句柄占用导致删除失败）
		{
			std::lock_guard wlock(m_writeMutex);
			std::erase_if(m_writeQueue, [envIndex](const PendingWrite& p) { return p.envIndex == envIndex; });
			const auto it = m_fileHandles.find(envIndex);
			if (it != m_fileHandles.end())
			{
				if (it->second != INVALID_HANDLE_VALUE)
				{
					CloseHandle(it->second);
				}
				m_fileHandles.erase(it);
			}
		}
		// 删除磁盘上的日志文件（失败静默，不影响 UI）
		try
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			fs::remove(logFilePath(envIndex), ec);
		}
		catch (...)
		{
		}
	}

	std::uint64_t EnvLogger::appendVersion() const
	{
		return m_appendCount.load(std::memory_order_relaxed);
	}

	void EnvLogger::loadFromDisk()
	{
		try
		{
			namespace fs = std::filesystem;
			const fs::path dir = logDirPath();
			std::error_code ec;
			if (!fs::exists(dir, ec))
			{
				return;
			}
			const std::int64_t cutoff = now_unix_seconds() - LOG_RETENTION_SECONDS;
			const std::int64_t now = now_unix_seconds();
			for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec))
			{
				if (!entry.is_regular_file(ec))
				{
					continue;
				}
				const std::wstring fileName = entry.path().filename().native();
				// 解析 env_<index>.log
				if (!fileName.starts_with(L"env_") || !fileName.ends_with(L".log"))
				{
					continue;
				}
				std::uint32_t envIndex = 0;
				try
				{
					envIndex = static_cast<std::uint32_t>(std::stoul(fileName.substr(4, fileName.size() - 8)));
				}
				catch (...)
				{
					continue;
				}
				// 读取文件行
				HANDLE hFile = CreateFileW(entry.path().native().c_str(), GENERIC_READ,
				                           FILE_SHARE_READ | FILE_SHARE_WRITE,
				                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (hFile == INVALID_HANDLE_VALUE)
				{
					continue;
				}
				LARGE_INTEGER fileSize{};
				GetFileSizeEx(hFile, &fileSize);
				if (fileSize.QuadPart <= 0 || fileSize.QuadPart > 4 * 1024 * 1024)
				{
					CloseHandle(hFile);
					continue;
				}
				std::wstring content;
				content.resize(static_cast<std::size_t>(fileSize.QuadPart / sizeof(wchar_t)));
				DWORD bytesRead = 0;
				ReadFile(hFile, content.data(), static_cast<DWORD>(fileSize.QuadPart), &bytesRead, nullptr);
				CloseHandle(hFile);
				content.resize(bytesRead / sizeof(wchar_t));

				std::lock_guard lock(m_mutex);
				LogFileData& data = m_logs[envIndex];
				std::vector<std::wstring> keptLines; // 24h 内需要保留的行（用于重写文件）
				std::size_t totalLines = 0;
				std::size_t start = 0;
				while (start < content.size())
				{
					const std::size_t nl = content.find(L'\n', start);
					if (nl == std::wstring::npos)
					{
						break;
					}
					std::wstring_view line{content.data() + start, nl - start};
					start = nl + 1;
					++totalLines;
					// 行格式：[YYYY-MM-DD HH:MM:SS] [type] [status] action\tdetail
					EnvLogEntry entry;
					entry.timestamp = now;
					if (line.size() >= 20 && line[0] == L'[')
					{
						try
						{
							const std::wstring dateStr{line.substr(1, 19)};
							std::tm tm{};
							std::wistringstream iss{dateStr};
							iss >> std::get_time(&tm, L"%Y-%m-%d %H:%M:%S");
							using namespace std::chrono;
							// 把解析出的日期时间当作本地时间，手动换算为 Unix 秒（避免 _mkgmtime）
							const year_month_day ymd{year{tm.tm_year + 1900}, month{static_cast<unsigned>(tm.tm_mon + 1)}, day{static_cast<unsigned>(tm.tm_mday)}};
							const auto tp = sys_seconds{sys_days{ymd}} + hours{tm.tm_hour} + minutes{tm.tm_min} + seconds{tm.tm_sec};
							entry.timestamp = duration_cast<seconds>(tp.time_since_epoch()).count();
						}
						catch (...)
						{
						}
					}
					// 提取 [type] [status] 与 action\tdetail（跳过时间戳括号，从时间戳 ] 之后开始）
					std::size_t pos = 0;
					{
						const std::size_t firstClose = line.find(L']');
						pos = firstClose == std::wstring_view::npos ? std::wstring_view::npos : firstClose + 1;
					}
					for (int bracket = 0; bracket < 2 && pos != std::wstring_view::npos; ++bracket)
					{
						const std::size_t open = line.find(L'[', pos);
						if (open == std::wstring_view::npos)
						{
							break;
						}
						const std::size_t close = line.find(L']', open);
						if (close == std::wstring_view::npos)
						{
							break;
						}
						const std::wstring_view inner = line.substr(open + 1, close - open - 1);
						try
						{
							const int value = std::stoi(std::wstring{inner});
							if (bracket == 0)
							{
								entry.type = value >= 0 && value <= 4 ? static_cast<biz::EnvLogType>(value) : biz::EnvLogType::Info;
							}
							else
							{
								entry.status = value >= 0 && value <= 2 ? static_cast<biz::EnvLogStatus>(value) : biz::EnvLogStatus::Info;
							}
						}
						catch (...)
						{
						}
						pos = close + 1;
					}
					// pos 现在指向 action 起始
					if (pos < line.size() && line[pos] == L' ')
					{
						++pos;
					}
					if (pos < line.size())
					{
						const std::size_t tab = line.find(L'\t', pos);
						if (tab != std::wstring_view::npos)
						{
							entry.action = std::wstring{line.substr(pos, tab - pos)};
							entry.detail = std::wstring{line.substr(tab + 1)};
						}
						else
						{
							entry.action = std::wstring{line.substr(pos)};
						}
					}
					if (entry.timestamp >= cutoff)
					{
						data.entries.push_back(std::move(entry));
						keptLines.emplace_back(line);
					}
					if (data.entries.size() > 500)
					{
						data.entries.pop_front();
					}
				}

				// 若文件中有过期行，重写文件（24h 自动过期清理）
				if (keptLines.size() < totalLines)
				{
					std::wstring rewritten;
					for (const std::wstring& keep : keptLines)
					{
						rewritten.append(keep);
						rewritten.push_back(L'\n');
					}
					HANDLE hRewrite = CreateFileW(entry.path().native().c_str(), GENERIC_WRITE,
					                              FILE_SHARE_READ | FILE_SHARE_WRITE,
					                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
					if (hRewrite != INVALID_HANDLE_VALUE)
					{
						DWORD written = 0;
						WriteFile(hRewrite, rewritten.c_str(), static_cast<DWORD>(rewritten.size() * sizeof(wchar_t)), &written, nullptr);
						CloseHandle(hRewrite);
					}
				}
			}
		}
		catch (...)
		{
		}
	}
}
