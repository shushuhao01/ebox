module FileRedirect;

namespace
{
	// 指纹敏感文件：不继承原机数据，让应用在环境内自生成。
	// 若把这些文件从原生目录拷贝到环境，所有环境将共享同一份设备指纹
	// （machine_id / qimei 等），多环境同时登录多个账号会被风控判定为
	// 同一台设备的批量行为，存在封号风险。
	bool is_fingerprint_file(const std::wstring& redirectFile)
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
				if (fs::exists(dst))
				{
					return true; // 已存在（并发去重或上次已拷贝），直接视为成功
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
			// 指纹文件不拷贝：环境首次启动时由应用自行生成全新指纹并持久化在环境内
			if (is_fingerprint_file(redirectFile))
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
				if (!fs::exists(companionRedirect))
				{
					copy_file_with_retry((fs::path{originalFile}.parent_path() / companionName).native(),
					                     companionRedirect.native());
				}
			}
		}
		catch (...)
		{
		}
	}
}

namespace biz
{
	void FileRedirect::requestCreateRedirectFile(const std::wstring& originalFile, const std::wstring& redirectFile)
	{
		std::shared_ptr<DoneEvent> doneEvent;
		bool bIsFirstRequest;
		{
			std::lock_guard lock(m_mutex);
			if (const auto it = m_tasks.find(redirectFile); it == m_tasks.end())
			{
				doneEvent = std::make_shared<DoneEvent>();
				m_tasks.insert(std::make_pair(redirectFile, doneEvent));
				bIsFirstRequest = true;
			}
			else
			{
				doneEvent = it->second;
				bIsFirstRequest = false;
			}
		}
		if (bIsFirstRequest)
		{
			create_redirect_file(originalFile, redirectFile);
			doneEvent->signal();
			endTask(redirectFile);
		}
		else
		{
			doneEvent->wait();
		}
	}

	void FileRedirect::endTask(const std::wstring& redirectFile)
	{
		std::lock_guard lock(m_mutex);
		m_tasks.erase(redirectFile);
	}
}
