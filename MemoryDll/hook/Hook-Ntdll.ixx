// ReSharper disable CppInconsistentNaming
// ReSharper disable CommentTypo
module;
#include <ntstatus.h>
export module Hook:Ntdll;

import "sys_defs.h";
import "hook_cache.h";
import :Core;
import std;
import GlobalData;
import Utility.SystemInfo;
import RpcClient;
import DynamicWin32Api;

namespace hook
{
	enum class ChangePolicy : std::uint8_t
	{
		ForceChange,
		TryToChange
	};

	template <auto trampoline, ChangePolicy Policy, typename... Args>
	NTSTATUS NTAPI ChangeObjNameThenTrampoline(POBJECT_ATTRIBUTES ObjectAttributes, Args&&... args)
	{
		if (ObjectAttributes && ObjectAttributes->ObjectName && ObjectAttributes->ObjectName->Buffer && ObjectAttributes->ObjectName->Length)
		{
			const PUNICODE_STRING pOldName = ObjectAttributes->ObjectName;
			std::wstring strNewName = std::format(L"{}{}", std::wstring_view{pOldName->Buffer, pOldName->Length / sizeof(wchar_t)}, global::Data::get().envFlagName());
			UNICODE_STRING newObjName;
			newObjName.Buffer = strNewName.data();
			newObjName.Length = newObjName.MaximumLength = static_cast<USHORT>(strNewName.length() * sizeof(wchar_t));
			ObjectAttributes->ObjectName = &newObjName;
			const NTSTATUS ret = trampoline(std::forward<Args>(args)...);
			// restore to old name
			ObjectAttributes->ObjectName = pOldName;
			// 如果需要强制改名，则直接返回刚才的结果即可
			if constexpr (Policy == ChangePolicy::ForceChange)
			{
				return ret;
			}
			// 如果只是尝试改名
			else
			{
				// 那么如果改名后接口调用失败，则使用原名再试一次
				if (!NT_SUCCESS(ret))
				{
					return trampoline(std::forward<Args>(args)...);
				}
				return ret;
			}
		}
		// 匿名对象直接调用原函数即可
		return trampoline(std::forward<Args>(args)...);
	}

	//////////////////////////////////////////////////////////////////////////
	//event
	template <auto trampoline>
	NTSTATUS NTAPI NtCreateEvent(OUT PHANDLE EventHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes, IN int EventType, IN BOOLEAN InitialState)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::ForceChange>(ObjectAttributes, EventHandle, DesiredAccess, ObjectAttributes, EventType, InitialState);
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtOpenEvent(OUT PHANDLE EventHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::TryToChange>(ObjectAttributes, EventHandle, DesiredAccess, ObjectAttributes);
	}

	//////////////////////////////////////////////////////////////////////////
	//mutant
	template <auto trampoline>
	NTSTATUS NTAPI NtCreateMutant(OUT PHANDLE MutantHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes, IN BOOLEAN InitialOwner)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::ForceChange>(ObjectAttributes, MutantHandle, DesiredAccess, ObjectAttributes, InitialOwner);
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtOpenMutant(OUT PHANDLE MutantHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::TryToChange>(ObjectAttributes, MutantHandle, DesiredAccess, ObjectAttributes);
	}

	//////////////////////////////////////////////////////////////////////////
	//section
	template <auto trampoline>
	NTSTATUS NTAPI NtCreateSection(OUT PHANDLE SectionHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes,
	                               IN PLARGE_INTEGER SectionSize OPTIONAL, IN ULONG Protect, IN ULONG Attributes, IN HANDLE FileHandle)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::ForceChange>(ObjectAttributes, SectionHandle, DesiredAccess, ObjectAttributes, SectionSize, Protect, Attributes, FileHandle);
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtOpenSection(OUT PHANDLE SectionHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::TryToChange>(ObjectAttributes, SectionHandle, DesiredAccess, ObjectAttributes);
	}

	//////////////////////////////////////////////////////////////////////////
	//semaphore
	template <auto trampoline>
	NTSTATUS NTAPI NtCreateSemaphore(OUT PHANDLE SemaphoreHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes, IN LONG InitialCount, IN LONG MaximumCount)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::ForceChange>(ObjectAttributes, SemaphoreHandle, DesiredAccess, ObjectAttributes, InitialCount, MaximumCount);
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtOpenSemaphore(OUT PHANDLE SemaphoreHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::TryToChange>(ObjectAttributes, SemaphoreHandle, DesiredAccess, ObjectAttributes);
	}

	//////////////////////////////////////////////////////////////////////////
	//timer
	template <auto trampoline>
	NTSTATUS NTAPI NtCreateTimer(OUT PHANDLE TimerHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes, IN int TimerType)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::ForceChange>(ObjectAttributes, TimerHandle, DesiredAccess, ObjectAttributes, TimerType);
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtOpenTimer(OUT PHANDLE TimerHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::TryToChange>(ObjectAttributes, TimerHandle, DesiredAccess, ObjectAttributes);
	}

	//////////////////////////////////////////////////////////////////////////
	//job
	template <auto trampoline>
	NTSTATUS NTAPI NtCreateJobObject(OUT PHANDLE JobHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::ForceChange>(ObjectAttributes, JobHandle, DesiredAccess, ObjectAttributes);
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtOpenJobObject(OUT PHANDLE JobHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::TryToChange>(ObjectAttributes, JobHandle, DesiredAccess, ObjectAttributes);
	}

	//////////////////////////////////////////////////////////////////////////
	//named pipe
	template <auto trampoline>
	NTSTATUS NTAPI NtCreateNamedPipeFile(_Out_ PHANDLE FileHandle, _In_ ULONG DesiredAccess, _In_ POBJECT_ATTRIBUTES ObjectAttributes,
	                                     _Out_ PIO_STATUS_BLOCK IoStatusBlock, _In_ ULONG ShareAccess, _In_ ULONG CreateDisposition,
	                                     _In_ ULONG CreateOptions, _In_ ULONG NamedPipeType, _In_ ULONG ReadMode,
	                                     _In_ ULONG CompletionMode, _In_ ULONG MaximumInstances, _In_ ULONG InboundQuota,
	                                     _In_ ULONG OutboundQuota, _In_opt_ PLARGE_INTEGER DefaultTimeout)
	{
		return ChangeObjNameThenTrampoline<trampoline, ChangePolicy::ForceChange>(ObjectAttributes, FileHandle, DesiredAccess, ObjectAttributes,
		                                                                          IoStatusBlock, ShareAccess, CreateDisposition,
		                                                                          CreateOptions, NamedPipeType, ReadMode,
		                                                                          CompletionMode, MaximumInstances, InboundQuota,
		                                                                          OutboundQuota, DefaultTimeout);
	}

	// enum OBJECT_INFORMATION_CLASS
	// {
	// 	ObjectBasicInformation, // q: OBJECT_BASIC_INFORMATION
	// 	ObjectNameInformation, // q: OBJECT_NAME_INFORMATION
	// 	ObjectTypeInformation, // q: OBJECT_TYPE_INFORMATION
	// 	ObjectTypesInformation, // q: OBJECT_TYPES_INFORMATION
	// 	ObjectHandleFlagInformation, // qs: OBJECT_HANDLE_FLAG_INFORMATION
	// 	ObjectSessionInformation, // s: void // change object session // (requires SeTcbPrivilege)
	// 	ObjectSessionObjectInformation, // s: void // change object session // (requires SeTcbPrivilege)
	// 	ObjectSetRefTraceInformation, // since 25H2
	// 	MaxObjectInfoClass
	// };
	//
	// struct OBJECT_NAME_INFORMATION
	// {

	// 	 UNICODE_STRING Name; // The object name (when present) includes a NULL-terminator and all path separators "\" in the name.

	// };

	//
	// inline win32_api::ApiProxy<utils::make_literal_name<L"ntdll">(), utils::make_literal_name<"NtQueryObject">(), NTSTATUS (NTAPI)(
	// 	                           _In_opt_ HANDLE h,
	// 	                           _In_ OBJECT_INFORMATION_CLASS ObjectInformationClass,
	// 	                           _Out_writes_bytes_opt_(ObjectInformationLength) PVOID ObjectInformation,
	// 	                           _In_ ULONG ObjectInformationLength,
	// 	                           _Out_opt_ PULONG ReturnLength)> NtQueryObject;
	//
	// std::wstring get_object_name(HANDLE object)
	// {
	// 	std::wstring result;
	// 	if (NtQueryObject)
	// 	{
	// 		std::vector<std::byte> buffer;
	// 		ULONG ReturnLength{0};
	// 		NtQueryObject(object, ObjectNameInformation, nullptr, 0, &ReturnLength);
	// 		if (ReturnLength)
	// 		{
	// 			buffer.resize(ReturnLength);
	// 			if (NT_SUCCESS(NtQueryObject(object, ObjectNameInformation, buffer.data(), ReturnLength, nullptr)))
	// 			{
	// 				OBJECT_NAME_INFORMATION& info = *reinterpret_cast<OBJECT_NAME_INFORMATION*>(buffer.data());
	// 				result = std::wstring_view{info.Name.Buffer, info.Name.Length / sizeof(wchar_t)};
	// 			}
	// 		}
	// 	}
	// 	return result;
	// }

	// std::wstring get_file_name_by_handle(HANDLE handle)
	// {
	// 	std::wstring result;
	// 	if (const DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, 0))
	// 	{
	// 		result.resize(size);
	// 		if (!GetFinalPathNameByHandleW(handle, result.data(), size, 0))
	// 		{
	// 			result.clear();
	// 		}
	// 	}
	// 	return result;
	// }

	inline win32_api::ApiProxy<utils::make_literal_name<L"ntdll">(), utils::make_literal_name<"NtClose">(), NTSTATUS (NTAPI)(_In_ _Post_ptr_invalid_ HANDLE Handle)> NtClose;

	// 通过句柄获取文件/目录的完整路径（格式与 \??\ 前缀一致，便于后续重定向判断）
	std::wstring get_file_name_by_handle(HANDLE handle)
	{
		std::wstring result;
		if (!handle)
		{
			return result;
		}
		if (const DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED))
		{
			result.resize(size);
			if (GetFinalPathNameByHandleW(handle, result.data(), size, FILE_NAME_NORMALIZED))
			{
				// GetFinalPathNameByHandleW 返回 \\?\ 前缀，统一成 \??\ 便于匹配
				if (result.starts_with(L"\\\\?\\"))
				{
					result = std::wstring{L"\\??\\"} + result.substr(4);
				}
			}
			else
			{
				result.clear();
			}
		}
		return result;
	}

