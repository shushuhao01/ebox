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
	EnvLogger::EnvLogger() = default;

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

		// 持久化：追加写入日志文件（离线后历史日志仍存在）
		// 行格式：[时间] [类型:0-4] [状态:0-2] 动作\t描述（\t 分隔动作与描述，描述可能含空格）
		try
		{
			namespace fs = std::filesystem;
			const fs::path dir = logDirPath();
			std::error_code ec;
			fs::create_directories(dir, ec);
			const std::wstring line = std::format(L"[{}] [{}] [{}] {}\t{}\n",
			                                      format_timestamp(entry.timestamp),
			                                      static_cast<int>(entry.type),
			                                      static_cast<int>(entry.status),
			                                      entry.action, entry.detail);
			HANDLE hFile = CreateFileW(logFilePath(envIndex).native().c_str(),
			                           FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (hFile != INVALID_HANDLE_VALUE)
			{
				DWORD written = 0;
				WriteFile(hFile, line.c_str(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
				CloseHandle(hFile);
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
