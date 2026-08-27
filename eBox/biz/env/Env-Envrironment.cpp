module Env;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import MainApp;
import EssentialData;

namespace biz
{
	namespace
	{
		// 启用指定权限（RegLoadKeyW 需要 SeRestorePrivilege，RegSaveKeyW 需要 SeBackupPrivilege）
		bool enable_privilege(LPCTSTR privilege)
		{
			HANDLE hToken = nullptr;
			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
			{
				return false;
			}
			TOKEN_PRIVILEGES tp{};
			LUID luid;
			bool ok = false;
			if (LookupPrivilegeValueW(nullptr, privilege, &luid))
			{
				tp.PrivilegeCount = 1;
				tp.Privileges[0].Luid = luid;
				tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
				ok = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
			}
			CloseHandle(hToken);
			return ok;
		}

		// 创建一个空的注册表 hive 文件（环境首次使用时）
		// 注意：不能直接在 HKEY_USERS 下创建键——RegCreateKeyExW(HKEY_USERS, ...)
		// 固定返回 ERROR_INVALID_PARAMETER(87)，导致空 hive 永远建不出来。
		// 改用 HKCU\Software 下的临时键（可正常创建）RegSaveKeyW 导出为 hive 文件。
		bool create_empty_hive_file(const std::wstring& hivePath)
		{
			constexpr wchar_t tempSubKey[] = L"Software\\eBox_Env_Temp";
			RegDeleteKeyW(HKEY_CURRENT_USER, tempSubKey);
			HKEY hTemp = nullptr;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, tempSubKey, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hTemp, nullptr) != ERROR_SUCCESS)
			{
				return false;
			}
			const bool ok = RegSaveKeyW(hTemp, hivePath.c_str(), nullptr) == ERROR_SUCCESS;
			RegCloseKey(hTemp);
			RegDeleteKeyW(HKEY_CURRENT_USER, tempSubKey);
			return ok;
		}
	}

	WaitObject::WaitObject()
	{
		m_pWait = CreateThreadpoolWait(&WaitObject::onHandleNotify, this, nullptr);
		if (!m_pWait)
		{
			throw std::runtime_error{std::format("CreateThreadpoolWait failed, error code:{}", GetLastError())};
		}
	}

	WaitObject::~WaitObject()
	{
		if (m_pWait)
		{
			SetThreadpoolWait(m_pWait, nullptr, nullptr);
			WaitForThreadpoolWaitCallbacks(m_pWait, TRUE);
			CloseThreadpoolWait(m_pWait);
		}
	}

	void WaitObject::setWait(HANDLE handle, WaitCallback cb)
	{
		SetThreadpoolWait(m_pWait, nullptr, nullptr);
		WaitForThreadpoolWaitCallbacks(m_pWait, TRUE);

		m_cb = std::move(cb);
		SetThreadpoolWait(m_pWait, handle, nullptr);
	}

	void WaitObject::onHandleNotify(PTP_CALLBACK_INSTANCE, PVOID Context, PTP_WAIT, TP_WAIT_RESULT)
	{
		if (WaitObject* pWaitObject = static_cast<WaitObject*>(Context);
			pWaitObject && pWaitObject->m_cb)
		{
			pWaitObject->m_cb();
			pWaitObject->m_cb = nullptr;
		}
	}

	void HandleWaiter::addWait(HANDLE handle, WaitObject::WaitCallback cb)
	{
		WaitObjectWrapper* objWrapper = getObject();
		objWrapper->obj.setWait(handle, [this, objWrapper, cb = std::move(cb)]
		{
			releaseObject(objWrapper);
			if (cb)
			{
				cb();
			}
		});
	}

	HandleWaiter::WaitObjectWrapper* HandleWaiter::getObject()
	{
		std::unique_ptr<WaitObjectWrapper> newObj;
		std::lock_guard lock(m_mutex);
		if (m_frees.empty())
		{
			newObj = std::make_unique<WaitObjectWrapper>();
		}
		else
		{
			newObj = std::move(m_frees.back());
			m_frees.pop_back();
		}

		WaitObjectWrapper* rawPtrReturn = newObj.get();
		m_inUse.push_back(std::move(newObj));
		const size_t index = m_inUse.size() - 1;
		rawPtrReturn->useIndex = index;
		return rawPtrReturn;
	}

	void HandleWaiter::releaseObject(const WaitObjectWrapper* obj)
	{
		std::lock_guard lock(m_mutex);

		const size_t idx = obj->useIndex;
		if (idx >= m_inUse.size())
		{
			std::unreachable();
		}
		if (idx < m_inUse.size() - 1)
		{
			std::swap(m_inUse.at(idx), m_inUse.back());
			m_inUse.at(idx)->useIndex = idx;
		}
		m_frees.push_back(std::move(m_inUse.back()));
		m_inUse.pop_back();
	}

	ProcessInfo::ProcessInfo(HANDLE handle) : m_hProcess(handle)
	{
		if (!m_hProcess)
		{
			std::unreachable();
		}
		m_processId = GetProcessId(m_hProcess);
		if (!m_processId)
		{
			throw std::runtime_error("Failed to get process id");
		}
		initializeFullPath();
	}

	ProcessInfo::ProcessInfo(DWORD pid) : m_hProcess(pid), m_processId(pid)
	{
		initializeFullPath();
	}

	ProcessInfo::ProcessInfo(HANDLE handle, DWORD pid) : m_hProcess(handle), m_processId(pid)
	{
		initializeFullPath();
	}

	void ProcessInfo::addToplevelWindow(void* hWnd)
	{
		m_toplevelWindows.insert(hWnd);
	}

	void ProcessInfo::removeToplevelWindow(void* hWnd)
	{
		m_toplevelWindows.erase(hWnd);
	}

	void ProcessInfo::initializeFullPath()
	{
		DWORD pathLength = std::numeric_limits<short>::max();
		m_fullPath.resize(pathLength);
		if (!QueryFullProcessImageNameW(m_hProcess, 0, m_fullPath.data(), &pathLength))
		{
			throw std::runtime_error("Failed to query full path");
		}
		m_fullPath.resize(pathLength);
		m_fullPath = std::wstring(m_fullPath);
	}

	bool ProcessDenseMap::addProcessInfo(const std::shared_ptr<ProcessInfo>& procInfo)
	{
		const DWORD pid = procInfo->getProcessId();
		auto [it, success] = m_sparse.insert(std::make_pair(pid, procInfo));
		if (!success)
		{
			return false;
		}

		m_procNames.insert(std::wstring{procInfo->getProcessFullPath()});

		m_densePids.push_back(pid);
		procInfo->setDenseIndex(m_densePids.size() - 1);
		return true;
	}

	bool ProcessDenseMap::removeProcessInfoById(DWORD pid)
	{
		const auto it = m_sparse.find(pid);
		if (it == m_sparse.end())
		{
			return false;
		}

		const auto itName = m_procNames.find(std::wstring{it->second->getProcessFullPath()});
		if (itName != m_procNames.end())
		{
			m_procNames.erase(itName);
		}

		const size_t idx = it->second->getDenseIndex();
		if (idx < m_densePids.size() - 1)
		{
			std::swap(m_densePids.at(idx), m_densePids.back());
			if (const auto swapped = m_sparse.find(m_densePids.at(idx)); swapped != m_sparse.end())
			{
				swapped->second->setDenseIndex(idx);
			}
		}
		m_densePids.pop_back();
		m_sparse.erase(it);
		return true;
	}

	std::size_t ProcessDenseMap::getCount() const
	{
		return m_sparse.size();
	}

	std::vector<std::shared_ptr<ProcessInfo>> ProcessDenseMap::getAllProcesses() const
	{
		std::vector<std::shared_ptr<ProcessInfo>> result;
		for (auto it = m_sparse.begin(); it != m_sparse.end(); ++it)
		{
			result.emplace_back(it->second);
		}
		return result;
	}

	std::shared_ptr<ProcessInfo> ProcessDenseMap::getProcessInfo(DWORD pid) const
	{
		const auto it = m_sparse.find(pid);
		if (it == m_sparse.end())
		{
			return nullptr;
		}
		return it->second;
	}

	bool TopLevelWindowDenseMap::addTopLevelWindow(void* hWnd)
	{
		auto [it, success] = m_sparse.insert(std::make_pair(hWnd, TopLevelWindow{hWnd}));
		if (!success)
		{
			return false;
		}
		m_denseHandles.push_back(hWnd);
		it->second.setDenseIndex(m_denseHandles.size() - 1);
		return true;
	}

	bool TopLevelWindowDenseMap::removeTopLevelWindow(void* hWnd)
	{
		const auto it = m_sparse.find(hWnd);
		if (it == m_sparse.end())
		{
			return false;
		}
		const size_t idx = it->second.getDenseIndex();
		if (idx < m_denseHandles.size() - 1)
		{
			std::swap(m_denseHandles.at(idx), m_denseHandles.back());
			if (const auto swapped = m_sparse.find(m_denseHandles.at(idx)); swapped != m_sparse.end())
			{
				swapped->second.setDenseIndex(idx);
			}
		}
		m_denseHandles.pop_back();
		m_sparse.erase(it);
		return true;
	}

	template <ArchBit BitType = CURRENT_ARCH_BIT>
	std::filesystem::path get_dll_full_path(const std::filesystem::path& binDir, std::wstring_view flagName, std::wstring_view extra = L"")
	{
		namespace fs = std::filesystem;
		if constexpr (BitType == ArchBit::Bit64)
		{
			return fs::path{fs::weakly_canonical(binDir / fs::path{std::format(L"{}_{}64.bin", flagName, extra)})};
		}
		else
		{
			return fs::path{fs::weakly_canonical(binDir / fs::path{std::format(L"{}_{}32.bin", flagName, extra)})};
		}
	}

	std::string Env::ensureDllInDeviceAndReturnPath(ArchBit bit) const
	{
		namespace fs = std::filesystem;
		// 注意：bin 目录必须与 MemoryDll 侧 initializeDllFullPath 的 rootPath\bin 保持一致。
		// 若用 exeDir\bin，在新电脑（exe 所在目录 != envDataRoot）上写入的文件
		// 与注入端读取路径不一致，导致 LoadLibrary 失败（错误 126 找不到指定的模块）。
		fs::path binDir{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"bin"})};
		if (!fs::exists(binDir))
		{
			fs::create_directories(binDir);
		}

		// 写入最新嵌入的 DLL 并覆盖旧文件：已存在的旧环境（bin 文件是旧版 DLL）
		// 在下次启动时也会被注入最新 DLL（例如新增的 Documents 重定向）。
		// 仅按文件大小判断"未变化"不可靠（新旧 DLL 大小可能恰好相同，导致旧版
		// 永远不被覆盖），因此改为与目标文件逐块比较内容，一致才跳过覆盖。
		// 若文件正被运行中的进程占用（覆盖失败），则保留旧文件，不影响已注入的进程。
		const auto writeDll = [&binDir](const fs::path& target, char* address, unsigned int size)
		{
			std::error_code ec;
			// 临时文件使用进程唯一后缀，避免多线程/多实例并发写同一 .temp 的竞态
			const fs::path tempWithSuffix = std::format(L"{}.{}.temp", target.native(), GetCurrentProcessId());
			{
				std::ofstream tempFile{tempWithSuffix, std::ios::binary | std::ios::trunc};
				tempFile.write(address, size);
				tempFile.close();
			}
			bool needReplace = true;
			if (fs::exists(target, ec) && !ec)
			{
				if (const auto fileSize = fs::file_size(target, ec); !ec && fileSize == size)
				{
					std::ifstream targetFile{target, std::ios::binary};
					std::ifstream tempFile{tempWithSuffix, std::ios::binary};
					if (targetFile && tempFile)
					{
						constexpr std::size_t BLOCK = 64 * 1024;
						std::vector<char> targetBuf(BLOCK);
						std::vector<char> tempBuf(BLOCK);
						needReplace = false;
						while (true)
						{
							targetFile.read(targetBuf.data(), static_cast<std::streamsize>(BLOCK));
							const std::streamsize targetRead = targetFile.gcount();
							tempFile.read(tempBuf.data(), static_cast<std::streamsize>(BLOCK));
							const std::streamsize tempRead = tempFile.gcount();
							if (targetRead != tempRead)
							{
								needReplace = true;
								break;
							}
							if (targetRead == 0)
							{
								break;
							}
							if (!std::equal(targetBuf.data(), targetBuf.data() + targetRead, tempBuf.data()))
							{
								needReplace = true;
								break;
							}
						}
					}
				}
			}
			if (needReplace)
			{
				if (!MoveFileExW(tempWithSuffix.native().c_str(), target.native().c_str(), MOVEFILE_REPLACE_EXISTING))
				{
					fs::remove(tempWithSuffix, ec);
				}
			}
			else
			{
				fs::remove(tempWithSuffix, ec);
			}
		};

		const fs::path path32{get_dll_full_path<ArchBit::Bit32>(binDir, m_flagName)};
		writeDll(path32, get_core_data().dll32.address, get_core_data().dll32.size);
		const fs::path path64{get_dll_full_path<ArchBit::Bit64>(binDir, m_flagName)};
		writeDll(path64, get_core_data().dll64.address, get_core_data().dll64.size);

		std::string dllFullPath;
		if (bit == ArchBit::Bit32)
		{
			dllFullPath = path32.string();
		}
		else
		{
			dllFullPath = path64.string();
		}
		return dllFullPath;
	}

	void Env::deleteDllFromDevice() const
	{
		namespace fs = std::filesystem;
		fs::path binDir{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"bin"})};
		if (!fs::exists(binDir))
		{
			return;
		}
		if (const fs::path path32{biz::get_dll_full_path<ArchBit::Bit32>(binDir, m_flagName)}; fs::exists(path32))
		{
			const fs::path tempPath{biz::get_dll_full_path<ArchBit::Bit32>(binDir, m_flagName, L"temp_to_delete")};
			fs::rename(path32, tempPath);
			fs::remove(tempPath);
		}

		if (const fs::path path64{biz::get_dll_full_path<ArchBit::Bit64>(binDir, m_flagName)}; fs::exists(path64))
		{
			const fs::path tempPath{biz::get_dll_full_path<ArchBit::Bit64>(binDir, m_flagName, L"temp_to_delete")};
			fs::rename(path64, tempPath);
			fs::remove(tempPath);
		}
	}

	// ---- 环境内 WXWork 数据目录扫描 / 统计 / 删除（缓存与聊天记录共用）----
	namespace
	{
		// CEF 缓存目录名白名单（匹配时忽略大小写）：
		//  - qtCef / WXWorkCefCache：QYWX CEF 渲染缓存（最大头）
		//  - ShaderCache/GrShaderCache/GraphiteDawnCache：GPU 着色器缓存
		//  - BrowserMetrics/segmentation_platform/Dictionaries：CEF 杂项缓存
		// 注意：Default（CEF 默认 profile）不在此列——它含 Cookies/Local Storage 等
		// 登录相关数据，整删会掉登录；仅清理其子缓存目录（见 collect_cache_dirs）。
		bool is_wxwork_cache_dir(std::wstring_view name)
		{
			static const std::vector<std::wstring_view> cacheDirs{
				L"qtcef", L"wxworkcefcache",
				L"shadercache", L"grshadercache", L"graphitedawncache",
				L"browsermetrics", L"segmentation_platform", L"dictionaries",
			};
			std::wstring lower{name};
			std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return std::towlower(c); });
			return std::find(cacheDirs.begin(), cacheDirs.end(), std::wstring_view{lower}) != cacheDirs.end();
		}

		// Default profile 下的纯缓存子目录（可安全删除，不碰 Cookies/Local Storage/Preferences）
		bool is_default_sub_cache_dir(std::wstring_view name)
		{
			static const std::vector<std::wstring_view> subCacheDirs{
				L"cache", L"code cache", L"gpucache", L"dawncache", L"dawnwebgpucache",
				L"dawnwebgpu", L"shadercache", L"grshadercache", L"graphitedawncache",
				L"service worker", L"shared dictionary", L"indexeddb/blob_storage" ,
			};
			std::wstring lower{name};
			std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return std::towlower(c); });
			return std::find(subCacheDirs.begin(), subCacheDirs.end(), std::wstring_view{lower}) != subCacheDirs.end();
		}

		// QYWX 数据目录下"数字命名"的企业数据目录（企业 ID，如 1688857791795750）
		bool is_wxwork_enterprise_dir(std::wstring_view name)
		{
			if (name.size() < 5)
			{
				return false;
			}
			return std::all_of(name.begin(), name.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
		}

		// 聊天记录目录：Data 消息库 / Index 搜索索引
		bool is_wxwork_chat_dir(std::wstring_view name)
		{
			return _wcsicmp(name.data(), L"Data") == 0 || _wcsicmp(name.data(), L"Index") == 0;
		}

		// 遍历环境目录下所有 WXWork 用户数据目录（Documents/Roaming 等重定向下各一处）
		void for_each_wxwork_dir(const std::filesystem::path& envDir,
		                         const std::function<void(const std::filesystem::path&)>& fn)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			const auto walk = [&](const fs::path& dir, const auto& self) -> void
			{
				for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
				{
					if (ec)
					{
						ec.clear();
						continue;
					}
					if (!entry.is_directory(ec))
					{
						ec.clear();
						continue;
					}
					ec.clear();
					const std::wstring name = entry.path().filename().native();
					if (_wcsicmp(name.c_str(), L"wxwork") == 0)
					{
						fn(entry.path());
						continue;
					}
					self(entry.path(), self);
				}
			};
			walk(envDir, walk);
		}

		// 收集 WXWork 数据目录子树内所有可清理的 CEF 缓存目录：
		//  - qtCef/WXWorkCefCache 等整目录删除；
		//  - Default profile 仅删其子缓存目录（Cache/Code Cache/GPU 着色器等），
		//    保留 Cookies/Local Storage/Preferences 等登录相关文件。
		void collect_cache_dirs(const std::filesystem::path& envDir, std::vector<std::filesystem::path>& out)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			for_each_wxwork_dir(envDir, [&](const fs::path& wxDir)
			{
				for (const auto& subEntry : fs::recursive_directory_iterator(wxDir, fs::directory_options::skip_permission_denied, ec))
				{
					if (ec)
					{
						ec.clear();
						continue;
					}
					if (!subEntry.is_directory(ec))
					{
						ec.clear();
						continue;
					}
					ec.clear();
					const std::wstring dirName = subEntry.path().filename().native();
					if (is_wxwork_cache_dir(dirName))
					{
						out.push_back(subEntry.path());
						continue;
					}
					// Default 目录本身不删，但收集其下的子缓存目录
					if (_wcsicmp(dirName.c_str(), L"default") == 0)
					{
						for (const auto& subSubEntry : fs::directory_iterator(subEntry.path(), fs::directory_options::skip_permission_denied, ec))
						{
							if (ec)
							{
								ec.clear();
								continue;
							}
							if (subSubEntry.is_directory(ec))
							{
								if (!ec && is_default_sub_cache_dir(subSubEntry.path().filename().native()))
								{
									out.push_back(subSubEntry.path());
								}
								ec.clear();
							}
						}
					}
				}
			});
		}

		// 收集各数字企业目录下的聊天记录目录（Data/Index，仅一级子目录）
		void collect_chat_dirs(const std::filesystem::path& envDir, std::vector<std::filesystem::path>& out)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			for_each_wxwork_dir(envDir, [&](const fs::path& wxDir)
			{
				for (const auto& subEntry : fs::directory_iterator(wxDir, fs::directory_options::skip_permission_denied, ec))
				{
					if (ec)
					{
						ec.clear();
						continue;
					}
					if (!subEntry.is_directory(ec))
					{
						ec.clear();
						continue;
					}
					ec.clear();
					if (!is_wxwork_enterprise_dir(subEntry.path().filename().native()))
					{
						continue;
					}
					for (const auto& dataEntry : fs::directory_iterator(subEntry.path(), fs::directory_options::skip_permission_denied, ec))
					{
						if (ec)
						{
							ec.clear();
							continue;
						}
						if (dataEntry.is_directory(ec) && !ec && is_wxwork_chat_dir(dataEntry.path().filename().native()))
						{
							out.push_back(dataEntry.path());
						}
						ec.clear();
					}
				}
			});
		}

		// 累计目录内全部文件大小（不删除）
		std::uint64_t dirs_total_size(const std::vector<std::filesystem::path>& dirs)
		{
			namespace fs = std::filesystem;
			std::uint64_t total = 0;
			std::error_code ec;
			for (const fs::path& dir : dirs)
			{
				for (const auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
				{
					if (ec)
					{
						ec.clear();
						continue;
					}
					if (entry.is_regular_file(ec))
					{
						if (!ec)
						{
							total += entry.file_size(ec);
						}
						ec.clear();
					}
				}
			}
			return total;
		}

		// 累计目录内文件大小后整体删除（删除失败忽略：文件正被运行中进程占用）
		std::uint64_t remove_dirs_with_size(const std::vector<std::filesystem::path>& dirs)
		{
			namespace fs = std::filesystem;
			std::uint64_t freed = 0;
			std::error_code ec;
			for (const fs::path& dir : dirs)
			{
				for (const auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
				{
					if (ec)
					{
						ec.clear();
						continue;
					}
					if (entry.is_regular_file(ec))
					{
						if (!ec)
						{
							freed += entry.file_size(ec);
						}
						ec.clear();
					}
				}
				fs::remove_all(dir, ec);
			}
			return freed;
		}
	}

	std::uint64_t Env::getWxworkCacheSize() const
	{
		namespace fs = std::filesystem;
		const fs::path envDir{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", m_index)})};
		if (!fs::exists(envDir))
		{
			return 0;
		}
		std::vector<fs::path> dirs;
		collect_cache_dirs(envDir, dirs);
		return dirs_total_size(dirs);
	}

	std::uint64_t Env::getWxworkChatDataSize() const
	{
		namespace fs = std::filesystem;
		const fs::path envDir{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", m_index)})};
		if (!fs::exists(envDir))
		{
			return 0;
		}
		std::vector<fs::path> dirs;
		collect_chat_dirs(envDir, dirs);
		return dirs_total_size(dirs);
	}

	std::uint64_t Env::getEnvDataSize() const
	{
		namespace fs = std::filesystem;
		const fs::path envDir{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", m_index)})};
		std::uint64_t total = 0;
		std::error_code ec;
		for (const auto& entry : fs::recursive_directory_iterator(envDir, fs::directory_options::skip_permission_denied, ec))
		{
			if (ec)
			{
				ec.clear();
				continue;
			}
			if (entry.is_regular_file(ec))
			{
				if (!ec)
				{
					total += entry.file_size(ec);
				}
				ec.clear();
			}
		}
		return total;
	}

	std::uint64_t Env::cleanWxworkCache() const
	{
		namespace fs = std::filesystem;
		const fs::path envDir{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", m_index)})};
		if (!fs::exists(envDir))
		{
			return 0;
		}
		std::vector<fs::path> dirs;
		collect_cache_dirs(envDir, dirs);
		return remove_dirs_with_size(dirs);
	}

	std::uint64_t Env::cleanWxworkChatData() const
	{
		namespace fs = std::filesystem;
		const fs::path envDir{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", m_index)})};
		if (!fs::exists(envDir))
		{
			return 0;
		}
		std::vector<fs::path> dirs;
		collect_chat_dirs(envDir, dirs);
		return remove_dirs_with_size(dirs);
	}

	std::wstring Env::registryHiveSubKey() const
	{
		return std::format(L"eBox_Env_{}", m_index);
	}

	std::filesystem::path Env::registryHivePath() const
	{
		namespace fs = std::filesystem;
		return fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", m_index)} / m_flagName);
	}

	bool Env::ensureRegistryHiveFile()
	{
		namespace fs = std::filesystem;
		const fs::path hivePath = registryHivePath();
		std::error_code ec;
		if (fs::exists(hivePath, ec) && !ec)
		{
			// 已存在但大小为 0 视为无效（历史上创建失败的残留），重建
			if (fs::file_size(hivePath, ec) > 0)
			{
				return true;
			}
		}
		if (!fs::exists(hivePath.parent_path(), ec))
		{
			fs::create_directories(hivePath.parent_path(), ec);
		}
		enable_privilege(SE_BACKUP_NAME);
		return create_empty_hive_file(hivePath.native());
	}

	bool Env::loadRegistryHive()
	{
		if (m_hiveLoaded)
		{
			return true;
		}
		namespace fs = std::filesystem;
		const fs::path hivePath = registryHivePath();
		// 文件不存在/无效时先创建（环境首次使用或历史残留 0 字节文件）
		if (!ensureRegistryHiveFile())
		{
			return false;
		}
		// 已加载则直接复用
		HKEY chk = nullptr;
		if (RegOpenKeyExW(HKEY_USERS, registryHiveSubKey().c_str(), 0, KEY_QUERY_VALUE, &chk) == ERROR_SUCCESS)
		{
			RegCloseKey(chk);
			m_hiveLoaded = true;
			return true;
		}
		enable_privilege(SE_RESTORE_NAME);
		if (RegLoadKeyW(HKEY_USERS, registryHiveSubKey().c_str(), hivePath.native().c_str()) != ERROR_SUCCESS)
		{
			// 旧 hive 文件可能损坏或不兼容（如旧版 RegLoadAppKeyW 生成的临时 hive），
			// 重建空 hive 再加载一次
			std::error_code removeEc;
			fs::remove(hivePath, removeEc);
			enable_privilege(SE_BACKUP_NAME);
			if (!create_empty_hive_file(hivePath.native()))
			{
				return false;
			}
			enable_privilege(SE_RESTORE_NAME);
			if (RegLoadKeyW(HKEY_USERS, registryHiveSubKey().c_str(), hivePath.native().c_str()) != ERROR_SUCCESS)
			{
				return false;
			}
		}
		m_hiveLoaded = true;
		return true;
	}

	bool Env::saveRegistryHive()
	{
		if (!m_hiveLoaded)
		{
			return false;
		}
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_USERS, registryHiveSubKey().c_str(), 0, KEY_ALL_ACCESS, &hKey) != ERROR_SUCCESS)
		{
			return false;
		}
		namespace fs = std::filesystem;
		const fs::path hivePath = registryHivePath();
		// hive 加载期间源文件被系统锁定，不能直接覆盖；先保存到 .tmp，
		// 卸载（RegUnLoadKeyW 释放文件锁）后再替换为正式文件
		const fs::path tmpPath = fs::path{hivePath.native() + L".tmp"};
		std::error_code ec;
		fs::remove(tmpPath, ec);
		enable_privilege(SE_BACKUP_NAME);
		const bool ok = RegSaveKeyW(hKey, tmpPath.native().c_str(), nullptr) == ERROR_SUCCESS;
		RegCloseKey(hKey);
		m_pendingHiveFile = ok ? tmpPath.native() : std::wstring{};
		return ok;
	}

	void Env::commitPendingHiveFile()
	{
		if (m_pendingHiveFile.empty())
		{
			return;
		}
		namespace fs = std::filesystem;
		std::error_code ec;
		const fs::path hivePath = registryHivePath();
		fs::remove(hivePath, ec);
		fs::rename(m_pendingHiveFile, hivePath, ec);
		m_pendingHiveFile.clear();
	}

	bool Env::unloadRegistryHive()
	{
		if (m_hiveLoaded)
		{
			enable_privilege(SE_RESTORE_NAME);
			RegUnLoadKeyW(HKEY_USERS, registryHiveSubKey().c_str());
			m_hiveLoaded = false;
		}
		// 卸载释放了 hive 文件锁，此时才把 .tmp 落盘替换为正式文件
		commitPendingHiveFile();
		return true;
	}

	std::shared_ptr<ProcessInfo> Env::addProcess(DWORD pid)
	{
		const std::shared_ptr<ProcessInfo> newProcInfo = std::make_shared<ProcessInfo>(pid);
		if (addProcessInternal(newProcInfo))
		{
			m_waiter.addWait(newProcInfo->getHandle(), [this, newProcInfo]
			{
				removeProcessInternal(newProcInfo);
				removeToplevelWindowWhenProcessTerminate(newProcInfo);
			});
			return newProcInfo;
		}
		return nullptr;
	}

	std::size_t Env::getAllProcessesCount() const
	{
		std::shared_lock lock(m_mutex);
		return m_processes.getCount();
	}

	std::vector<std::shared_ptr<ProcessInfo>> Env::getAllProcesses() const
	{
		std::shared_lock lock(m_mutex);
		return m_processes.getAllProcesses();
	}

	std::shared_ptr<ProcessInfo> Env::getProcess(DWORD pid) const
	{
		std::shared_lock lock(m_mutex);
		return m_processes.getProcessInfo(pid);
	}

	std::vector<DWORD> Env::getAllProcessIds() const
	{
		std::shared_lock lock(m_mutex);
		return m_processes.getPids();
	}

	bool Env::contains(const std::wstring& procFullName) const
	{
		std::shared_lock lock(m_mutex);
		return m_processes.contains(procFullName);
	}

	void Env::setProcCountChangeNotify(ProcCountChangeNotify notify)
	{
		std::unique_lock lock(m_mutex);
		m_notify = std::move(notify);
	}

	void Env::addToplevelWindow(DWORD pid, void* hWnd)
	{
		std::shared_ptr<ProcessInfo> proc = getProcess(pid);
		if (!proc)
		{
			return;
		}
		{
			std::unique_lock lock(m_wndMutex);
			m_toplevelWindows.addTopLevelWindow(hWnd);
			proc->addToplevelWindow(hWnd);
		}
		// 应用首个顶层窗口出现 = 首次数据初始化完成，立即清除“首次初始化中”
		// 提示（无需再等待固定时长）；窗口已出现后进程被关闭也不再显示提示。
		if (isFirstLaunchPending())
		{
			markFirstLaunchDone();
		}
	}

	void Env::removeToplevelWindow(DWORD pid, void* hWnd)
	{
		std::shared_ptr<ProcessInfo> proc = getProcess(pid);
		if (!proc)
		{
			return;
		}
		std::unique_lock lock(m_wndMutex);
		m_toplevelWindows.removeTopLevelWindow(hWnd);
		proc->removeToplevelWindow(hWnd);
	}

	bool Env::containsToplevelWindow(void* hWnd) const
	{
		std::shared_lock lock(m_wndMutex);
		return m_toplevelWindows.contains(hWnd);
	}

	std::vector<void*> Env::getAllToplevelWindows() const
	{
		std::shared_lock lock(m_wndMutex);
		return m_toplevelWindows.getAllHandles();
	}

	std::filesystem::path Env::firstLaunchFilePath() const
	{
		namespace fs = std::filesystem;
		return fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", m_index)} / L"first_launch.pending");
	}

	bool Env::isFirstLaunchPending() const
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		return fs::exists(firstLaunchFilePath(), ec) && !ec;
	}

	void Env::markFirstLaunchDone()
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		fs::remove(firstLaunchFilePath(), ec);
		m_firstProcStartTick = 0;
	}

	bool Env::shouldClearFirstLaunchPending() const
	{
		if (!isFirstLaunchPending())
		{
			return false;
		}
		// 进程已全部退出（应用未启动或被用户关闭）或已出现顶层窗口（应用已正常
		// 启动），即视为首次初始化完成，立即清除提示，不再等待固定时长。
		if (m_processes.getCount() == 0)
		{
			return true;
		}
		return !getAllToplevelWindows().empty();
	}

	bool Env::isFirstLaunchTipActive() const
	{
		// 从未发生首次启动（老环境正常启动）不显示
		if (m_firstLaunchTipStartTick == 0)
		{
			return false;
		}
		// 进程已全部退出（应用未启动或被用户关闭）：立即消失
		if (m_processes.getCount() == 0)
		{
			return false;
		}
		// 窗口尚未出现：仍处于首次数据初始化中，持续展示提示
		if (getAllToplevelWindows().empty())
		{
			return true;
		}
		// 窗口已出现（首次初始化完成）：继续展示满 30 秒再消失
		return (GetTickCount64() - m_firstLaunchTipStartTick) < 30000;
	}

	bool Env::addProcessInternal(const std::shared_ptr<ProcessInfo>& procInfo)
	{
		ProcCountChangeNotify notify;
		std::size_t count = 0;
		{
			// 锁内只更新容器并取出回调副本；文件系统访问与用户回调一律放到锁外。
			// 否则长临界区（isFirstLaunchPending 的 weakly_canonical/fs::exists）或锁内
			// 回调会把所有共享读者（getAllProcessIds / getAllProcesses 等）堵死，
			// 使 UI 线程无法泵消息而出现“未响应”，这正是启停环境时卡死的根因。
			std::unique_lock lock(m_mutex);
			if (!m_processes.addProcessInfo(procInfo))
			{
				return false;
			}
			count = m_processes.getCount();
			notify = m_notify;
		}
		// 首次启动计时起点：pending 未完成时记录首个进程加入时刻
		if (isFirstLaunchPending())
		{
			std::unique_lock lock(m_mutex);
			if (m_firstProcStartTick == 0)
			{
				m_firstProcStartTick = GetTickCount64();
				// 进程区温馨提示计时起点：与 m_firstProcStartTick 同刻，但不受
				// markFirstLaunchDone 清零影响（窗口出现后进程区提示展示满 30 秒）
				m_firstLaunchTipStartTick = m_firstProcStartTick;
			}
		}
		if (notify)
		{
			notify(EProcEvent::Create, procInfo, count);
		}
		return true;
	}

	bool Env::removeProcessInternal(const std::shared_ptr<ProcessInfo>& procInfo)
	{
		std::size_t remaining = 0;
		bool removed = false;
		ProcCountChangeNotify notify;
		{
			// 锁内只更新容器并取出回调副本；用户回调放到锁外执行。
			// 避免在 env 的共享互斥锁上运行回调（spawn 协程 / addTask / 日志）时
			// 长时间占住独占锁，把共享读者全部堵住。
			std::unique_lock lock(m_mutex);
			removed = m_processes.removeProcessInfoById(procInfo->getProcessId());
			if (removed)
			{
				notify = m_notify;
				remaining = m_processes.getCount();
			}
		}
		if (removed && notify)
		{
			notify(EProcEvent::Terminate, procInfo, remaining);
		}
		// 环境内进程全部退出后保存注册表 hive 作为检查点。
		// 注意：不能在此卸载 hive！cmd /c start 启动应用时 cmd 会立即退出，
		// 若此时卸载 hive，正在初始化的子进程（如 WXWork）将无法打开
		// HKU\eBox_Env_<idx> 而被 TerminateProcess 秒杀。
		// hive 保持加载，仅在 eBox 退出 / 删除环境时卸载。
		if (removed && remaining == 0)
		{
			saveRegistryHive();
			// 首次启动完成判定：进程全部退出（cmd 中转进程退出时应用已完成注入，
			// 故此处即为应用未启动或被用户关闭）即清除 pending 标记；
			// 窗口出现则由 addToplevelWindow 路径即时清除
			if (shouldClearFirstLaunchPending())
			{
				markFirstLaunchDone();
			}
		}
		return removed;
	}

	void Env::removeToplevelWindowWhenProcessTerminate(const std::shared_ptr<ProcessInfo>& procInfo)
	{
		std::unique_lock lock(m_wndMutex);
		const std::unordered_set<void*>& allToplevelWindows = procInfo->getToplevelWindows();
		for (void* hWnd : allToplevelWindows)
		{
			m_toplevelWindows.removeTopLevelWindow(hWnd);
		}
	}
}