	std::wstring_view viewFileObjectName(POBJECT_ATTRIBUTES ObjectAttributes)
	{
		do
		{
			if (!ObjectAttributes)
			{
				break;
			}
			if (ObjectAttributes->RootDirectory)
			{
				// 需要考虑吗?
				// std::wcout << std::format(L"====================> {}\n", get_object_name(ObjectAttributes->RootDirectory));
				break;
			}
			if (!ObjectAttributes->ObjectName
				|| !ObjectAttributes->ObjectName->Buffer
				|| !ObjectAttributes->ObjectName->Length)
			{
				break;
			}
			return {ObjectAttributes->ObjectName->Buffer, ObjectAttributes->ObjectName->Length / sizeof(wchar_t)};
		}
		while (false);
		return {};
	}

	// 环境内目标是否已有"有效"数据（存在且非空）。
	// 避免把上次运行中断残留的 0 字节文件误判为"已初始化"，
	// 否则切换主体企业时应用基于空文件继续 → 本地数据加载异常。
	bool redirect_target_has_data(std::wstring_view redirectPath)
	{
		namespace fs = std::filesystem;
		try
		{
			if (redirectPath.starts_with(L"\\??\\"))
			{
				redirectPath = redirectPath.substr(4);
			}
			std::error_code ec;
			const auto size = fs::file_size(std::wstring{redirectPath}, ec);
			return !ec && size > 0;
		}
		catch (...)
		{
		}
		return false;
	}

	// 重定向拷贝"等待超时"记录（TTL 去重）：
	// 同一重定向文件等待落盘超时后，TTL 内不再重复等待（直接查一次立即返回），
	// 避免 WXWork 高频访问缺失文件（属性查询轮询、重复打开/创建）时每次访问都
	// 阻塞数百毫秒轮询造成 UI 迟缓。超时后由调用方回退原有逻辑（如创建空文件），
	// 与拷贝失败的既有降级行为一致；TTL 过期后允许重新尝试。
	class RedirectTimeoutCache
	{
	public:
		// 返回 true 表示允许等待（TTL 内无超时记录）
		bool should_wait(const std::wstring& path)
		{
			const ULONGLONG now = ::GetTickCount64();
			std::lock_guard lock(m_mutex);
			purge_locked(now);
			return m_timeouts.find(path) == m_timeouts.end();
		}

		// 继承请求是否已发出（TTL 内不重复发 RPC）：切换主体/扫码登录瞬间同一文件
		// 会被多次访问，重复 RPC 只会让 ncalrpc 端点与宿主任务队列洪峰化
		bool should_request(const std::wstring& path)
		{
			const ULONGLONG now = ::GetTickCount64();
			std::lock_guard lock(m_mutex);
			purge_locked(now);
			return m_requests.find(path) == m_requests.end();
		}

		// 标记继承请求已发出（eBox 拷贝在途/排队中）
		void mark_requested(const std::wstring& path)
		{
			const ULONGLONG now = ::GetTickCount64();
			std::lock_guard lock(m_mutex);
			purge_locked(now);
			m_requests[path] = now;
		}

		// 是否"请求在途"（已发继承请求且未超时）：在途文件应给予更充分的等待窗口，
		// 否则大量文件并发继承排队时 1200ms 不够用，过早超时产生空文件残留
		bool is_inflight(const std::wstring& path)
		{
			const ULONGLONG now = ::GetTickCount64();
			std::lock_guard lock(m_mutex);
			purge_locked(now);
			return m_timeouts.find(path) == m_timeouts.end() && m_requests.find(path) != m_requests.end();
		}

		void record_timeout(const std::wstring& path)
		{
			const ULONGLONG now = ::GetTickCount64();
			std::lock_guard lock(m_mutex);
			purge_locked(now);
			if (m_timeouts.size() >= MaxEntries)
			{
				m_timeouts.clear(); // 达到上限：整体清空（后续文件重新尝试）
			}
			m_timeouts[path] = now;
		}

	private:
		static constexpr ULONGLONG TtlMs = 5000;
		static constexpr std::size_t MaxEntries = 512;

		void purge_locked(ULONGLONG now)
		{
			for (auto it = m_timeouts.begin(); it != m_timeouts.end();)
			{
				if (now - it->second > TtlMs)
				{
					it = m_timeouts.erase(it);
				}
				else
				{
					++it;
				}
			}
			for (auto it = m_requests.begin(); it != m_requests.end();)
			{
				if (now - it->second > TtlMs)
				{
					it = m_requests.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		std::mutex m_mutex;
		std::unordered_map<std::wstring, ULONGLONG> m_timeouts;
		std::unordered_map<std::wstring, ULONGLONG> m_requests;
	};

	// 单例指针：InterlockedCompareExchangePointer 一次性发布（无 magic static）。
	// 反射注入子进程下 magic static 初始化不可靠（实测 WXWorkWeb 崩溃），且永不析构
	// 由操作系统回收，避免短命中转进程退出竞态（与 hook_cache 同一约定）。
	RedirectTimeoutCache* g_redirectTimeoutCachePtr = nullptr;

	RedirectTimeoutCache& redirect_timeout_cache()
	{
		if (g_redirectTimeoutCachePtr == nullptr)
		{
			RedirectTimeoutCache* fresh = new RedirectTimeoutCache;
			if (::InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&g_redirectTimeoutCachePtr),
			                                        fresh, nullptr) != nullptr)
			{
				delete fresh; // 其他线程抢先发布，释放本线程临时对象
			}
		}
		return *g_redirectTimeoutCachePtr;
	}

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

