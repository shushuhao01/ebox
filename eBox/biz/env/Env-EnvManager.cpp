module;
#include <shlobj.h>
#include <objbase.h>
module Env;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import std;
import MainApp;
import UI.MainWindow;
import :Reg;
import EssentialData;
import Utility.SystemInfo;
import EnvLog;
import biz.License;

namespace
{
	namespace fs = std::filesystem;

	std::uint64_t get_random_number()
	{
		thread_local std::mt19937_64 rng{std::random_device{}()};
		std::uniform_int_distribution<std::uint64_t> dis;
		return dis(rng);
	}

	void delete_dir_by_cmd(std::wstring_view dir)
	{
		PROCESS_INFORMATION procInfo = {nullptr};
		STARTUPINFOW startupInfo = {sizeof(startupInfo)};
		startupInfo.dwFlags = STARTF_USESHOWWINDOW;
		startupInfo.wShowWindow = SW_HIDE;
		namespace fs = std::filesystem;
		const fs::path cmdPath{fs::weakly_canonical(fs::path{sys_info::get_system_dir()} / fs::path{L"cmd.exe"})};
		std::wstring cmdLine = std::format(LR"(/c rd /s /q "{}")", dir);
		if (CreateProcessW(cmdPath.c_str(), cmdLine.data(),
		                   nullptr, nullptr, FALSE, CREATE_DEFAULT_ERROR_MODE, nullptr, nullptr,
		                   &startupInfo, &procInfo))
		{
			CloseHandle(procInfo.hThread);
			// 同步等待删除完成（最多 60 秒），确保目录真正移除，避免用户看到
			// "<index>_<flag>_to_delete" 残留文件夹；大目录（几百 MB）删除一般数秒内完成
			WaitForSingleObject(procInfo.hProcess, 60000);
			CloseHandle(procInfo.hProcess);
		}
	}

