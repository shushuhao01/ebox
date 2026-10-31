module Env;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import MainApp;
import EssentialData;
import EnvLog;

namespace biz
{
	namespace
	{
		// hive 备份文件后缀：<hive>.bad.<yyyyMMdd-HHmmss>，便于事后排查又不会覆盖历史备份
		std::wstring backup_timestamp_suffix()
		{
			SYSTEMTIME st{};
			GetLocalTime(&st);
			return std::format(L"{:04}{:02}{:02}-{:02}{:02}{:02}",
			                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		}

		// RegLoadKeyW 的"暂时性"失败：文件正被杀软扫描、被其它进程短暂映射时会出现。
		// 这类失败不代表 hive 损坏，重试通常能成功，不能因此去备份并重建（那会清空环境登录态）。
		bool is_transient_hive_error(LSTATUS status)
		{
			switch (status)
			{
			case ERROR_SHARING_VIOLATION:
			case ERROR_LOCK_VIOLATION:
			case ERROR_ACCESS_DENIED:
			case ERROR_BUSY:
				return true;
			default:
				return false;
			}
		}

		// 权限类失败：RegLoadKeyW 需要 SeRestorePrivilege、RegSaveKeyW 需要 SeBackupPrivilege，
		// 未以管理员身份运行时访问令牌里根本没有这两项权限，RegLoadKeyW 返回
		// 1314(ERROR_PRIVILEGE_NOT_HELD)；权限被组策略移除时返回 1300。
		// 这类失败与 hive 文件本身无关，绝不能备份重建——那等于把用户现有登录态换成一个空环境。
		bool is_privilege_hive_error(LSTATUS status)
		{
			return status == ERROR_PRIVILEGE_NOT_HELD || status == ERROR_NOT_ALL_ASSIGNED;
		}

		// 把 hive 文件的"大小 + 最后写入时间"整理成一行日志。
		// 重启后是否要重新扫码，取决于本次启动加载到的是不是关机前最新那份快照；
		// 把两个文件的实际状态写进日志，复现时就能直接区分"关机前没存"和"存了没提升"。
		std::wstring describe_hive_file(const std::filesystem::path& path)
		{
			WIN32_FILE_ATTRIBUTE_DATA attr{};
			if (!GetFileAttributesExW(path.native().c_str(), GetFileExInfoStandard, &attr))
			{
				return L"不存在";
			}
			FILETIME local{};
			FileTimeToLocalFileTime(&attr.ftLastWriteTime, &local);
			SYSTEMTIME st{};
			FileTimeToSystemTime(&local, &st);
			const std::uint64_t size = (static_cast<std::uint64_t>(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
			return std::format(L"{} 字节 / 写入 {:04}-{:02}-{:02} {:02}:{:02}:{:02}",
			                   size, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		}

		// 把 1314 翻译成用户能照做的动作，避免只看到"错误码 1314"无从下手
		std::wstring hive_privilege_hint()
		{
			HANDLE token = nullptr;
			if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
			{
				return L"无法查询进程令牌权限";
			}
			TOKEN_ELEVATION elevation{};
			DWORD size = sizeof(elevation);
			const bool queried = GetTokenInformation(token, TokenElevation, &elevation, size, &size) != FALSE;
			CloseHandle(token);
			if (queried && !elevation.TokenIsElevated)
			{
				return L"当前进程未以管理员身份运行，请以管理员身份重新运行 eBox";
			}
			return L"当前进程令牌中没有 SeRestorePrivilege / SeBackupPrivilege（可能被安全策略移除）";
		}

		// 加载 hive：暂时性错误重试 3 次（累计最多约 1.2 秒），其余错误立即返回
		LSTATUS load_hive_with_retry(const std::wstring& subKey, const std::wstring& hivePath)
		{
			LSTATUS status = ERROR_SUCCESS;
			for (int attempt = 0; attempt < 3; ++attempt)
			{
				status = RegLoadKeyW(HKEY_USERS, subKey.c_str(), hivePath.c_str());
				if (status == ERROR_SUCCESS || !is_transient_hive_error(status))
				{
					return status;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds{200 * (attempt + 1)});
			}
			return status;
		}

		// 写环境级错误日志。EnvLogger 由 Biz.Core 初始化，极端早期（构造前）访问会解引用空指针，
		// 这里统一做空指针保护。
		void log_env_error(const Env& env, std::wstring_view action, std::wstring_view detail)
		{
			if (!g_env_logger)
			{
				return;
			}
			env_logger().append(env.getIndex(), EnvLogType::Error, EnvLogStatus::Fail, action, detail);
		}

		// 写环境级提示日志（普通用户模式等非故障信息，避免被当成错误）
		void log_env_info(const Env& env, std::wstring_view action, std::wstring_view detail)
		{
			if (!g_env_logger)
			{
				return;
			}
			env_logger().append(env.getIndex(), EnvLogType::Info, EnvLogStatus::Info, action, detail);
		}

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
		// 返回 LSTATUS：调用方需要区分"权限不足(1314)"与"其它失败"，前者不可反复重试/删文件。
		LSTATUS create_empty_hive_file(const std::wstring& hivePath)
		{
			constexpr wchar_t tempSubKey[] = L"Software\\eBox_Env_Temp";
			RegDeleteKeyW(HKEY_CURRENT_USER, tempSubKey);
			HKEY hTemp = nullptr;
			const LSTATUS createStatus = RegCreateKeyExW(HKEY_CURRENT_USER, tempSubKey, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hTemp, nullptr);
			if (createStatus != ERROR_SUCCESS)
			{
				return createStatus;
			}
			const LSTATUS saveStatus = RegSaveKeyW(hTemp, hivePath.c_str(), nullptr);
			RegCloseKey(hTemp);
			RegDeleteKeyW(HKEY_CURRENT_USER, tempSubKey);
			return saveStatus;
		}
		// 注册表 hive 保存/卸载的后台执行器。
		//
		// RegSaveKeyW 与 RegUnLoadKeyW 都是不可取消的内核同步操作，耗时随该 hive 的
		// 脏页量增长（豆包这类 Electron 应用频繁写注册表，单次可达数秒）。之前它们
		// 直接跑在调用线程上，造成两类卡顿：
		//   1. 进程退出回调（线程池线程）里同步 RegSaveKeyW：线程池被长时间占用，
		//      后续进程/窗口事件迟迟得不到处理，表现为整体卡顿；
		//   2. 上面这些操作一旦落到 UI 线程，超过看门狗的 4s 阈值即判定"卡死"。
		// 现在统一丢到一条懒启动的后台线程串行执行，调用线程立即返回。
		class HiveWorker
		{
		public:
			static HiveWorker& instance()
			{
				static HiveWorker worker;
				return worker;
			}

			void post(std::function<void()> job)
			{
				{
					std::lock_guard lock(m_mutex);
					m_jobs.push_back(std::move(job));
				}
				m_cv.notify_one();
			}

		private:
			HiveWorker()
			{
				// 进程退出由 ExitProcess 兜底，无需 stop/join
				std::thread([this] { run(); }).detach();
			}

			void run()
			{
				std::unique_lock lock(m_mutex);
				for (;;)
				{
					m_cv.wait(lock, [this] { return !m_jobs.empty(); });
					std::function<void()> job = std::move(m_jobs.front());
					m_jobs.pop_front();
					lock.unlock();
					job();
					lock.lock();
				}
			}

			std::mutex m_mutex;
			std::condition_variable m_cv;
			std::deque<std::function<void()>> m_jobs;
		};
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

	// 目标文件内容是否与内嵌数据完全一致：先比大小，再逐块比较（不整文件读入内存）。
	bool embedded_file_matches(const std::filesystem::path& target, const char* address, unsigned int size)
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		if (!fs::exists(target, ec) || ec)
		{
			return false;
		}
		if (const auto fileSize = fs::file_size(target, ec); ec || fileSize != size)
		{
			return false;
		}
		std::ifstream targetFile{target, std::ios::binary};
		if (!targetFile)
		{
			return false;
		}
		constexpr std::size_t BLOCK = 64 * 1024;
		std::vector<char> buf(BLOCK);
		std::size_t offset = 0;
		while (offset < size)
		{
			const std::size_t want = (std::min)(BLOCK, static_cast<std::size_t>(size) - offset);
			targetFile.read(buf.data(), static_cast<std::streamsize>(want));
			if (static_cast<std::size_t>(targetFile.gcount()) != want)
			{
				return false;
			}
			if (!std::equal(buf.data(), buf.data() + want, address + offset))
			{
				return false;
			}
			offset += want;
		}
		return true;
	}

	// 将内嵌资源写入目标文件（内容一致时跳过），供 DLL / 注入器 exe 共用。
	// 先与内存中的内嵌数据比较：一致就完全不碰磁盘，避免每次启动都新建一个
	// 700KB 的临时 PE 文件（新文件必然被杀软实时扫描，拖慢启动）。
	// 仅按文件大小判断"未变化"不可靠（新旧文件大小可能恰好相同，导致旧版
	// 永远不被覆盖），因此仍逐块比较内容。
	// 若文件正被运行中的进程占用（覆盖失败），则保留旧文件，不影响已注入的进程。
	void write_embedded_file(const std::filesystem::path& target, const char* address, unsigned int size)
	{
		namespace fs = std::filesystem;
		if (embedded_file_matches(target, address, size))
		{
			return;
		}
		std::error_code ec;
		// 临时文件使用进程唯一后缀，避免多线程/多实例并发写同一 .temp 的竞态
		const fs::path tempWithSuffix = std::format(L"{}.{}.temp", target.native(), GetCurrentProcessId());
		{
			std::ofstream tempFile{tempWithSuffix, std::ios::binary | std::ios::trunc};
			tempFile.write(address, size);
			tempFile.close();
		}
		if (!MoveFileExW(tempWithSuffix.native().c_str(), target.native().c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			fs::remove(tempWithSuffix, ec);
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
		const fs::path path32{get_dll_full_path<ArchBit::Bit32>(binDir, m_flagName)};
		write_embedded_file(path32, get_core_data().dll32.address, get_core_data().dll32.size);
		const fs::path path64{get_dll_full_path<ArchBit::Bit64>(binDir, m_flagName)};
		write_embedded_file(path64, get_core_data().dll64.address, get_core_data().dll64.size);

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

	std::wstring Env::ensureInjector32InDeviceAndReturnPath() const
	{
		namespace fs = std::filesystem;
		// 与 DLL 同目录（envDataRoot\bin），避免写入带重定向或权限受限的目录
		const fs::path binDir{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"bin"})};
		if (!fs::exists(binDir))
		{
			fs::create_directories(binDir);
		}
		const fs::path injectorPath{binDir / fs::path{L"eBoxInject32.exe"}};
		write_embedded_file(injectorPath, get_core_data().exe32.address, get_core_data().exe32.size);
		return injectorPath.native();
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

	// 普通用户模式下的环境注册表存储位置（注入 DLL 侧使用同一路径，见 MemoryDll GlobalData::initializeRegistry）
	static std::wstring user_registry_store_subkey(std::uint32_t index)
	{
		return std::format(L"Software\\eBox\\EnvRegistry\\{}", index);
	}

	// 该环境的用户注册表存储是否已存在。
	// 存在即说明此环境曾以普通用户身份运行过，之后固定使用它：否则同一环境在
	// "这次提权、下次不提权"之间会同时存在 hive 与用户注册表两份数据，来回切换
	// 会让用户每次都要重新扫码。
	static bool user_registry_store_exists(std::uint32_t index)
	{
		const std::wstring storeKey = user_registry_store_subkey(index);
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, storeKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		{
			return false;
		}
		RegCloseKey(hKey);
		return true;
	}

	bool Env::useUserRegistryStore()
	{
		// 未提权时 RegLoadKeyW（需 SeRestorePrivilege）/ RegSaveKeyW（需 SeBackupPrivilege）
		// 一定失败，hive 方案对普通用户不可用。用户配置注册表本身就是"自动落盘的注册表"，
		// 把环境虚拟注册表挂在这里即可：无需任何权限、重启后仍在、环境内所有进程共享同一份。
		const std::wstring storeKey = user_registry_store_subkey(m_index);
		HKEY hKey = nullptr;
		const LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, storeKey.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
		if (hKey)
		{
			RegCloseKey(hKey);
		}
		if (status != ERROR_SUCCESS)
		{
			log_env_error(*this, L"注册表", std::format(L"用户注册表存储初始化失败（错误码 {}）：HKCU\\{}；本次为临时模式，数据不持久化", static_cast<int>(status), storeKey));
			return false;
		}
		m_userRegistryStore = true;
		// 对 UI 与退出流程而言等价于"注册表已就绪"（不需要保存/卸载）
		m_hiveLoaded = true;
		log_env_info(*this, L"注册表", std::format(L"当前以普通用户运行，环境注册表使用用户注册表存储（HKCU\\{}），登录态随用户配置自动保存，重启后无需重新扫码", storeKey));
		return true;
	}

	void Env::deleteUserRegistryStore() const
	{
		const std::wstring storeKey = user_registry_store_subkey(m_index);
		RegDeleteTreeW(HKEY_CURRENT_USER, storeKey.c_str());
	}

	bool Env::ensureRegistryHiveFile()
	{
		// 与 load/save/unload 串行化：创建环境时的预创建与随后的启动加载可能并发，
		// 两个线程同时 RegSaveKeyW 同一路径会让其中一个失败（甚至写出不完整的 hive）。
		std::lock_guard hiveLock(m_hiveMutex);
		return ensureRegistryHiveFileLocked();
	}

	bool Env::ensureRegistryHiveFileLocked(LSTATUS* outStatus)
	{
		namespace fs = std::filesystem;
		const fs::path hivePath = registryHivePath();
		std::error_code ec;
		if (fs::exists(hivePath, ec) && !ec)
		{
			// 已存在但大小为 0 视为无效（历史上创建失败的残留），重建。
			// 注意 RegSaveKeyW 不允许覆盖已存在的文件，必须先删掉这个 0 字节残留，
			// 否则重建永远失败、该环境会永久退化为"数据不持久化"。
			if (fs::file_size(hivePath, ec) > 0)
			{
				if (outStatus)
				{
					*outStatus = ERROR_SUCCESS;
				}
				return true;
			}
			std::error_code removeEc;
			fs::remove(hivePath, removeEc);
		}
		if (!fs::exists(hivePath.parent_path(), ec))
		{
			fs::create_directories(hivePath.parent_path(), ec);
		}
		enable_privilege(SE_BACKUP_NAME);
		const LSTATUS status = create_empty_hive_file(hivePath.native());
		if (outStatus)
		{
			*outStatus = status;
		}
		return status == ERROR_SUCCESS;
	}

	// 把上次未完成落盘的 <hive>.tmp 提升为正式 hive 文件。
	//
	// 正式 hive 文件只在 unloadRegistryHive（RegUnLoadKeyW 释放文件锁后）被 .tmp 替换；
	// 若进程在关机/被杀时没走完卸载流程，.tmp 里才是最新的注册表快照，而正式文件仍是
	// 旧数据（甚至已被删除）。这里在加载前优先采用 .tmp，避免"重启后登录态丢失"。
	void Env::promotePendingHiveFile()
	{
		namespace fs = std::filesystem;
		const fs::path hivePath = registryHivePath();
		const fs::path tmpPath = fs::path{hivePath.native() + L".tmp"};
		const fs::path oldPath = fs::path{hivePath.native() + L".old"};
		std::error_code ec;
		if (!fs::exists(tmpPath, ec) || ec)
		{
			return;
		}
		if (fs::file_size(tmpPath, ec) == 0 || ec)
		{
			// 空/读不到大小：是上次保存失败的残留，直接清掉
			std::error_code removeEc;
			fs::remove(tmpPath, removeEc);
			return;
		}
		// 开机瞬间杀毒软件常会打开正式 hive 文件做扫描，此时删除/改名会短暂返回共享冲突。
		// 提升失败会让本次启动加载到"旧"的正式文件——企业微信随即要求重新扫码，
		// 因此这里必须重试，而不是失败一次就退回旧文件。
		//
		// 不能"先删正式 hive 再改名 .tmp"：remove 成功而 rename 失败时正式 hive 就没了，
		// 本次启动只能新建空 hive，随后的检查点还会覆盖掉完好的 .tmp。
		// 改成先把正式 hive 改名成 .old（可回退），提升成功后再删 .old。
		std::error_code lastEc;
		for (int attempt = 0; attempt < 8; ++attempt)
		{
			// 上一轮可能留下 .old，先清掉
			std::error_code removeOldEc;
			fs::remove(oldPath, removeOldEc);

			bool backedUp = false;
			std::error_code existsEc;
			if (fs::exists(hivePath, existsEc) && !existsEc)
			{
				std::error_code backupEc;
				fs::rename(hivePath, oldPath, backupEc);
				if (backupEc)
				{
					// 正式 hive 改名失败（多为杀软占用）：原文件未动，稍后重试
					lastEc = backupEc;
					if (attempt + 1 < 8)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds{250});
					}
					continue;
				}
				backedUp = true;
			}

			std::error_code renameEc;
			fs::rename(tmpPath, hivePath, renameEc);
			if (!renameEc)
			{
				std::error_code removeBackupEc;
				fs::remove(oldPath, removeBackupEc);
				log_env_info(*this, L"注册表", std::format(L"已采用关机前准备的 hive 快照：{}", describe_hive_file(hivePath)));
				return;
			}
			lastEc = renameEc;
			if (backedUp)
			{
				// 提升失败：把原正式 hive 放回去，保证本次启动仍能加载旧快照
				std::error_code restoreEc;
				fs::rename(oldPath, hivePath, restoreEc);
			}
			if (attempt + 1 < 8)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds{250});
			}
		}
		log_env_error(*this, L"注册表", std::format(L"hive 临时快照提升失败（重试 8 次，错误码 {}）：{}；本次将加载旧快照 {}，环境登录态可能回退",
		                                            lastEc.value(), describe_hive_file(tmpPath), describe_hive_file(hivePath)));
	}

	bool Env::loadRegistryHive()
	{
		// 有界等待锁：hive 锁可能正被后台 HiveWorker 的 RegSaveKeyW / RegUnLoadKeyW 占用，
		// 而这两个内核调用在杀软实时扫描下可能持续十几秒。启动流程不能因此被卡住——
		// 拿不到锁就让调用方回退为进程级虚拟注册表（DLL 侧已支持），宁可这次不持久化。
		std::unique_lock hiveLock(m_hiveMutex, std::defer_lock);
		if (!hiveLock.try_lock_for(std::chrono::seconds{2}))
		{
			log_env_error(*this, L"注册表", L"hive 正被后台保存/卸载占用（等待超过 2 秒），本次为临时模式，数据不持久化");
			return false;
		}
		if (m_hiveLoaded)
		{
			return true;
		}
		// 该环境已在用用户注册表存储：固定沿用，不再走 hive（避免提权/未提权交替启动时两套存储来回切换）
		if (user_registry_store_exists(m_index))
		{
			return useUserRegistryStore();
		}
		// 已加载则直接复用。必须先于 promotePendingHiveFile 判断：hive 仍挂载时它的
		// 文件被内核锁定，此时去删/改名只会失败，还会留下一条误导性的错误日志。
		HKEY chk = nullptr;
		if (RegOpenKeyExW(HKEY_USERS, registryHiveSubKey().c_str(), 0, KEY_QUERY_VALUE, &chk) == ERROR_SUCCESS)
		{
			RegCloseKey(chk);
			m_hiveLoaded = true;
			return true;
		}
		namespace fs = std::filesystem;
		const fs::path hivePath = registryHivePath();
		// 先尝试采用上次未完成落盘的 .tmp（关机/杀进程场景下的最新数据）
		promotePendingHiveFile();
		// 文件不存在/无效时先创建（环境首次使用或历史残留 0 字节文件）
		LSTATUS createStatus = ERROR_SUCCESS;
		if (!ensureRegistryHiveFileLocked(&createStatus))
		{
			if (is_privilege_hive_error(createStatus))
			{
				// 普通用户无法 RegSaveKeyW 创建 hive：改用用户注册表存储（不需要任何权限，同样持久化）
				return useUserRegistryStore();
			}
			log_env_error(*this, L"注册表", std::format(L"hive 文件创建失败：{}（错误码 {}），本次为临时模式，数据不持久化", hivePath.native(), static_cast<int>(createStatus)));
			return false;
		}
		enable_privilege(SE_RESTORE_NAME);
		// 复现"重启后要重新扫码"时的决定性证据：本次真正加载的 hive 有多新、是否还残留未提升的快照。
		// 正式文件写入时间远早于关机时刻且 .tmp 不存在 → 关机前的保存根本没执行（被系统强杀/超时），
		// 而不是加载失败——这两种情况的修复方向完全不同，不能靠猜。
		log_env_info(*this, L"注册表", std::format(L"加载 hive：正式文件 {}；未提升快照 {}",
		                                          describe_hive_file(hivePath),
		                                          describe_hive_file(fs::path{hivePath.native() + L".tmp"})));
		const LSTATUS loadStatus = load_hive_with_retry(registryHiveSubKey(), hivePath.native());
		if (loadStatus != ERROR_SUCCESS)
		{
			if (is_privilege_hive_error(loadStatus))
			{
				// 权限不足（未以管理员身份运行）：hive 文件本身是好的，绝不能备份重建，
				// 否则等于把用户现有登录态换成一个空环境。改为用户注册表存储，仍然持久化。
				return useUserRegistryStore();
			}
			if (is_transient_hive_error(loadStatus))
			{
				// 文件被占用（杀软扫描 / 上一轮进程尚未完全释放对 hive 的映射）：
				// 绝不能备份重建——那等于把用户现有登录态换成一个空环境。
				log_env_error(*this, L"注册表", std::format(L"hive 加载失败（错误码 {}：文件被占用，已重试 3 次）：{}；本次为临时模式，数据不持久化",
				                                            static_cast<int>(loadStatus), hivePath.native()));
				return false;
			}
			// 旧 hive 文件可能损坏或不兼容（如旧版 RegLoadAppKeyW 生成的临时 hive）。
			// 这里【只备份不删除】：改名成 .bad.<时间戳> 保留现场，便于用户事后恢复/排查；
			// 直接删除会让"一次加载失败"永久变成"登录态全丢"。
			const fs::path badPath = fs::path{std::format(L"{}.bad.{}", hivePath.native(), backup_timestamp_suffix())};
			std::error_code renameEc;
			fs::rename(hivePath, badPath, renameEc);
			if (renameEc)
			{
				// 改名失败（文件被占用/权限不足）：不做任何破坏性动作，保持临时模式
				log_env_error(*this, L"注册表", std::format(L"hive 加载失败（错误码 {}），且无法备份为 {}（错误码 {}）；本次为临时模式，数据不持久化",
				                                            static_cast<int>(loadStatus), badPath.native(), renameEc.value()));
				return false;
			}
			enable_privilege(SE_BACKUP_NAME);
			const LSTATUS rebuildStatus = create_empty_hive_file(hivePath.native());
			if (rebuildStatus != ERROR_SUCCESS)
			{
				log_env_error(*this, L"注册表", std::format(L"hive 加载失败（错误码 {}），已备份为 {}，但重建空 hive 失败（错误码 {}）；本次为临时模式，数据不持久化",
				                                            static_cast<int>(loadStatus), badPath.native(), static_cast<int>(rebuildStatus)));
				return false;
			}
			enable_privilege(SE_RESTORE_NAME);
			const LSTATUS retryStatus = load_hive_with_retry(registryHiveSubKey(), hivePath.native());
			if (retryStatus != ERROR_SUCCESS)
			{
				log_env_error(*this, L"注册表", std::format(L"hive 加载失败（错误码 {}），旧文件已备份为 {}，重建后仍加载失败（错误码 {}）；本次为临时模式，数据不持久化",
				                                            static_cast<int>(loadStatus), badPath.native(), static_cast<int>(retryStatus)));
				return false;
			}
			log_env_error(*this, L"注册表", std::format(L"hive 文件损坏或不兼容（错误码 {}），已备份为 {} 并重建空环境，该环境原有登录态已丢失",
			                                            static_cast<int>(loadStatus), badPath.native()));
		}
		m_hiveLoaded = true;
		return true;
	}

	bool Env::saveRegistryHive()
	{
		std::lock_guard hiveLock(m_hiveMutex);
		if (m_userRegistryStore)
		{
			// 用户注册表由 Windows 自动落盘，无需 RegSaveKeyW
			return true;
		}
		if (!m_hiveLoaded)
		{
			return false;
		}
		HKEY hKey = nullptr;
		const LSTATUS openStatus = RegOpenKeyExW(HKEY_USERS, registryHiveSubKey().c_str(), 0, KEY_ALL_ACCESS, &hKey);
		if (openStatus != ERROR_SUCCESS)
		{
			log_env_error(*this, L"注册表", std::format(L"保存 hive 失败：无法打开已加载的 hive（错误码 {}），本次数据未落盘", static_cast<int>(openStatus)));
			return false;
		}
		namespace fs = std::filesystem;
		const fs::path hivePath = registryHivePath();
		// hive 加载期间源文件被系统锁定，不能直接覆盖；先保存到 .tmp，
		// 卸载（RegUnLoadKeyW 释放文件锁）后再替换为正式文件
		const fs::path tmpPath = fs::path{hivePath.native() + L".tmp"};
		// 先写到 .tmp.new，成功后再改名成 .tmp。关机时 Windows 只等几秒就会强杀
		// eBox，若直接写 .tmp，被强杀会留下半截文件，下次启动会被当成有效快照
		// 提升、反而覆盖掉好的正式 hive。改成"写完成才顶替"，半截文件永远不会被提升。
		const fs::path stagingPath = fs::path{hivePath.native() + L".tmp.new"};
		std::error_code ec;
		fs::remove(stagingPath, ec);
		enable_privilege(SE_BACKUP_NAME);
		const LSTATUS saveStatus = RegSaveKeyW(hKey, stagingPath.native().c_str(), nullptr);
		RegCloseKey(hKey);
		bool ok = saveStatus == ERROR_SUCCESS;
		if (ok)
		{
			fs::remove(tmpPath, ec);
			ec.clear();
			fs::rename(stagingPath, tmpPath, ec);
			if (ec)
			{
				ok = false;
				std::error_code removeEc;
				fs::remove(stagingPath, removeEc);
				log_env_error(*this, L"注册表", std::format(L"hive 快照改名失败：{}（错误码 {}），本次注册表变更未落盘", tmpPath.native(), ec.value()));
			}
			else
			{
				m_pendingHiveFile = tmpPath.native();
			}
		}
		else
		{
			// 失败可能留下写了一半的暂存文件，清掉，避免下次加载时被误当成有效快照
			m_pendingHiveFile.clear();
			std::error_code removeEc;
			fs::remove(stagingPath, removeEc);
			if (is_privilege_hive_error(saveStatus))
			{
				log_env_error(*this, L"注册表", std::format(L"保存 hive 失败（错误码 {}）：{}，本次注册表变更未落盘", static_cast<int>(saveStatus), hive_privilege_hint()));
			}
			else
			{
				log_env_error(*this, L"注册表", std::format(L"保存 hive 失败（错误码 {}），本次注册表变更未落盘", static_cast<int>(saveStatus)));
			}
		}
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
		ec.clear();
		fs::rename(m_pendingHiveFile, hivePath, ec);
		if (ec)
		{
			// 保留 .tmp（下次加载时 promotePendingHiveFile 会把它提升为正式文件），不丢数据
			log_env_error(*this, L"注册表", std::format(L"hive 正式文件替换失败：{}（错误码 {}），数据暂存于 .tmp，下次启动时自动恢复", hivePath.native(), ec.value()));
			return;
		}
		m_pendingHiveFile.clear();
	}

	bool Env::unloadRegistryHive()
	{
		std::lock_guard hiveLock(m_hiveMutex);
		if (m_userRegistryStore)
		{
			// 用户注册表没有挂载动作，也就没有卸载；保持 m_hiveLoaded 为 true，
			// 避免下次启动重复尝试 hive（普通用户一定失败）
			return true;
		}
		if (m_hiveLoaded)
		{
			enable_privilege(SE_RESTORE_NAME);
			LSTATUS unloadStatus = RegUnLoadKeyW(HKEY_USERS, registryHiveSubKey().c_str());
			// 环境内进程刚被终止时内核句柄可能还没完全释放，RegUnLoadKeyW 会返回
			// ERROR_ACCESS_DENIED(5)。短暂重试，避免 hive 长期挂着、正式文件一直不更新。
			for (int attempt = 0; unloadStatus != ERROR_SUCCESS && attempt < 4; ++attempt)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds{200});
				unloadStatus = RegUnLoadKeyW(HKEY_USERS, registryHiveSubKey().c_str());
			}
			if (unloadStatus != ERROR_SUCCESS)
			{
				// hive 仍挂载（通常是环境内进程还没退干净）：保留 m_hiveLoaded，
				// 下次仍可保存并再次尝试卸载；本次数据已写入 .tmp，
				// 下次启动由 promotePendingHiveFile 提升为正式文件，不会丢。
				log_env_error(*this, L"注册表", std::format(L"hive 卸载失败（错误码 {}），数据已暂存于 .tmp，下次启动时自动恢复", static_cast<int>(unloadStatus)));
				return false;
			}
			m_hiveLoaded = false;
		}
		// 卸载释放了 hive 文件锁，此时才把 .tmp 落盘替换为正式文件
		commitPendingHiveFile();
		return true;
	}

	void Env::saveRegistryHiveAsync()
	{
		if (!m_hiveLoaded || m_userRegistryStore)
		{
			return;
		}
		// 已排队/执行中的请求直接合并：任务执行时读到的是当时的注册表内容，
		// 期间的变更都会被这一次覆盖，无需重复保存。
		bool expected = false;
		if (!m_saveQueued.compare_exchange_strong(expected, true))
		{
			return;
		}
		// 持有 shared_ptr 保活：环境在保存任务执行期间被删除也不会访问已释放对象。
		std::shared_ptr<Env> self = shared_from_this();
		HiveWorker::instance().post([self]
		{
			self->saveRegistryHive();
			self->m_saveQueued = false;
		});
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
			// 异步保存：RegSaveKeyW 在进程退出回调的线程池线程上同步执行会堵住
			// 后续进程/窗口事件（多开 Electron 时表现为整体卡顿），改为后台执行。
			saveRegistryHiveAsync();
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