	// 判断文件是否属于"运行时可再生的缓存/日志/临时"类，无需从源机继承拷贝。
	// 源机运行中的 WXWork 会独占写这些文件（日志/缓存），继承拷贝必然冲突重试，
	// 拖慢登录与切换；且这类文件在环境内自建即可，缺失不影响数据完整性。
	// 注意：数据库家族（.db/.db-wal/.db-journal/.sqlite/.ldb/LevelDB 数字.log）优先
	// 保护，绝不排除——排除会直接导致 WXWork "本地数据加载异常"。
	bool is_no_inherit_file(std::wstring_view path)
	{
		std::wstring lower{path};
		std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		// Chromium/CEF 设备指纹 Local State（含 machine_id）：多环境共享同一指纹会被风控，
		// 环境内自生成；宿主侧 is_skip_copy_file 同样排除，两端一致避免无谓等待。
		if (lower.ends_with(L"local state"))
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
		// Chromium 临时文件（~RFxxx.tmp 等）；.db-wal.NNNN.tmp 这类数据库写临时保留
		if (lower.ends_with(L".tmp") && lower.find(L".db-") == std::wstring::npos)
		{
			return true;
		}
		return false;
	}

	// 发起文件继承请求 + 有界等待落盘。缓存/日志/临时类文件不请求、不等待，
	// 直接在环境内自建，避免源文件独占导致的拷贝重试与超时等待。
	bool wait_redirect_ready(std::wstring_view redirectPath); // 定义见下方

	// 源机文件是否存在且有数据（>0 字节）。源机缺失/为空的文件（如 SQLite 空闲态的
	// .db-wal、Chromium First Run 标记、LevelDB LOG.old 等）无可继承内容，直接跳过
	// 请求与等待、由应用在环境内自建——避免每个这类文件白白等待 1~2 秒超时
	//（实测占继承等待超时的 72%，是登录/切换卡顿的主因）。
	bool source_file_has_data(std::wstring_view filePath)
	{
		std::wstring path{filePath};
		if (path.starts_with(L"\\??\\"))
		{
			path = path.substr(4);
		}
		// 用 FindFirstFileW 查询源机文件大小：其内部走 NtQueryDirectoryFile（本 DLL 未 hook），
		// 不会像 fs::file_size（内部走 NtQueryAttributesFile）那样再次进入本 DLL 的 hook
		// 重定向逻辑，造成无限递归栈溢出（crashpad_handler 已实测异常码 0xc00000fd）。
		WIN32_FIND_DATAW fd{};
		const HANDLE hFind = ::FindFirstFileW(path.c_str(), &fd);
		if (hFind == INVALID_HANDLE_VALUE)
		{
			return false; // 源机不存在
		}
		::FindClose(hFind);
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			return false; // 目录：无可继承的"文件数据"，由文件访问按需继承
		}
		const ULONGLONG size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
		return size > 0;
	}

	void request_redirect_inherit(std::wstring_view filePath, std::wstring_view redirectPath)
	{
		if (is_no_inherit_file(filePath))
		{
			return;
		}
		// 源机无数据（缺失或 0 字节）→ 无可继承内容，跳过请求与等待，应用自建
		if (!source_file_has_data(filePath))
		{
			return;
		}
		const std::wstring requestPath{filePath};
		if (redirect_timeout_cache().should_request(requestPath))
		{
			redirect_timeout_cache().mark_requested(requestPath);
			rpc::default_call_ignore_error(&rpc::ClientDefault::createRedirectFile, requestPath.c_str(), std::wstring{redirectPath}.c_str());
		}
		wait_redirect_ready(redirectPath);
	}