	// 启动时清理上次删除环境失败/中断遗留的 *_to_delete 残留目录
	void cleanup_delete_residues()
	{
		namespace fs = std::filesystem;
		try
		{
			const fs::path envDir{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"})};
			std::error_code ec;
			if (!fs::exists(envDir, ec) || ec)
			{
				return;
			}
			for (const auto& entry : fs::directory_iterator(envDir, fs::directory_options::skip_permission_denied, ec))
			{
				if (ec)
				{
					ec.clear();
					continue;
				}
				const std::wstring name = entry.path().filename().native();
				if (name.find(L"_to_delete") != std::wstring::npos)
				{
					delete_dir_by_cmd(entry.path().native());
				}
			}
		}
		catch (...)
		{
		}
	}

	void delete_env_dir(std::uint32_t index, std::wstring_view flagName)
	{
		namespace fs = std::filesystem;
		try
		{
			const fs::path envDir{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"})};
			const fs::path envPath{fs::weakly_canonical(envDir / fs::path{std::format(L"{}", index)})};
			const fs::path tempPath{fs::weakly_canonical(envDir / fs::path{std::format(L"{}_{}_to_delete", index, flagName)})};

			if (fs::exists(envPath) && !fs::exists(tempPath))
			{
				fs::rename(envPath, tempPath);
				delete_dir_by_cmd(tempPath.native());
			}
		}
		catch (...)
		{
		}
	}

	// 生成随机 machine_id（32 位十六进制，与 Chromium 生成的格式一致）
	std::wstring generate_machine_id()
	{
		GUID guid{};
		if (CoCreateGuid(&guid) != S_OK)
		{
			return {};
		}
		return std::format(L"{:08x}{:04x}{:04x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
		                   guid.Data1, guid.Data2, guid.Data3,
		                   guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		                   guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
	}

	// 判断是否应跳过拷贝的残留文件（Chromium 原子写残留 / SQLite 日志，源机本身是脏数据，不该带入环境）
	bool is_residue_file(std::wstring_view name)
	{
		return name.find(L"~RF") != std::wstring_view::npos
			|| name.ends_with(L"-journal")
			|| name.ends_with(L"-wal")
			|| name.ends_with(L"-shm");
	}

	// 单文件安全拷贝（源被占用/不存在时静默跳过，不阻塞环境创建）
	void copy_file_no_throw(const fs::path& src, const fs::path& dst)
	{
		try
		{
			std::error_code ec;
			if (fs::exists(src, ec) && !ec)
			{
				fs::create_directories(dst.parent_path(), ec);
				fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
			}
		}
		catch (...)
		{
		}
	}

	// 递归收集待拷贝的普通文件列表（目录在拷贝时按需创建）
	// skipTopDirs：跳过 src 下指定的顶层子目录。用于跳过可自行重建的缓存目录
	// （CEF 的 Cache/Code Cache/GPUCache、CDN 下载缓存等），这些数据拷贝过去
	// 反而要占用新建环境大量小文件 IO 时间，且不影响功能——应用首启会自动重建。
	void collect_copy_files(const fs::path& src, const fs::path& dst,
	                        std::vector<std::pair<fs::path, fs::path>>& files,
	                        const std::vector<std::wstring>& skipTopDirs = {})
	{
		std::error_code ec;
		if (!fs::exists(src, ec) || ec)
		{
			return;
		}
		for (const auto& entry : fs::recursive_directory_iterator(src, fs::directory_options::skip_permission_denied, ec))
		{
			if (ec)
			{
				ec.clear();
				continue;
			}
			const fs::path rel = entry.path().lexically_relative(src);
			if (is_residue_file(rel.filename().native()))
			{
				continue;
			}
			if (!skipTopDirs.empty())
			{
				const auto firstSeg = rel.begin();
				if (firstSeg != rel.end())
				{
					const std::wstring top = firstSeg->native();
					if (std::find(skipTopDirs.begin(), skipTopDirs.end(), top) != skipTopDirs.end())
					{
						continue;
					}
				}
			}
			if (entry.is_regular_file())
			{
				files.emplace_back(entry.path(), dst / rel);
			}
		}
	}

	// 多线程并行拷贝目录（跳过残留文件），失败的文件跳过（源机可能被 WXWork 占用）。
	// CEF 框架缓存（Default 等）含大量小文件，串行 fs::copy_file 在慢电脑上耗时十几秒，
	// 并行拷贝可显著缩短新环境首启前的数据预置时间。
	void copy_dir_skip_residue(const fs::path& src, const fs::path& dst,
	                           const std::vector<std::wstring>& skipTopDirs = {})
	{
		std::vector<std::pair<fs::path, fs::path>> files;
		collect_copy_files(src, dst, files, skipTopDirs);
		if (files.empty())
		{
			return;
		}
		const unsigned nThreads = std::clamp<unsigned>(std::thread::hardware_concurrency(), 2, 8);
		std::atomic<std::size_t> next{0};
		std::vector<std::thread> workers;
		workers.reserve(nThreads);
		for (unsigned t = 0; t < nThreads; ++t)
		{
			workers.emplace_back([&files, &next]
			{
				while (true)
				{
					const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
					if (i >= files.size())
					{
						break;
					}
					std::error_code ec;
					fs::create_directories(files[i].second.parent_path(), ec);
					fs::copy_file(files[i].first, files[i].second, fs::copy_options::overwrite_existing, ec);
				}
			});
		}
		for (auto& worker : workers)
		{
			worker.join();
		}
	}

	// 预拷贝 QYWX 核心数据（Global 全局配置 + Profiles 各企业目录顶层配置/数据库
	// + CEF 运行时缓存目录）。跳过 qtCef 与聊天子目录缓存。
	// 目的：新建环境首启时 WXWork 能直接读到企业配置快速出窗口。
	// 实测结论（重要）：
	//  - qtCef 渲染缓存（Code Cache/Cache 等）绑定 CEF 设备标识，新环境拷贝后
	//    CEF 启动时反而要扫描校验重建 → 首启更慢（环境19 无qtCef 18s vs 环境20 有qtCef 30s），
	//    故【不预拷 qtCef】；其缓存由 WXWork 首启自行生成，二次启动即快（几秒）。
	//  - Default/ShaderCache 等 CEF 框架缓存拷贝后无副作用，预拷以加速框架初始化。
	void precopy_wxwork_core(const fs::path& srcWxWork, const fs::path& dstWxWork)
	{
		// Global：全局配置（config.db 等），量小且关键，整体预拷贝。
		// 跳过 CDNcdn（含 download 目录 1496 个小文件/5.8MB）——纯 CDN 下载缓存，
		// 应用会自动重新下载，预拷贝徒增新建环境的小文件 IO 时间，不影响功能。
		copy_dir_skip_residue(srcWxWork / fs::path{L"Global"}, dstWxWork / fs::path{L"Global"},
		                      {L"CDNcdn"});
		// CEF 框架 profile（Default 38MB）与着色器/GPU 缓存，缺失时每次启动都要重建。
		// Default 内跳过可重建的 CEF 缓存目录（Cache/Code Cache/GPUCache 等，首启自动重建），
		// 保留 Extensions（功能组件）、Local Storage/Network（Cookies/Login Data）等
		// 登录态与配置数据——不影响登录与功能。Profiles\<hash> 的登录态在下方单独预拷。
		copy_dir_skip_residue(srcWxWork / fs::path{L"Default"}, dstWxWork / fs::path{L"Default"},
		                      {L"Cache", L"Code Cache", L"GPUCache",
		                       L"DawnGraphiteCache", L"DawnWebGPUCache"});
		static constexpr std::wstring_view cefCacheDirs[] = {
			L"ShaderCache", L"GrShaderCache", L"GraphiteDawnCache",
			L"BrowserMetrics", L"segmentation_platform", L"Dictionaries",
		};
		for (const std::wstring_view dir : cefCacheDirs)
		{
			copy_dir_skip_residue(srcWxWork / fs::path{dir}, dstWxWork / fs::path{dir});
		}
		// Profiles\<hash>：只预拷贝顶层配置文件（io_data.json/setting.json/*.db），子目录（聊天缓存）不拷
		const fs::path srcProfiles = srcWxWork / fs::path{L"Profiles"};
		const fs::path dstProfiles = dstWxWork / fs::path{L"Profiles"};
		std::error_code ec;
		if (!fs::exists(srcProfiles, ec) || ec)
		{
			return;
		}
		for (const auto& entry : fs::directory_iterator(srcProfiles, fs::directory_options::skip_permission_denied, ec))
		{
			if (ec)
			{
				ec.clear();
				continue;
			}
			if (!entry.is_directory())
			{
				continue;
			}
			const std::wstring hash = entry.path().filename().native();
			const fs::path dstHashDir = dstProfiles / hash;
			fs::create_directories(dstHashDir, ec);
			for (const auto& f : fs::directory_iterator(entry.path(), fs::directory_options::skip_permission_denied, ec))
			{
				if (ec)
				{
					ec.clear();
					continue;
				}
				if (!f.is_regular_file())
				{
					continue;
				}
				const std::wstring fileName = f.path().filename().native();
				if (is_residue_file(fileName))
				{
					continue;
				}
				copy_file_no_throw(f.path(), dstHashDir / fileName);
			}
		}
	}

	// 创建环境时预生成设备指纹与核心数据（不必等应用启动）：
	//  1. Local State——以源机完整 Local State 为模板，仅替换 machine_id 为环境独立值。
	//     保留 os_crypt.encrypted_key 等完整结构，避免 CEF 因 Local State 残缺而"全新初始化"
	//     （表现为主窗口迟迟不出现、反复触发 WXWorkRepair、偶发本地数据加载异常）。
	//  2. WXWork 核心数据预拷贝（Global + Profiles 顶层配置），首启快速加载企业。
	//  3. 注册表 hive 文件——由 Env::loadRegistryHive 在 createEnv 时预创建
	// 注：qimei 由腾讯组件在应用启动时生成，这里不做。
	void precreate_env_fingerprint(const std::shared_ptr<biz::Env>& env)
	{
		if (!env)
		{
			return;
		}
		namespace fs = std::filesystem;
		try
		{
			// 原生 Documents 路径（如 C:\Users\xxx\Documents 或 D:\backup\documents）
			PWSTR docs = nullptr;
			if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs) != S_OK || !docs)
			{
				return;
			}
			const fs::path docsPath{docs};
			CoTaskMemFree(docs);
			// 去掉盘符得到重定向相对路径（与 MemoryDll 的重定向规则一致）
			const fs::path rel = docsPath.relative_path();
			if (rel.empty())
			{
				return;
			}
			const fs::path localStatePath = fs::weakly_canonical(
				fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", env->getIndex())} / rel / fs::path{L"WXWork"} / fs::path{L"Local State"});
			std::error_code ec;
			if (fs::exists(localStatePath, ec) && !ec)
			{
				return; // 已存在（如已启动过），不覆盖
			}
			const std::wstring machineId = generate_machine_id();
			if (machineId.empty())
			{
				return;
			}
			// machine_id 是 32 位十六进制 ASCII，逐字符窄化无信息丢失
			std::string machineIdUtf8;
			machineIdUtf8.reserve(machineId.size());
			for (const wchar_t c : machineId)
			{
				machineIdUtf8.push_back(static_cast<char>(c));
			}
			fs::create_directories(localStatePath.parent_path(), ec);

			// 1) Local State：以源机完整版为模板替换 machine_id
			const fs::path srcWxWorkDir = docsPath / fs::path{L"WXWork"};
			const fs::path srcLocalState = srcWxWorkDir / fs::path{L"Local State"};
			const fs::path envWxWorkDir = localStatePath.parent_path();
			bool bTemplateUsed = false;
			if (fs::exists(srcLocalState, ec) && !ec)
			{
				try
				{
					std::ifstream in{srcLocalState, std::ios::binary};
					if (in)
					{
						std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
						in.close();
						// Chromium 格式 "machine_id":"<32hex>"（注意别误替换 user_experience_metrics 里的数字 machine_id）
						std::regex machineIdRegex("\"machine_id\":\"([0-9a-fA-F]+)\"");
						std::string newContent = std::regex_replace(content, machineIdRegex,
							"\"machine_id\":\"" + machineIdUtf8 + "\"");
						if (newContent != content)
						{
							std::ofstream out{localStatePath, std::ios::binary | std::ios::trunc};
							if (out)
							{
								out.write(newContent.data(), static_cast<std::streamsize>(newContent.size()));
								bTemplateUsed = true;
							}
						}
					}
				}
				catch (...)
				{
				}
			}
			if (!bTemplateUsed)
			{
				// 无源机模板或写入失败：退回最简 Local State（仅 machine_id）
				const std::string json = std::format("{{\n  \"machine_id\": \"{}\"\n}}\n", machineIdUtf8);
				std::ofstream out{localStatePath, std::ios::binary | std::ios::trunc};
				if (out)
				{
					out.write(json.data(), static_cast<std::streamsize>(json.size()));
				}
			}

			// 2) WXWork 核心数据预拷贝（Global + Profiles 顶层配置）
			precopy_wxwork_core(srcWxWorkDir, envWxWorkDir);
		}
		catch (...)
		{
		}
		// 预创建注册表 hive 文件（加载失败不影响环境创建，DLL 侧有回退）
		env->ensureRegistryHiveFile();
	}
}

namespace biz
{
	EnvManager::EnvManager()
	{
		// 清理上次删除环境失败/中断遗留的 *_to_delete 残留目录（启动时执行，避免越积越多）
		cleanup_delete_residues();
		initialize_env_reg([this](const EnvInitializeData& data)
		{
			loadEnvFrom(data.index, data.flag, data.flagName, data.name, data.appPath);
		});
	}

	void EnvManager::loadEnvFrom(std::uint32_t index, std::uint64_t flag, std::wstring_view flagName, std::wstring_view name, std::wstring_view appPath)
	{
		if (index >= m_currentIndex.load(std::memory_order_relaxed))
		{
			m_currentIndex.store(index + 1, std::memory_order_relaxed);
		}
		namespace fs = std::filesystem;
		const fs::path envPath{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", index)})};
		fs::create_directories(envPath);
		std::shared_ptr<Env> env = std::make_shared<Env>(index, flag, flagName, name, appPath);
		addEnv(env);
		// 为已存在的环境补生成设备指纹（升级场景：旧环境可能没有 Local State / hive 文件）
		precreate_env_fingerprint(env);
	}

	std::shared_ptr<Env> EnvManager::createEnv()
	{
		if (getEnvCount() >= 100)
		{
			throw std::runtime_error("最多只能创建100个环境");
		}
		// ===== 授权检查：到期后禁止新增环境 =====
		if (!biz::license::canLaunch())
		{
			throw std::runtime_error("授权已到期，无法新增环境。请联系作者续期激活码。");
		}
		std::shared_ptr<Env> envResult;
		const std::uint32_t index = m_currentIndex.fetch_add(1, std::memory_order_relaxed);
		auto [flag, flagName] = ensureCreateNewEnvFlag(index);
		const std::wstring name = std::format(L"环境{}", index);
		envResult = std::make_shared<Env>(index, flag, flagName, name);
		add_env_to_reg(flagName, envResult.get());
		addEnv(envResult);
		// 标记"首次启动未完成"：UI 在进程区显示"首次启动稍慢，请稍候"提示，
		// 该环境进程首次持续运行超过阈值后自动清除
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			fs::create_directories(envResult->firstLaunchFilePath().parent_path(), ec);
			std::ofstream ofs{envResult->firstLaunchFilePath(), std::ios::binary | std::ios::trunc};
			if (ofs)
			{
				ofs << "pending";
			}
		}
		// 创建环境时即预生成设备指纹（Local State/machine_id + 注册表 hive 文件），
		// 无需等待应用启动；qimei 由腾讯组件在应用启动时生成
		precreate_env_fingerprint(envResult);
		env_logger().append(index, EnvLogType::Info, EnvLogStatus::Info,
		                    L"新建环境", std::format(L"环境{} 已创建", index));
		return envResult;
	}

	std::shared_ptr<Env> EnvManager::findEnvByFlagNoExcept(std::uint64_t flag) const
	{
		std::shared_lock lock(m_mutex);
		const auto it = m_flagToEnv.find(flag);
		if (it == m_flagToEnv.end())
		{
			return nullptr;
		}
		return it->second;
	}

	std::shared_ptr<Env> EnvManager::findEnvByFlag(std::uint64_t flag) const
	{
		std::shared_ptr<Env> result = findEnvByFlagNoExcept(flag);
		if (!result)
		{
			throw std::runtime_error(std::format("Failed to find env, flagName:{:016X}", flag));
		}
		return result;
	}

	std::size_t EnvManager::getEnvCount() const
	{
		std::shared_lock lock(m_mutex);
		return m_flagToEnv.size();
	}

	void EnvManager::setEnvAppPath(std::shared_ptr<Env> env, std::wstring_view appPath)
	{
		if (!env)
		{
			return;
		}
		try
		{
			env->setAppPath(appPath);
			save_env_app_path_to_reg(env->getFlagName(), appPath);
		}
		catch (...)
		{
		}
	}

	void EnvManager::setLastEnvForProc(std::wstring_view procFullPath, const std::shared_ptr<Env>& env)
	{
		if (!env)
		{
			return;
		}
		try
		{
			save_proc_last_env(procFullPath, env->getFlag());
		}
		catch (...)
		{
		}
	}

	std::shared_ptr<Env> EnvManager::getLastEnvForProc(std::wstring_view procFullPath) const
	{
		try
		{
			if (std::optional<std::uint64_t> flag = load_proc_last_env(procFullPath); flag.has_value())
			{
				return findEnvByFlagNoExcept(flag.value());
			}
		}
		catch (...)
		{
		}
		return nullptr;
	}

	bool EnvManager::renameEnv(std::shared_ptr<Env> env, std::wstring_view newName)
	{
		if (!env || newName.empty())
		{
			return false;
		}
		try
		{
			env->setName(newName);
			save_env_name_to_reg(env->getFlagName(), newName);
			env_logger().append(env->getIndex(), EnvLogType::Info, EnvLogStatus::Success,
			                    L"重命名环境", std::format(L"{} -> {}", env->getName(), newName));
			return true;
		}
		catch (...)
		{
			env_logger().append(env->getIndex(), EnvLogType::Error, EnvLogStatus::Fail,
			                    L"重命名环境失败", std::wstring{newName});
		}
		return false;
	}

	// ReSharper disable once CppPassValueParameterByConstReference
	void EnvManager::deleteEnv(std::shared_ptr<Env> env)
	{
		env_logger().append(env->getIndex(), EnvLogType::Info, EnvLogStatus::Info,
		                    L"删除环境", std::format(L"环境{} 及其数据将被删除", env->getIndex()));
		env->deleteDllFromDevice();
		// 若注册表 hive 仍加载，先卸载再删除目录，否则 hive 文件被系统锁定无法删除
		env->unloadRegistryHive();
		removeEnv(env->getFlag());
		delete_env_dir(env->getIndex(), env->getFlagName());
		delete_env_from_reg(env->getFlagName());
		// 删除环境后同步清理该环境的日志（内存缓存 + 磁盘文件），避免残留历史日志
		env_logger().clear(env->getIndex());
	}

	bool EnvManager::containsProcessIdExclude(std::uint32_t pid, std::uint64_t excludeEnvFlag) const
	{
		if (GetCurrentProcessId() == pid)
		{
			return true;
		}
		// 锁内只拷贝 env 指针快照（极短临界区），锁外再逐个 env 查询。
		// 之前是在全局共享锁内遍历 m_flagToEnv 并嵌套各 Env 锁，长临界区 +
		// 写者优先会使所有争用该锁的线程（含 UI）排长队，形成锁护送/饥饿。
		for (const auto& env : getAllEnv())
		{
			if (env->getFlag() != excludeEnvFlag && env->getProcess(pid))
			{
				return true;
			}
		}
		return false;
	}

	std::vector<DWORD> EnvManager::getAllProcessIdsExclude(std::uint64_t excludeEnvFlag) const
	{
		// 锁内只拷贝快照，锁外再逐 env 收集，避免长临界区阻塞其它线程
		const auto envs = getAllEnv();
		std::vector<DWORD> result;
		result.reserve(envs.size() * 2);
		for (const auto& env : envs)
		{
			if (env->getFlag() == excludeEnvFlag)
			{
				continue;
			}
			std::vector<DWORD> temp = env->getAllProcessIds();
			result.insert(result.end(), temp.begin(), temp.end());
		}
		result.push_back(GetCurrentProcessId());
		return result;
	}

	bool EnvManager::containsToplevelWindowExclude(void* hWnd, std::uint64_t excludeEnvFlag) const
	{
		if (hWnd == ui::main_wnd().nativeHandle())
		{
			return true;
		}
		// 锁内只拷贝快照，锁外再逐 env 查询，避免长临界区阻塞其它线程
		for (const auto& env : getAllEnv())
		{
			if (env->getFlag() != excludeEnvFlag && env->containsToplevelWindow(hWnd))
			{
				return true;
			}
		}
		return false;
	}

	std::vector<void*> EnvManager::getAllToplevelWindows() const
	{
		// 锁内只拷贝快照，锁外再逐 env 收集，避免长临界区阻塞其它线程
		const auto envs = getAllEnv();
		std::vector<void*> result;
		result.reserve(envs.size() * 4);
		for (const auto& env : envs)
		{
			std::vector<void*> temp = env->getAllToplevelWindows();
			result.insert(result.end(), temp.begin(), temp.end());
		}
		return result;
	}

	std::vector<void*> EnvManager::getAllToplevelWindowsExclude(std::uint64_t excludeEnvFlag) const
	{
		// 锁内只拷贝快照，锁外再逐 env 收集，避免长临界区阻塞其它线程
		const auto envs = getAllEnv();
		std::vector<void*> result;
		result.reserve(envs.size() * 4);
		for (const auto& env : envs)
		{
			if (env->getFlag() == excludeEnvFlag)
			{
				continue;
			}
			std::vector<void*> temp = env->getAllToplevelWindows();
			result.insert(result.end(), temp.begin(), temp.end());
		}
		return result;
	}

	std::vector<std::shared_ptr<Env>> EnvManager::getAllEnv() const
	{
		std::vector<std::shared_ptr<Env>> result;
		std::shared_lock lock(m_mutex);
		result.reserve(m_flagToEnv.size());
		for (auto it = m_flagToEnv.begin(); it != m_flagToEnv.end(); ++it)
		{
			result.push_back(it->second);
		}
		return result;
	}

	void EnvManager::setEnvChangeNotify(EnvChangeNotify envChangeNotify)
	{
		std::unique_lock lock(m_mutex);
		m_envChangeNotify = std::move(envChangeNotify);
	}

	EnvManager::EnvFlagInfo EnvManager::ensureCreateNewEnvFlag(std::uint32_t index) const
	{
		namespace fs = std::filesystem;
		// envPath 与循环无关：weakly_canonical + create_directories 只做一次，
		// 避免放在循环内随重试反复做文件 I/O。
		const fs::path envPath{fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env"} / fs::path{std::format(L"{}", index)})};
		fs::create_directories(envPath);

		EnvFlagInfo result;
		while (true)
		{
			const std::uint64_t flag = get_random_number();
			const std::wstring flagName = std::format(L"{:016X}", flag);
			// 先查文件再查内存：envPath 已 canonical 且 flagName 为 16 位 HEX（无 "./.." 特殊段），
			// 直接拼接即可，省掉原实现每轮一次的 weakly_canonical I/O；文件冲突时不碰锁。
			const fs::path envFile{envPath / fs::path{flagName}};
			std::error_code ec;
			if (fs::exists(envFile, ec))
			{
				continue;
			}
			// 唯一一次 shared_lock 查询：64 位随机 flag 冲突概率极低，正常 1 次通过。
			if (findEnvByFlagNoExcept(flag))
			{
				continue;
			}

			result.flag = flag;
			result.flagName = flagName;
			break;
		}
		return result;
	}

	void EnvManager::addEnv(const std::shared_ptr<Env>& env)
	{
		EnvChangeNotify notify;
		{
			std::unique_lock lock(m_mutex);
			if (!m_flagToEnv.insert(std::make_pair(env->getFlag(), env)).second)
			{
				throw std::runtime_error("add env failed! env flag error!");
			}
			notify = m_envChangeNotify;
		}
		// 通知移出锁外，缩短写者临界区，避免阻塞大量读者（RPC 热路径）
		if (notify)
		{
			notify(EChangeType::Create, env);
		}
	}

	void EnvManager::removeEnv(std::uint64_t flag)
	{
		std::shared_ptr<Env> removed;
		EnvChangeNotify notify;
		{
			std::unique_lock lock(m_mutex);
			const auto it = m_flagToEnv.find(flag);
			if (it == m_flagToEnv.end())
			{
				throw std::runtime_error("remove env failed! can't find env flag!");
			}
			removed = it->second;
			notify = m_envChangeNotify;
			m_flagToEnv.erase(it);
		}
		// 通知移出锁外，缩短写者临界区，避免阻塞大量读者（RPC 热路径）
		if (notify)
		{
			notify(EChangeType::Delete, removed);
		}
	}
}