	// 有界等待重定向目标落盘。返回 true 表示已就绪。
	// 宿主收到 createRedirectFile 请求后由工作池【异步】拷贝（原子 temp+rename 落盘）。
	// ncalrpc 无调用超时（RPC_C_OPT_CALL_TIMEOUT 对 local RPC 无效），不能在调用线程内
	// 同步等待宿主返回；这里以 10ms 粒度有界轮询，超时后记录到 TTL 去重表，
	// TTL 内再访问同一文件不再重复等待，由调用方回退到原有逻辑（如创建空文件）。
	// 既杜绝无限阻塞，又避免高频访问缺失文件时反复等待造成的 UI 迟缓。
	// 等待窗口分级：已发继承请求（在途，eBox 正在排队/拷贝）→ 总 3000ms，充分等待
	// 保证数据就绪（切换主体/扫码登录瞬间大量文件并发继承，1200ms 常不够用，过早
	// 超时产生空文件 → "本地数据加载异常"）；未请求过 → 常规 1200ms（调用方会先发
	// 请求再进入本函数，正常场景多数 200ms 内落盘）。
	bool wait_redirect_ready(std::wstring_view redirectPath)
	{
		const std::wstring path{redirectPath};
		if (redirect_target_has_data(path))
		{
			return true;
		}
		RedirectTimeoutCache& cache = redirect_timeout_cache();
		if (!cache.should_wait(path))
		{
			return false; // 最近已超时过，不再重复等待
		}
		const int maxAttempts = cache.is_inflight(path) ? 300 : 120; // 300*10ms=3000ms / 120*10ms=1200ms
		for (int i = 0; i < maxAttempts; ++i)
		{
			if (redirect_target_has_data(path))
			{
				return true;
			}
			Sleep(10);
		}
		cache.record_timeout(path);
		return false;
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtCreateFile(OUT PHANDLE FileHandle, IN ACCESS_MASK DesiredAccess, IN POBJECT_ATTRIBUTES ObjectAttributes,
	                            OUT PIO_STATUS_BLOCK IoStatusBlock, IN PLARGE_INTEGER AllocationSize OPTIONAL, IN ULONG FileAttributes,
	                            IN ULONG ShareAccess, IN ULONG CreateDisposition, IN ULONG CreateOptions,
	                            IN PVOID EaBuffer OPTIONAL, IN ULONG EaLength)
	{
		const std::wstring_view filePath = viewFileObjectName(ObjectAttributes);
		//管道;
		if (filePath.starts_with(L"\\??\\pipe\\"))
		{
			/* \??\pipe\ */
			std::wstring strNewName = std::format(L"{}{}", filePath, global::Data::get().envFlagName());
			const PUNICODE_STRING pOldName = ObjectAttributes->ObjectName;
			UNICODE_STRING newObjName;
			newObjName.Buffer = strNewName.data();
			newObjName.Length = newObjName.MaximumLength = static_cast<USHORT>(strNewName.length() * sizeof(wchar_t));
			ObjectAttributes->ObjectName = &newObjName;
			NTSTATUS ret = trampoline(FileHandle, DesiredAccess, ObjectAttributes,
			                          IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
			                          CreateDisposition, CreateOptions, EaBuffer, EaLength);
			ObjectAttributes->ObjectName = pOldName;
			if (!NT_SUCCESS(ret))
			{
				ret = trampoline(FileHandle, DesiredAccess, ObjectAttributes,
				                 IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
				                 CreateDisposition, CreateOptions, EaBuffer, EaLength);
			}
			return ret;
		}
		auto processRedirect = [&]()-> std::optional<NTSTATUS>
		{
			std::optional<NTSTATUS> defaultRet = std::nullopt;
			const bool bIsDir = CreateOptions & FILE_DIRECTORY_FILE;
			if (!global::Data::get().isInKnownFolderPath(filePath))
			{
				return defaultRet;
			}
			std::optional<std::wstring> redirectPath = global::Data::get().getRedirectPath(filePath);
			if (!redirectPath)
			{
				return defaultRet;
			}
			// 目录不预创建：环境内目录已存在时优先枚举环境内（保证目录枚举与文件打开一致），
			// 环境内不存在时由下方回退到真实路径枚举（文件访问仍走按需拷贝补齐数据）。
			// 若这里预创建空目录，FILE_OPEN 会命中空目录导致应用误判"无数据"。
			if (!bIsDir)
			{
				if (!global::ensure_dir_exists(redirectPath.value(), false))
				{
					return defaultRet;
				}
			}
			// FILE_CREATE（总是新建）但环境内目标缺失或为空、源机存在同名数据文件时，
			// 先继承源机数据再以 FILE_OPEN 打开。避免 Chromium/CEF 等应用在首次进入
			// 新主体企业目录时"创建"出空文件（或复用上次中断残留的空文件），
			// 造成数据缺失与"本地数据加载异常"。
			if (CreateDisposition == FILE_CREATE && !bIsDir && !redirect_target_has_data(redirectPath.value()))
			{
				// 试探源路径（FILE_OPEN 不创建）
				HANDLE tempSrcHandle{nullptr};
				IO_STATUS_BLOCK srcStatusBlock{};
				const NTSTATUS srcRet = trampoline(&tempSrcHandle, DesiredAccess, ObjectAttributes,
				                                   &srcStatusBlock, AllocationSize, FileAttributes, ShareAccess,
				                                   FILE_OPEN, CreateOptions, EaBuffer, EaLength);
				if (NT_SUCCESS(srcRet))
				{
					NtClose(tempSrcHandle);
					request_redirect_inherit(filePath, redirectPath.value());
					const PUNICODE_STRING pOldName = ObjectAttributes->ObjectName;
					UNICODE_STRING newObjName;
					newObjName.Buffer = redirectPath.value().data();
					newObjName.Length = newObjName.MaximumLength = static_cast<USHORT>(redirectPath.value().length() * sizeof(wchar_t));
					ObjectAttributes->ObjectName = &newObjName;
					NTSTATUS ret2 = trampoline(FileHandle, DesiredAccess, ObjectAttributes,
					                           IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
					                           FILE_OPEN, CreateOptions, EaBuffer, EaLength);
					ObjectAttributes->ObjectName = pOldName;
					if (NT_SUCCESS(ret2))
					{
						return ret2;
					}
					// 拷贝/打开失败：退回下方 FILE_CREATE 正常流程（创建空文件）
				}
			}
			const PUNICODE_STRING pOldName = ObjectAttributes->ObjectName;
			UNICODE_STRING newObjName;
			newObjName.Buffer = redirectPath.value().data();
			newObjName.Length = newObjName.MaximumLength = static_cast<USHORT>(redirectPath.value().length() * sizeof(wchar_t));
			HANDLE tempDstHandle{};
			ObjectAttributes->ObjectName = &newObjName;
			NTSTATUS ret = trampoline(&tempDstHandle, DesiredAccess, ObjectAttributes,
			                          IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
			                          CreateDisposition, CreateOptions, EaBuffer, EaLength);
			ObjectAttributes->ObjectName = pOldName;
			if (NT_SUCCESS(ret) || CreateDisposition == FILE_CREATE)
			{
				// 继承超时残留补漏：打开成功但目标为 0 字节（非新建语义、非目录）且源机有同名
				// 数据时，主动触发一次继承拷贝并重开——否则应用直接命中空文件初始化
				// （Local Storage/LevelDB）→ "本地数据加载异常"。FILE_CREATE 已在上方继承
				// 分支处理过（失败才回退到这里创建空文件），不再重复触发避免死循环。
				if (CreateDisposition != FILE_CREATE && NT_SUCCESS(ret) && !bIsDir)
				{
					LARGE_INTEGER dstSize{};
					const bool bIsEmpty = GetFileSizeEx(tempDstHandle, &dstSize) && dstSize.QuadPart == 0;
					if (bIsEmpty)
					{
						NtClose(tempDstHandle);
						tempDstHandle = nullptr;
						HANDLE tempSrcHandle{nullptr};
						IO_STATUS_BLOCK srcStatusBlock{};
						const NTSTATUS srcRet = trampoline(&tempSrcHandle, DesiredAccess, ObjectAttributes,
						                                   &srcStatusBlock, AllocationSize, FileAttributes, ShareAccess,
						                                   FILE_OPEN, CreateOptions, EaBuffer, EaLength);
						if (NT_SUCCESS(srcRet))
						{
							NtClose(tempSrcHandle);
							request_redirect_inherit(filePath, redirectPath.value());
							ObjectAttributes->ObjectName = &newObjName;
							const NTSTATUS ret2 = trampoline(FileHandle, DesiredAccess, ObjectAttributes,
							                                 IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
							                                 CreateDisposition, CreateOptions, EaBuffer, EaLength);
							ObjectAttributes->ObjectName = pOldName;
							return ret2;
						}
						// 源机无同名数据：保持原行为，重新打开空文件目标返回
						ObjectAttributes->ObjectName = &newObjName;
						const NTSTATUS ret3 = trampoline(FileHandle, DesiredAccess, ObjectAttributes,
						                                 IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
						                                 CreateDisposition, CreateOptions, EaBuffer, EaLength);
						ObjectAttributes->ObjectName = pOldName;
						return ret3;
					}
				}
				*FileHandle = tempDstHandle;
				return ret;
			}
			// 目录：环境内不存在（首次枚举/打开）→ 回退真实路径。
			// 枚举真实路径可看到源机完整目录列表；应用随后访问其中的文件时，
			// 由下方按需拷贝链路把环境内数据补齐，保证两者最终一致。
			if (bIsDir)
			{
				defaultRet = trampoline(FileHandle, DesiredAccess, ObjectAttributes,
				                        IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
				                        CreateDisposition, CreateOptions, EaBuffer, EaLength);
				return defaultRet;
			}
			// 不是找不到文件的错误也直接返回
			if (ret != STATUS_OBJECT_NAME_NOT_FOUND)
			{
				return ret;
			}
			// 找不到文件， 试试源路径，但是用FILE_OPEN（不允许创建文件）是否可以成功
			HANDLE tempSrcHandle{nullptr};
			IO_STATUS_BLOCK srcStatusBlock{};
			const NTSTATUS srcRet = trampoline(&tempSrcHandle, DesiredAccess, ObjectAttributes,
			                                   &srcStatusBlock, AllocationSize, FileAttributes, ShareAccess,
			                                   FILE_OPEN, CreateOptions, EaBuffer, EaLength);
			// 用源路径尝试都失败了，直接返回
			if (!NT_SUCCESS(srcRet))
			{
				return ret;
			}
			NtClose(tempSrcHandle);

			// 到这里，重定向的文件不存在，但原始文件成功，请求eBox去copy源文件过来
			// 为什么不直接在这里创建文件并copy?
			// 因为要考虑多进程架构的软件可能同时都要访问同一个文件，在这里做并发限制比较困难，而且NtCreateFile还被hook了，用不了高阶接口。干脆用eBox做
			request_redirect_inherit(filePath, redirectPath.value());

			// 最终再次尝试重定向位置
			ObjectAttributes->ObjectName = &newObjName;
			ret = trampoline(FileHandle, DesiredAccess, ObjectAttributes,
			                 IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
			                 CreateDisposition, CreateOptions, EaBuffer, EaLength);
			ObjectAttributes->ObjectName = pOldName;
			return ret;
		};

		// 符合条件的特定路径重定向
		if (const std::optional<NTSTATUS> result = processRedirect())
		{
			return result.value();
		}
		return trampoline(FileHandle, DesiredAccess, ObjectAttributes,
		                  IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
		                  CreateDisposition, CreateOptions, EaBuffer, EaLength);
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtOpenFile(OUT PHANDLE FileHandle, IN ACCESS_MASK DesiredAccess,
	                          IN POBJECT_ATTRIBUTES ObjectAttributes, OUT PIO_STATUS_BLOCK IoStatusBlock,
	                          IN ULONG ShareAccess, IN ULONG OpenOptions)
	{
		const std::wstring_view filePath = viewFileObjectName(ObjectAttributes);
		auto processRedirect = [&]()-> std::optional<NTSTATUS>
		{
			std::optional<NTSTATUS> defaultRet = std::nullopt;
			const bool bIsDir = OpenOptions & FILE_DIRECTORY_FILE;
			if (!global::Data::get().isInKnownFolderPath(filePath))
			{
				return defaultRet;
			}
			std::optional<std::wstring> redirectPath = global::Data::get().getRedirectPath(filePath);
			if (!redirectPath)
			{
				return defaultRet;
			}
			if (!bIsDir)
			{
				if (!global::ensure_dir_exists(redirectPath.value(), false))
				{
					return defaultRet;
				}
			}
			const PUNICODE_STRING pOldName = ObjectAttributes->ObjectName;
			UNICODE_STRING newObjName;
			newObjName.Buffer = redirectPath.value().data();
			newObjName.Length = newObjName.MaximumLength = static_cast<USHORT>(redirectPath.value().length() * sizeof(wchar_t));
			HANDLE tempDstHandle{};
			ObjectAttributes->ObjectName = &newObjName;
			NTSTATUS ret = trampoline(&tempDstHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
			ObjectAttributes->ObjectName = pOldName;
			if (NT_SUCCESS(ret))
			{
				// 继承超时残留补漏（同 NtCreateFile）：打开成功但目标为 0 字节且源机有同名
				// 数据时，主动触发一次继承拷贝并重开，避免应用命中空文件 → "本地数据加载异常"。
				if (!bIsDir)
				{
					LARGE_INTEGER dstSize{};
					const bool bIsEmpty = GetFileSizeEx(tempDstHandle, &dstSize) && dstSize.QuadPart == 0;
					if (bIsEmpty)
					{
						NtClose(tempDstHandle);
						tempDstHandle = nullptr;
						HANDLE tempSrcHandle{nullptr};
						const NTSTATUS srcRet = trampoline(&tempSrcHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
						if (NT_SUCCESS(srcRet))
						{
							NtClose(tempSrcHandle);
							request_redirect_inherit(filePath, redirectPath.value());
							ObjectAttributes->ObjectName = &newObjName;
							const NTSTATUS ret2 = trampoline(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
							ObjectAttributes->ObjectName = pOldName;
							return ret2;
						}
						// 源机无同名数据：重新打开空文件目标返回
						ObjectAttributes->ObjectName = &newObjName;
						const NTSTATUS ret3 = trampoline(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
						ObjectAttributes->ObjectName = pOldName;
						return ret3;
					}
				}
				*FileHandle = tempDstHandle;
				return ret;
			}
			// 目录：环境内不存在（首次枚举/打开）→ 回退真实路径（文件访问由按需拷贝补齐）
			if (bIsDir)
			{
				defaultRet = trampoline(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
				return defaultRet;
			}
			// 不是找不到文件的错误也直接返回
			if (ret != STATUS_OBJECT_NAME_NOT_FOUND)
			{
				return ret;
			}
			// 源路径是否可以成功
			HANDLE tempSrcHandle{nullptr};
			ret = trampoline(&tempSrcHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
			// 用源路径尝试都失败了，直接返回
			if (!NT_SUCCESS(ret))
			{
				return ret;
			}
			NtClose(tempSrcHandle);

			request_redirect_inherit(filePath, redirectPath.value());

			// 最终再次尝试重定向位置
			ObjectAttributes->ObjectName = &newObjName;
			ret = trampoline(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
			ObjectAttributes->ObjectName = pOldName;
			return ret;
		};

		// 符合条件的特定路径重定向
		if (const std::optional<NTSTATUS> result = processRedirect())
		{
			return result.value();
		}
		return trampoline(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
	}

	// NtQueryAttributesFile 在win10后貌似不走 ntdll的NtCreateFile， 也hook一下
	template <auto trampoline>
	std::optional<NTSTATUS> MyQueryAttributesFile(_In_ POBJECT_ATTRIBUTES ObjectAttributes, _Out_ void* FileInformation)
	{
		const std::wstring_view filePath = viewFileObjectName(ObjectAttributes);
		if (!global::Data::get().isInKnownFolderPath(filePath))
		{
			return std::nullopt;
		}
		std::optional<std::wstring> redirectPath = global::Data::get().getRedirectPath(filePath);
		if (!redirectPath)
		{
			return std::nullopt;
		}
		if (!global::ensure_dir_exists(redirectPath.value(), false))
		{
			return std::nullopt;
		}
		const PUNICODE_STRING pOldName = ObjectAttributes->ObjectName;
		UNICODE_STRING newObjName;
		newObjName.Buffer = redirectPath.value().data();
		newObjName.Length = newObjName.MaximumLength = static_cast<USHORT>(redirectPath.value().length() * sizeof(wchar_t));
		ObjectAttributes->ObjectName = &newObjName;
		NTSTATUS ret = trampoline(ObjectAttributes, FileInformation);
		ObjectAttributes->ObjectName = pOldName;
		if (NT_SUCCESS(ret))
		{
			return ret;
		}

		// 不是找不到文件的错误也直接返回
		if (ret != STATUS_OBJECT_NAME_NOT_FOUND)
		{
			return ret;
		}
		// 源路径是否可以成功
		ret = trampoline(ObjectAttributes, FileInformation);
		// 用源路径尝试都失败了，直接返回
		if (!NT_SUCCESS(ret))
		{
			return ret;
		}

		request_redirect_inherit(filePath, redirectPath.value());

		// 最终再次尝试重定向位置
		ObjectAttributes->ObjectName = &newObjName;
		ret = trampoline(ObjectAttributes, FileInformation);
		ObjectAttributes->ObjectName = pOldName;
		return ret;
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtQueryAttributesFile(_In_ POBJECT_ATTRIBUTES ObjectAttributes, _Out_ void* FileInformation)
	{
		if (const std::optional<NTSTATUS> result = MyQueryAttributesFile<trampoline>(ObjectAttributes, FileInformation))
		{
			return result.value();
		}
		return trampoline(ObjectAttributes, FileInformation);
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtQueryFullAttributesFile(_In_ POBJECT_ATTRIBUTES ObjectAttributes, _Out_ void* FileInformation)
	{
		if (const std::optional<NTSTATUS> result = MyQueryAttributesFile<trampoline>(ObjectAttributes, FileInformation))
		{
			return result.value();
		}
		return trampoline(ObjectAttributes, FileInformation);
	}

	enum FILE_INFORMATION_CLASS
	{
		FileRenameInformation = 10, // s: FILE_RENAME_INFORMATION (requires DELETE) // 10
		// FileDispositionInformation = 13, // s: FILE_DISPOSITION_INFORMATION (requires DELETE)
		// FileShortNameInformation = 40, // s: FILE_NAME_INFORMATION (requires DELETE) // 40
		// FileDispositionInformationEx = 64, // s: FILE_DISPOSITION_INFO_EX (requires DELETE) // since REDSTONE
		FileRenameInformationEx = 65, // s: FILE_RENAME_INFORMATION_EX (requires DELETE) // since REDSTONE2 (Win10 1607+)
	};

	struct FILE_RENAME_INFORMATION
	{
		BOOLEAN ReplaceIfExists;
		HANDLE RootDirectory;
		ULONG FileNameLength;
		WCHAR FileName[1];
	};

	struct FILE_RENAME_INFORMATION_EX
	{
		BOOLEAN ReplaceIfExists;
		HANDLE RootDirectory;
		ULONG FileNameLength;
		ULONG Flags;
		WCHAR FileName[1];
	};

	// 重定向"重命名目标路径"。temp+rename 原子写入在 Win10/11 上 NtCreateFile 拦不到
	// rename 这一步，必须在这里把目标路径也改到环境内，否则文件会被移出环境。
	template <auto trampoline>
	NTSTATUS NTAPI NtSetInformationFile(_In_ HANDLE FileHandle, _Out_ PIO_STATUS_BLOCK IoStatusBlock,
	                                    _In_reads_bytes_(Length) PVOID FileInformation, _In_ ULONG Length, _In_ FILE_INFORMATION_CLASS FileInformationClass)
	{
		if ((FileInformationClass == FileRenameInformation || FileInformationClass == FileRenameInformationEx) && FileInformation && Length)
		{
			auto processRedirect = [&]()-> std::optional<NTSTATUS>
			{
				FILE_RENAME_INFORMATION& fileInfo = *static_cast<FILE_RENAME_INFORMATION*>(FileInformation);
				if (!fileInfo.FileNameLength)
				{
					return std::nullopt;
				}
				// Ex 结构体多一个 Flags 字段，FileName 偏移不同，必须按结构体取路径
				const WCHAR* pFileName = FileInformationClass == FileRenameInformationEx
					? reinterpret_cast<const FILE_RENAME_INFORMATION_EX*>(FileInformation)->FileName
					: fileInfo.FileName;
				std::wstring filePath{pFileName, fileInfo.FileNameLength / sizeof(wchar_t)};
				// Chromium/CEF 原子写经常用 RootDirectory(目录句柄)+相对文件名的方式 rename
				// （如把 Local State~RF*.TMP 换成 Local State）。此前直接跳过导致 rename 目标
				// 落在原生目录、文件被移出环境。这里解析目录句柄得到完整路径再参与重定向判断。
				if (fileInfo.RootDirectory)
				{
					std::wstring rootDirPath = get_file_name_by_handle(fileInfo.RootDirectory);
					if (rootDirPath.empty())
					{
						return std::nullopt;
					}
					if (filePath.starts_with(L'\\'))
					{
						filePath = rootDirPath + filePath;
					}
					else
					{
						filePath = rootDirPath + L"\\" + filePath;
					}
				}
				const std::wstring_view filePathView{filePath};
				if (!global::Data::get().isInKnownFolderPath(filePathView))
				{
					return std::nullopt;
				}
				std::optional<std::wstring> redirectPath = global::Data::get().getRedirectPath(filePath);
				if (!redirectPath)
				{
					return std::nullopt;
				}
				if (!global::ensure_dir_exists(redirectPath.value(), false))
				{
					return std::nullopt;
				}
				const ULONG newFileNameLength = static_cast<ULONG>(redirectPath.value().length() * sizeof(wchar_t));
				const size_t fileNameOffset = FileInformationClass == FileRenameInformationEx
					? offsetof(FILE_RENAME_INFORMATION_EX, FileName)
					: offsetof(FILE_RENAME_INFORMATION, FileName);
				std::vector<std::byte> newFileInfoBuffer(fileNameOffset + newFileNameLength);
				if (FileInformationClass == FileRenameInformationEx)
				{
					const auto& src = *reinterpret_cast<const FILE_RENAME_INFORMATION_EX*>(FileInformation);
					auto& dst = *reinterpret_cast<FILE_RENAME_INFORMATION_EX*>(newFileInfoBuffer.data());
					dst.ReplaceIfExists = src.ReplaceIfExists;
					dst.RootDirectory = nullptr; // 目标已改为绝对路径，必须清空 RootDirectory
					dst.FileNameLength = newFileNameLength;
					dst.Flags = src.Flags;
				}
				else
				{
					const auto& src = *reinterpret_cast<const FILE_RENAME_INFORMATION*>(FileInformation);
					auto& dst = *reinterpret_cast<FILE_RENAME_INFORMATION*>(newFileInfoBuffer.data());
					dst.ReplaceIfExists = src.ReplaceIfExists;
					dst.RootDirectory = nullptr; // 目标已改为绝对路径，必须清空 RootDirectory
					dst.FileNameLength = newFileNameLength;
				}
				memcpy(newFileInfoBuffer.data() + fileNameOffset, redirectPath.value().data(), newFileNameLength);
				return trampoline(FileHandle, IoStatusBlock, newFileInfoBuffer.data(), static_cast<ULONG>(newFileInfoBuffer.size()), FileInformationClass);
			};
			if (const std::optional<NTSTATUS> result = processRedirect())
			{
				return result.value();
			}
			return trampoline(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
		}
		return trampoline(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
	}

	// 貌似没什么调用会走 NtDeleteFile ? 
	// template <auto trampoline>
	// NTSTATUS NTAPI NtDeleteFile(_In_ POBJECT_ATTRIBUTES ObjectAttributes)
	// {
	// 	auto processRedirect = [&]()-> std::optional<NTSTATUS>
	// 	{
	// 		const std::wstring_view filePath = viewFileObjectName(ObjectAttributes);
	// 		if (!global::Data::get().isInKnownFolderPath(filePath))
	// 		{
	// 			return std::nullopt;
	// 		}
	// 		std::optional<std::wstring> redirectPath = global::Data::get().getRedirectPath(filePath);
	// 		if (!redirectPath)
	// 		{
	// 			return std::nullopt;
	// 		}
	// 		const PUNICODE_STRING pOldName = ObjectAttributes->ObjectName;
	// 		UNICODE_STRING newObjName;
	// 		newObjName.Buffer = redirectPath.value().data();
	// 		newObjName.Length = newObjName.MaximumLength = static_cast<USHORT>(redirectPath.value().length() * sizeof(wchar_t));
	// 		ObjectAttributes->ObjectName = &newObjName;
	// 		NTSTATUS ret = trampoline(ObjectAttributes);
	// 		ObjectAttributes->ObjectName = pOldName;
	// 		return ret;
	// 	};
	// 	if (const std::optional<NTSTATUS> result = processRedirect())
	// 	{
	// 		return result.value();
	// 	}
	// 	return trampoline(ObjectAttributes);
	// }

	// 原子替换（ReplaceFileW → NtReplaceFile）：Chromium/CEF 写 Local State 等文件时
	// 除了 temp+rename 也常用 ReplaceFileW 原子替换。目标路径同样必须重定向进环境，
	// 否则文件会落在原生目录（表现为关闭进程后环境内 Local State 消失、原生出现新文件）。
	// 注意：替换文件(ReplacedObjectAttributes)与目标文件(ObjectAttributes)两个路径都要重定向。
	template <auto trampoline>
	NTSTATUS NTAPI NtReplaceFile(_Out_ PHANDLE FileHandle, _In_ ACCESS_MASK DesiredAccess,
	                             _In_ POBJECT_ATTRIBUTES ObjectAttributes, _In_ POBJECT_ATTRIBUTES ReplacedObjectAttributes,
	                             _In_ ULONG CreateOptions, _In_ ULONG CreateDisposition,
	                             _In_ ULONG OpenReparsePoint, _In_opt_ PVOID SecurityQos)
	{
		// 依次对两个文件路径做重定向，任一成功都值得尝试
		auto tryRedirectOne = [&](POBJECT_ATTRIBUTES oa, std::wstring& newNameOut, PUNICODE_STRING& oldNameOut) -> bool
		{
			if (!oa)
			{
				return false;
			}
			const std::wstring_view filePath = viewFileObjectName(oa);
			if (filePath.empty())
			{
				return false;
			}
			if (!global::Data::get().isInKnownFolderPath(filePath))
			{
				return false;
			}
			std::optional<std::wstring> redirectPath = global::Data::get().getRedirectPath(filePath);
			if (!redirectPath)
			{
				return false;
			}
			// ReplaceFileW 的目标通常是已存在的文件，重定向位置先确保目录存在
			if (!global::ensure_dir_exists(redirectPath.value(), false))
			{
				return false;
			}
			newNameOut = std::move(redirectPath.value());
			oldNameOut = oa->ObjectName;
			return true;
		};

		std::wstring objNewName, replacedNewName;
		PUNICODE_STRING objOldName = nullptr, replacedOldName = nullptr;
		UNICODE_STRING newObjName, newReplacedName;
		bool bObjRedirected = tryRedirectOne(ObjectAttributes, objNewName, objOldName);
		bool bReplacedRedirected = tryRedirectOne(ReplacedObjectAttributes, replacedNewName, replacedOldName);
		if (!bObjRedirected && !bReplacedRedirected)
		{
			return trampoline(FileHandle, DesiredAccess, ObjectAttributes, ReplacedObjectAttributes,
			                  CreateOptions, CreateDisposition, OpenReparsePoint, SecurityQos);
		}

		auto applyRedirect = [](POBJECT_ATTRIBUTES oa, const std::wstring& newName, PUNICODE_STRING oldName, UNICODE_STRING& out) -> POBJECT_ATTRIBUTES
		{
			if (!oldName || !oa)
			{
				return oa;
			}
			out.Buffer = const_cast<wchar_t*>(newName.data());
			out.Length = out.MaximumLength = static_cast<USHORT>(newName.length() * sizeof(wchar_t));
			oa->ObjectName = &out;
			return oa;
		};
		applyRedirect(ObjectAttributes, objNewName, objOldName, newObjName);
		applyRedirect(ReplacedObjectAttributes, replacedNewName, replacedOldName, newReplacedName);

		const NTSTATUS ret = trampoline(FileHandle, DesiredAccess, ObjectAttributes, ReplacedObjectAttributes,
		                                CreateOptions, CreateDisposition, OpenReparsePoint, SecurityQos);
		// 恢复原始路径（detour 调用方可能复用该 OBJECT_ATTRIBUTES）
		if (objOldName)
		{
			ObjectAttributes->ObjectName = objOldName;
		}
		if (replacedOldName)
		{
			ReplacedObjectAttributes->ObjectName = replacedOldName;
		}
		return ret;
	}

	// 供 hook_cache 使用的全量拉取回调: 返回"其他环境全部 pid"集合
	bool fetch_other_processes(std::unordered_set<ULONG_PTR>& out)
	{
		try
		{
			const rpc::ClientDefault c;
			std::uint64_t pids[rpc::MAX_PID_COUNT]{};
			std::uint32_t count = rpc::MAX_PID_COUNT;
			c.getAllProcessIdExclude(global::Data::get().envFlag(), pids, &count);
			out.reserve(count);
			for (std::uint32_t i = 0; i < count; ++i)
			{
				out.insert(static_cast<ULONG_PTR>(pids[i]));
			}
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtQuerySystemInformation(IN SYSTEM_INFORMATION_CLASS SystemInformationClass, IN OUT PVOID SystemInformation, IN ULONG SystemInformationLength, OUT PULONG ReturnLength OPTIONAL)
	{
		const NTSTATUS ret = trampoline(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
		if (!NT_SUCCESS(ret) || !SystemInformation)
		{
			return ret;
		}

		if (SystemInformationClass == SystemProcessInformation)
		{
			// 用 TTL 缓存过滤其他环境进程(见 hook_cache.h), 避免每次进程枚举都同步 RPC
			PSYSTEM_PROCESS_INFORMATION pIndex = static_cast<PSYSTEM_PROCESS_INFORMATION>(SystemInformation);
			PSYSTEM_PROCESS_INFORMATION pShow = pIndex;

			do
			{
				if (pIndex->UniqueProcessId && hook_cache::process_other(reinterpret_cast<ULONG_PTR>(pIndex->UniqueProcessId)))
				{
					if (pIndex->NextEntryOffset)
					{
						pShow->NextEntryOffset += pIndex->NextEntryOffset;
					}
					else
					{
						pShow->NextEntryOffset = 0;
					}
				}
				else
				{
					pShow = pIndex;
				}

				if (pIndex->NextEntryOffset)
				{
					pIndex = reinterpret_cast<PSYSTEM_PROCESS_INFORMATION>(reinterpret_cast<char*>(pIndex) + pIndex->NextEntryOffset);
				}
				else
				{
					pIndex = nullptr;
				}
			}
			while (pIndex);
		}
		return ret;
	}

	//////////////////////////////////////////////////////////////////////////
	//Reg key
	typedef enum _KEY_INFORMATION_CLASS
	{
		KeyBasicInformation,
		KeyNodeInformation,
		KeyFullInformation,
		KeyNameInformation,
		KeyCachedInformation,
		KeyFlagsInformation,
		KeyVirtualizationInformation,
		KeyHandleTagsInformation,
		KeyTrustInformation,
		KeyLayerInformation,
		MaxKeyInfoClass
	} KEY_INFORMATION_CLASS;

	typedef struct _KEY_NAME_INFORMATION
	{
		ULONG NameLength;
		WCHAR Name[1];
	} KEY_NAME_INFORMATION, *PKEY_NAME_INFORMATION;

	typedef enum _KEY_VALUE_INFORMATION_CLASS
	{
		KeyValueBasicInformation,
		KeyValueFullInformation,
		KeyValuePartialInformation,
		KeyValueFullInformationAlign64,
		KeyValuePartialInformationAlign64,
		KeyValueLayerInformation,
		MaxKeyValueInfoClass
	} KEY_VALUE_INFORMATION_CLASS;

	inline win32_api::ApiProxy<utils::make_literal_name<L"ntdll">(), utils::make_literal_name<"NtQueryKey">(), NTSTATUS (NTAPI)(_In_ HANDLE KeyHandle,
	                                                                                                                            _In_ KEY_INFORMATION_CLASS KeyInformationClass,
	                                                                                                                            _Out_writes_bytes_opt_(Length) PVOID KeyInformation,
	                                                                                                                            _In_ ULONG Length,
	                                                                                                                            _Out_ PULONG ResultLength)> NtQueryKey;

	std::vector<std::byte> GetKeyNameInformation(HANDLE KeyHandle)
	{
		std::vector<std::byte> buffer;
		if (!KeyHandle)
		{
			return buffer;
		}
		// 单次系统调用：先用 512 字节栈缓冲（注册表键名一般远小于此），不足才动态分配
		std::array<std::byte, 512> stackBuf{};
		ULONG size = 0;
		const NTSTATUS status = NtQueryKey(KeyHandle, KeyNameInformation, stackBuf.data(), static_cast<ULONG>(stackBuf.size()), &size);
		if (status == STATUS_BUFFER_TOO_SMALL && size > stackBuf.size())
		{
			buffer.resize(size);
			if (!NT_SUCCESS(NtQueryKey(KeyHandle, KeyNameInformation, buffer.data(), size, &size)))
			{
				buffer.clear();
				return buffer;
			}
			return buffer;
		}
		if (!NT_SUCCESS(status))
		{
			return buffer;
		}
		buffer.resize(size);
		std::memcpy(buffer.data(), stackBuf.data(), size);
		return buffer;
	}

	std::wstring GetKeyName(HANDLE KeyHandle)
	{
		std::wstring fullName;
		std::vector<std::byte> buffer = GetKeyNameInformation(KeyHandle);
		if (buffer.empty())
		{
			return fullName;
		}
		const KEY_NAME_INFORMATION* ni = reinterpret_cast<KEY_NAME_INFORMATION*>(buffer.data());
		fullName = std::wstring_view{ni->Name, ni->NameLength / sizeof(wchar_t)};
		return fullName;
	}

	std::expected<HKEY, LSTATUS> RegCreateKeyExW(_In_ HKEY hKey, _In_opt_ LPCWSTR lpSubKey)
	{
		HKEY hResKey{nullptr};
		LSTATUS status = ::RegCreateKeyExW(hKey, lpSubKey,
		                                   0, nullptr, 0,
		                                   KEY_ALL_ACCESS, nullptr, &hResKey, nullptr);
		if (status == ERROR_SUCCESS)
		{
			return std::expected<HKEY, LSTATUS>{hResKey};
		}
		return std::unexpected{status};
	}

	std::expected<HKEY, LSTATUS> RegOpenKeyExW(_In_ HKEY hKey, _In_opt_ LPCWSTR lpSubKey)
	{
		HKEY hResKey{nullptr};
		LSTATUS status = ::RegOpenKeyExW(hKey, lpSubKey, 0, KEY_ALL_ACCESS, &hResKey);
		if (status == ERROR_SUCCESS)
		{
			return std::expected<HKEY, LSTATUS>{hResKey};
		}
		return std::unexpected{status};
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtSetValueKey(_In_ HANDLE KeyHandle, _In_ PUNICODE_STRING ValueName, _In_opt_ ULONG TitleIndex,
	                             _In_ ULONG Type, _In_reads_bytes_opt_(DataSize) PVOID Data, _In_ ULONG DataSize)
	{
		// TODO:
		// 暂时先允许写入真实注册表
		// 测试发现一些写入如果不写入真实注册表程序会变的很卡，不知道为什么。也许一些系统操作必须写入，其对应的查询又貌似不走NtQueryValueKey
		// 暂时不研究过滤了，写入也不影响啥，又不是安全软件。
		const NTSTATUS retStatus = trampoline(KeyHandle, ValueName, TitleIndex, Type, Data, DataSize);
		if (!NT_SUCCESS(retStatus))
		{
			return retStatus;
		}
		// 额外往app env key写入，以供后续优先查询，这样每个环境中都有一份自己的虚拟注册表了
		const std::wstring keyName = GetKeyName(KeyHandle);
		if (keyName.size() && !global::is_app_key_name(keyName))
		{
			// 该键已被写入 appKey，否定缓存失效
			global::Data::get().clear_key_not_in_app_cache(keyName);
			const std::expected<HKEY, LSTATUS> result =
				RegCreateKeyExW(global::Data::get().appKey(), global::remove_leading_backslashes_sv(keyName).data())
				.and_then([&](HKEY hKey)-> std::expected<HKEY, LSTATUS>
				{
					const NTSTATUS status = trampoline(hKey, ValueName, TitleIndex, Type, Data, DataSize);
					RegCloseKey(hKey);
					if (!NT_SUCCESS(status))
					{
						return std::unexpected{status};
					}
					return std::expected<HKEY, LSTATUS>{HKEY{}};
				});
			// if (result)
			// {
			// 	return STATUS_SUCCESS;
			// }
		}
		return retStatus;
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtQueryValueKey(_In_ HANDLE KeyHandle,
	                               _In_ PUNICODE_STRING ValueName,
	                               _In_ KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
	                               _Out_writes_bytes_opt_(Length) PVOID KeyValueInformation,
	                               _In_ ULONG Length,
	                               _Out_ PULONG ResultLength)
	{
		// 优先从app env key查询，查询失败才从真实注册表查
		const std::wstring keyName = GetKeyName(KeyHandle);
		if (keyName.size() && !global::is_app_key_name(keyName))
		{
			// 否定缓存命中：该键已确认不在 appKey，直接查真实注册表（省去每次失败的 RegOpenKeyExW 系统调用）
			if (!global::Data::get().is_key_not_in_app_cache(keyName))
			{
				const std::expected<HKEY, LSTATUS> result =
					RegOpenKeyExW(global::Data::get().appKey(), global::remove_leading_backslashes_sv(keyName).data())
					.and_then([&](HKEY hKey)-> std::expected<HKEY, LSTATUS>
					{
						const NTSTATUS status = trampoline(hKey, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
						RegCloseKey(hKey);
						if (!NT_SUCCESS(status))
						{
							return std::unexpected{status};
						}
						return std::expected<HKEY, LSTATUS>{HKEY{}};
					});
				if (result)
				{
					return STATUS_SUCCESS;
				}
				// 缓冲区不够不认为是失败，也直接返回。
				if (result.error() == STATUS_BUFFER_TOO_SMALL || result.error() == STATUS_BUFFER_OVERFLOW)
				{
					return result.error();
				}
				// 键不在 appKey（打开失败）：标记否定缓存，后续同一键查询直接走真实注册表
				global::Data::get().mark_key_not_in_app_cache(keyName);
			}
		}
		return trampoline(KeyHandle, ValueName, KeyValueInformationClass, KeyValueInformation, Length, ResultLength);
	}

	template <auto trampoline>
	NTSTATUS NTAPI NtQueryMultipleValueKey(_In_ HANDLE KeyHandle,
	                                       _Inout_updates_(EntryCount) PKEY_VALUE_ENTRY ValueEntries,
	                                       _In_ ULONG EntryCount,
	                                       _Out_writes_bytes_(*BufferLength) PVOID ValueBuffer,
	                                       _Inout_ PULONG BufferLength,
	                                       _Out_opt_ PULONG RequiredBufferLength)
	{
		// 优先从app env key查询，查询失败才从真实注册表查
		const std::wstring keyName = GetKeyName(KeyHandle);
		if (keyName.size() && !global::is_app_key_name(keyName))
		{
			// 否定缓存命中：该键已确认不在 appKey，直接查真实注册表
			if (!global::Data::get().is_key_not_in_app_cache(keyName))
			{
				const std::expected<HKEY, LSTATUS> result =
					RegOpenKeyExW(global::Data::get().appKey(), global::remove_leading_backslashes_sv(keyName).data())
					.and_then([&](HKEY hKey)-> std::expected<HKEY, LSTATUS>
					{
						const NTSTATUS status = trampoline(hKey, ValueEntries, EntryCount, ValueBuffer, BufferLength, RequiredBufferLength);
						RegCloseKey(hKey);
						if (!NT_SUCCESS(status))
						{
							return std::unexpected{status};
						}
						return std::expected<HKEY, LSTATUS>{HKEY{}};
					});
				if (result)
				{
					return STATUS_SUCCESS;
				}
				// 缓冲区不够不认为是失败，也直接返回。
				if (result.error() == STATUS_BUFFER_TOO_SMALL || result.error() == STATUS_BUFFER_OVERFLOW)
				{
					return result.error();
				}
				// 键不在 appKey（打开失败）：标记否定缓存
				global::Data::get().mark_key_not_in_app_cache(keyName);
			}
		}
		return trampoline(KeyHandle, ValueEntries, EntryCount, ValueBuffer, BufferLength, RequiredBufferLength);
	}

	void hook_ntdll()
	{
		// 注册进程隔离查询的全量拉取回调(hook_cache TTL 缓存用)
		hook_cache::set_proc_fetcher(&fetch_other_processes);

		constexpr auto NTDLL_LIB_NAME = utils::make_literal_name<L"ntdll.dll">();
		sys_info::SysDllMapHelper ntdllMapped = sys_info::get_ntdll_mapped();
		void* ntdllMappedAddress = ntdllMapped.memAddress();

#define CREATE_HOOK_BY_NAME(name) \
		create_hook_by_func_type<NTDLL_LIB_NAME, utils::make_literal_name<#name>(), decltype(name<nullptr>)>().setHookFromGetter([&](auto trampolineConst) \
		{ \
			return HookInfo{&name<trampolineConst.value>, ntdllMappedAddress}; \
		})

		CREATE_HOOK_BY_NAME(NtCreateEvent);
		CREATE_HOOK_BY_NAME(NtOpenEvent);
		CREATE_HOOK_BY_NAME(NtCreateMutant);
		CREATE_HOOK_BY_NAME(NtOpenMutant);
		CREATE_HOOK_BY_NAME(NtCreateSection);
		CREATE_HOOK_BY_NAME(NtOpenSection);
		CREATE_HOOK_BY_NAME(NtCreateSemaphore);
		CREATE_HOOK_BY_NAME(NtOpenSemaphore);
		CREATE_HOOK_BY_NAME(NtCreateTimer);
		CREATE_HOOK_BY_NAME(NtOpenTimer);
		CREATE_HOOK_BY_NAME(NtCreateJobObject);
		CREATE_HOOK_BY_NAME(NtOpenJobObject);
		CREATE_HOOK_BY_NAME(NtCreateNamedPipeFile);
		CREATE_HOOK_BY_NAME(NtCreateFile);
		CREATE_HOOK_BY_NAME(NtOpenFile);
		CREATE_HOOK_BY_NAME(NtQueryAttributesFile);
		CREATE_HOOK_BY_NAME(NtQueryFullAttributesFile);
		// 文件重命名(原子写入的 temp+rename)在 Win10/11 上同样需要重定向，
		// 否则 Chromium/CEF 的 Local State 等文件会被 rename 移出环境到原生目录，
		// 导致每次启动环境都像全新设备(需要验证码)。所有 Windows 版本一律 hook。
		CREATE_HOOK_BY_NAME(NtSetInformationFile);
		// 原子替换(ReplaceFileW → NtReplaceFile)：此 hook 与 WXWork 实测不兼容——
		// 恢复后 WXWork 启动即秒退（此前 A/B 验证已确认，本次恢复后再次复现秒退）。
		// 根因：ReplaceFileW 要求 replacement 与 replaced 同卷，且成功后删除 replacement；
		// 沙箱重定向下两者路径被改到环境内，跨卷/路径语义变化导致内核返回错误，
		// WXWork 把替换失败视为数据异常 → 启动秒退。
		// 结论：保持禁用。CEF 写 Local State 的主通道是 temp+rename（NtSetInformationFile
		// 已重定向），不依赖 NtReplaceFile；禁用后环境内文件不会被移出、不会落在原生目录。
		// CREATE_HOOK_BY_NAME(NtReplaceFile);
		// CREATE_HOOK_BY_NAME(NtDeleteFile);
		CREATE_HOOK_BY_NAME(NtQuerySystemInformation);

		// 简单做一个虚拟注册表：将所有的写入操作都copy一份到自己的环境中，查询优先查询虚拟环境中的，找不到的话再从真实注册表查
		CREATE_HOOK_BY_NAME(NtSetValueKey);
		CREATE_HOOK_BY_NAME(NtQueryValueKey);
		CREATE_HOOK_BY_NAME(NtQueryMultipleValueKey);
	}
}
