module;
#include <Ole2.h>
#pragma comment(lib, "ole32.lib")
export module Hook:Clipboard;

import "sys_defs.h";
import std;
import :Core;
import GlobalData;

// 跨环境文件粘贴桥接（方案 A：剪贴板桥接 + 共享交换目录）
//
// 背景：eBox 把“已知文件夹”（我的文档 / AppData / SavedGames / ProgramData 等）按
// envIndex 重定向到影子目录 <rootPath>\Env\<n>\<相对路径>。企业微信（Chromium/CEF）
// 在会话里“选中图片（不打开）”后 Ctrl+C / 右键复制时，剪贴板里放进多种既含文件“逻辑
// 路径”的格式（CF_HDROP / text/uri-list / text/x-moz-url / HTML Format / 私有格式
// “WeWork Message”），字节实体只存在于 A 环境影子目录。到 B 环境粘贴时，B 进程按同一
// 逻辑路径读取 → 被重定向到 B 影子（不存在）→ 回退真实路径也失败 → 图片不出现。而
// “先点开预览再复制”放的是自包含位图（CF_DIB，不含路径），故必成功。
//
// 实测：只改写 CF_HDROP 无效——企业微信粘贴实际消费的是私有格式 “WeWork Message”
// （Boost 序列化档案，路径以单字节长度前缀存储）以及 Chromium 的文本/HTML 格式。
//
// 方案：复制侧把命中重定向影子的文件物理拷贝到所有环境都可见的共享目录
// <rootPath>\ClipboardShare\<envIndex>\<token>\<文件名>（不属于任何已知文件夹，故不被
// 重定向），并改写上述所有携带路径的格式：
//   - CF_HDROP：重建 HGLOBAL；
//   - text/uri-list / text/x-moz-url / HTML Format：按 UTF-8/ACP/UTF-16LE 子串替换；
//   - WeWork Message（二进制）：等长原地替换（长度守恒，不破坏 Boost 档案与长度前缀）。
// 为此共享路径按“与原始路径等长”构造（token 位数补齐），使所有替换均为长度守恒，
// HTML 头部偏移也无需改动。仅处理文件引用，绝不触碰指纹/注册表，隔离根基不受影响。
namespace hook
{
	namespace
	{
		constexpr ULONGLONG kSweepIntervalMs = 5ULL * 60 * 1000; // 清理扫描频率限制
		constexpr auto kSharedFileMaxAge = std::chrono::hours(24); // 共享文件老化时间

		// 生成 32 位十六进制随机串（无花括号/连字符），失败返回空。
		std::wstring guid_hex()
		{
			GUID guid{};
			if (FAILED(::CoCreateGuid(&guid)))
			{
				return {};
			}
			wchar_t buf[40]{};
			if (::StringFromGUID2(guid, buf, 40) <= 0)
			{
				return {};
			}
			std::wstring out;
			out.reserve(32);
			for (const wchar_t* p = buf; *p; ++p)
			{
				const wchar_t c = *p;
				if ((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F'))
				{
					out.push_back(c);
				}
			}
			return out;
		}

		// 生成长度为 len 的十六进制 token（必要时拼接多段随机串），失败返回空。
		std::wstring make_leaf(std::size_t len)
		{
			std::wstring pool;
			while (pool.size() < len)
			{
				std::wstring h = guid_hex();
				if (h.empty())
				{
					return {};
				}
				pool += h;
			}
			return pool.substr(0, len);
		}

		// 共享根前缀 <rootPath>\ClipboardShare\<envIndex>（不含结尾反斜杠）。
		std::wstring shared_root_prefix()
		{
			try
			{
				return (std::filesystem::path{global::Data::get().rootPath()}
					/ std::filesystem::path{L"ClipboardShare"}
					/ std::filesystem::path{std::format(L"{}", global::Data::get().envIndex())}).native();
			}
			catch (...)
			{
				return {};
			}
		}

		// 构造共享目标路径，尽量使 dst 字符数与 original 相同（供二进制等长替换/HTML 偏移守恒）。
		// 目标形如 <prefix>\<leaf>\<filename>；leaf 位数 = original.size() - fixed（>=1 时等长）。
		bool make_shared_dst(const std::wstring& original, std::wstring& dst, bool& equalLen)
		{
			equalLen = false;
			const std::wstring prefix = shared_root_prefix();
			if (prefix.empty())
			{
				return false;
			}
			const std::wstring filename = std::filesystem::path{original}.filename().native();
			if (filename.empty())
			{
				return false;
			}

			const std::size_t fixed = prefix.size() + filename.size() + 2; // prefix \ leaf \ filename
			std::wstring leaf;
			if (original.size() > fixed)
			{
				leaf = make_leaf(original.size() - fixed);
				if (!leaf.empty())
				{
					equalLen = true;
				}
			}
			if (leaf.empty())
			{
				leaf = guid_hex(); // 非等长兜底（原路径过短）
				if (leaf.empty())
				{
					return false;
				}
			}

			std::filesystem::path dir = std::filesystem::path{prefix} / std::filesystem::path{leaf};
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			if (ec)
			{
				return false;
			}
			const std::filesystem::path target = dir / std::filesystem::path{filename};
			dst = target.native();
			if (equalLen && dst.size() != original.size())
			{
				equalLen = false; // 二次校验
			}
			return true;
		}

		// 单个路径改写：仅当为“盘符绝对路径”且命中已知文件夹（即会被重定向）时才桥接。
		// 源用逻辑路径，交给现有 NtCreateFile/NtOpenFile hook 自动解析到影子或按需继承；
		// 目标为共享目录（非已知文件夹，不被重定向）。返回是否成功改写，equalLen 见上。
		bool rewrite_single_path(const std::wstring& original, std::wstring& out, bool& equalLen)
		{
			equalLen = false;
			if (original.size() < 3)
			{
				return false;
			}
			const wchar_t drive = original[0];
			if (!((drive >= L'a' && drive <= L'z') || (drive >= L'A' && drive <= L'Z')))
			{
				return false;
			}
			if (original[1] != L':' || (original[2] != L'\\' && original[2] != L'/'))
			{
				return false;
			}

			std::wstring ntPath;
			ntPath.reserve(original.size() + 4);
			ntPath.append(LR"(\??\)");
			ntPath.append(original);
			if (!global::Data::get().isInKnownFolderPath(ntPath))
			{
				return false; // 不在已知文件夹（含已是共享路径的情形）→ 无需改写
			}

			std::wstring dst;
			if (!make_shared_dst(original, dst, equalLen))
			{
				return false;
			}
			if (!::CopyFileW(original.c_str(), dst.c_str(), FALSE))
			{
				return false;
			}
			out = std::move(dst);
			return true;
		}

		// 解析 CF_HDROP 的 HGLOBAL，填充 out（原始逻辑路径）。用 GlobalSize 定界以防越界。
		bool parse_hdrop_paths(HGLOBAL hGlobal, std::vector<std::wstring>& out)
		{
			out.clear();
			if (!hGlobal)
			{
				return false;
			}
			const SIZE_T totalBytes = ::GlobalSize(hGlobal);
			if (totalBytes < sizeof(DROPFILES))
			{
				return false;
			}
			const void* base = ::GlobalLock(hGlobal);
			if (!base)
			{
				return false;
			}

			DROPFILES df{};
			memcpy(&df, base, sizeof(DROPFILES));
			if (df.pFiles >= totalBytes)
			{
				::GlobalUnlock(hGlobal);
				return false;
			}
			const BYTE* filesStart = static_cast<const BYTE*>(base) + df.pFiles;
			const SIZE_T filesBytes = totalBytes - df.pFiles;

			if (df.fWide)
			{
				const wchar_t* p = reinterpret_cast<const wchar_t*>(filesStart);
				const SIZE_T count = filesBytes / sizeof(wchar_t);
				SIZE_T idx = 0;
				while (idx < count)
				{
					const SIZE_T remaining = count - idx;
					const SIZE_T len = ::wcsnlen(p + idx, remaining);
					if (len == 0)
					{
						break; // 双 null 结束
					}
					if (len == remaining)
					{
						out.clear(); // 无终止符，视为非法
						break;
					}
					out.emplace_back(p + idx, len);
					idx += len + 1;
				}
			}
			else
			{
				const char* p = reinterpret_cast<const char*>(filesStart);
				SIZE_T idx = 0;
				while (idx < filesBytes)
				{
					const SIZE_T remaining = filesBytes - idx;
					const SIZE_T len = ::strnlen(p + idx, remaining);
					if (len == 0)
					{
						break;
					}
					if (len == remaining)
					{
						out.clear();
						break;
					}
					const int wlen = ::MultiByteToWideChar(CP_ACP, 0, p + idx, static_cast<int>(len), nullptr, 0);
					if (wlen > 0)
					{
						std::wstring wide(static_cast<std::size_t>(wlen), L'\0');
						::MultiByteToWideChar(CP_ACP, 0, p + idx, static_cast<int>(len), wide.data(), wlen);
						out.push_back(std::move(wide));
					}
					idx += len + 1;
				}
			}

			::GlobalUnlock(hGlobal);
			return !out.empty();
		}

		// 由路径列表构造一个全新的 CF_HDROP HGLOBAL（宽字符，双 null 结束）。
		HGLOBAL build_hdrop_global(const std::vector<std::wstring>& paths)
		{
			SIZE_T total = sizeof(DROPFILES) + sizeof(wchar_t);
			for (const std::wstring& path : paths)
			{
				total += (path.size() + 1) * sizeof(wchar_t);
			}
			HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total);
			if (!hGlobal)
			{
				return nullptr;
			}
			void* base = ::GlobalLock(hGlobal);
			if (!base)
			{
				::GlobalFree(hGlobal);
				return nullptr;
			}
			auto* df = static_cast<DROPFILES*>(base);
			df->pFiles = static_cast<DWORD>(sizeof(DROPFILES));
			df->pt.x = 0;
			df->pt.y = 0;
			df->fNC = FALSE;
			df->fWide = TRUE;
			auto* dst = reinterpret_cast<wchar_t*>(static_cast<BYTE*>(base) + sizeof(DROPFILES));
			for (const std::wstring& path : paths)
			{
				memcpy(dst, path.c_str(), path.size() * sizeof(wchar_t));
				dst += path.size();
				*dst++ = L'\0';
			}
			*dst = L'\0'; // 结束的双 null（GMEM_ZEROINIT 已保证，此处显式写出）
			::GlobalUnlock(hGlobal);
			return hGlobal;
		}

		// 由原始字节构造 HGLOBAL（用于文本/二进制格式改写）。
		HGLOBAL build_global_bytes(const std::string& bytes)
		{
			const SIZE_T sz = bytes.empty() ? 1 : bytes.size();
			HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, sz);
			if (!hGlobal)
			{
				return nullptr;
			}
			if (void* base = ::GlobalLock(hGlobal))
			{
				if (!bytes.empty())
				{
					memcpy(base, bytes.data(), bytes.size());
				}
				::GlobalUnlock(hGlobal);
			}
			return hGlobal;
		}

		// 宽字符串按指定代码页转字节串；空入参返回空。
		std::string wide_to(const std::wstring& s, UINT codePage)
		{
			if (s.empty())
			{
				return {};
			}
			const int n = ::WideCharToMultiByte(codePage, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
			if (n <= 0)
			{
				return {};
			}
			std::string out(static_cast<std::size_t>(n), '\0');
			::WideCharToMultiByte(codePage, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
			return out;
		}

		// 宽字符串的 UTF-16LE 原始字节（用于匹配以宽字符存储的格式）。
		std::string wide_to_utf16le_bytes(const std::wstring& s)
		{
			std::string out;
			out.resize(s.size() * sizeof(wchar_t));
			if (!out.empty())
			{
				memcpy(out.data(), s.data(), out.size());
			}
			return out;
		}

		// 在字节串中把所有 from 替换为 to（左到右扫描，不重扫插入内容）。返回是否有替换。
		bool replace_all(std::string& s, const std::string& from, const std::string& to)
		{
			if (from.empty())
			{
				return false;
			}
			bool any = false;
			std::size_t pos = 0;
			while ((pos = s.find(from, pos)) != std::string::npos)
			{
				s.replace(pos, from.size(), to);
				pos += to.size();
				any = true;
			}
			return any;
		}

		// 路径反斜杠变体（用于匹配 file:/// 形式）。
		std::wstring to_forward_slash(std::wstring s)
		{
			for (wchar_t& c : s)
			{
				if (c == L'\\')
				{
					c = L'/';
				}
			}
			return s;
		}

		// 定期清理共享目录：删除最后写入早于 24h 的 token 子目录。频率限制 5 分钟。
		void maybe_sweep_shared_root()
		{
			static std::atomic<ULONGLONG> s_lastSweep{0}; // 常量初始化，无 guard
			const ULONGLONG now = ::GetTickCount64();
			ULONGLONG last = s_lastSweep.load(std::memory_order_relaxed);
			if (now < last + kSweepIntervalMs)
			{
				return;
			}
			if (!s_lastSweep.compare_exchange_strong(last, now, std::memory_order_relaxed))
			{
				return; // 其他线程已认领本次扫描
			}

			try
			{
				const std::filesystem::path root = std::filesystem::path{global::Data::get().rootPath()}
					/ std::filesystem::path{L"ClipboardShare"};
				std::error_code ec;
				if (!std::filesystem::exists(root, ec))
				{
					return;
				}
				const auto cutoff = std::filesystem::file_time_type::clock::now() - kSharedFileMaxAge;
				for (const auto& envDir : std::filesystem::directory_iterator(root, ec))
				{
					if (ec)
					{
						break;
					}
					std::error_code dec;
					if (!envDir.is_directory(dec))
					{
						continue;
					}
					for (const auto& itemDir : std::filesystem::directory_iterator(envDir.path(), ec))
					{
						if (ec)
						{
							break;
						}
						std::error_code idec;
						if (!itemDir.is_directory(idec))
						{
							continue;
						}
						std::error_code tec;
						const auto writeTime = std::filesystem::last_write_time(itemDir.path(), tec);
						if (tec)
						{
							continue;
						}
						if (writeTime < cutoff)
						{
							std::error_code rec;
							std::filesystem::remove_all(itemDir.path(), rec);
						}
					}
				}
			}
			catch (...)
			{
			}
		}

		// 动态剪贴板格式 ID（RegisterClipboardFormat 幂等，函数内静态缓存）。
		UINT fmt_uri_list()
		{
			static const UINT v = ::RegisterClipboardFormatW(L"text/uri-list");
			return v;
		}
		UINT fmt_moz_url()
		{
			static const UINT v = ::RegisterClipboardFormatW(L"text/x-moz-url");
			return v;
		}
		UINT fmt_html()
		{
			static const UINT v = ::RegisterClipboardFormatW(L"HTML Format");
			return v;
		}
		UINT fmt_wework_message()
		{
			static const UINT v = ::RegisterClipboardFormatW(L"WeWork Message");
			return v;
		}

		bool is_text_bridged_format(UINT fmt)
		{
			const UINT uri = fmt_uri_list();
			const UINT moz = fmt_moz_url();
			const UINT html = fmt_html();
			return (uri && fmt == uri) || (moz && fmt == moz) || (html && fmt == html);
		}

		// 探测数据对象是否提供 CF_HDROP 文件列表；只有提供时才包代理，避免无关复制受影响。
		bool data_object_has_hdrop(IDataObject* obj)
		{
			if (!obj)
			{
				return false;
			}
			FORMATETC fe{};
			fe.cfFormat = CF_HDROP;
			fe.ptd = nullptr;
			fe.dwAspect = DVASPECT_CONTENT;
			fe.lindex = -1;
			fe.tymed = TYMED_HGLOBAL;
			if (obj->QueryGetData(&fe) == S_OK)
			{
				return true;
			}
			IEnumFORMATETC* enumFmt = nullptr;
			if (SUCCEEDED(obj->EnumFormatEtc(DATADIR_GET, &enumFmt)) && enumFmt)
			{
				FORMATETC tmp{};
				ULONG fetched = 0;
				while (enumFmt->Next(1, &tmp, &fetched) == S_OK && fetched == 1)
				{
					if (tmp.cfFormat == CF_HDROP)
					{
						enumFmt->Release();
						return true;
					}
				}
				enumFmt->Release();
			}
			return false;
		}

		// 单条桥接映射：原始逻辑路径 → 共享路径。
		struct BridgeEntry
		{
			std::wstring original;
			std::wstring shared;
			bool equalLen = false;
			bool asciiOriginal = false;
			bool asciiShared = false;
		};

		bool is_ascii(const std::wstring& s)
		{
			for (const wchar_t c : s)
			{
				if (c >= 0x80)
				{
					return false;
				}
			}
			return true;
		}

		// IDataObject 代理：改写所有携带路径的格式（CF_HDROP / 文本类 / WeWork Message），
		// 其余方法一律委托内层。复制设置时（构造）一次性建立映射并拷贝文件。
		// 由于 OLE 剪贴板管理器在“源进程内”调用 GetData 完成延迟渲染，代理无需实现
		// IMarshal/IPersist（QI 对二者返回 E_NOINTERFACE，避免把内层对象委托出去导致
		// 解封到原始内层对象而丢失路径改写）。
		class ClipboardDataObjectProxy final : public IDataObject
		{
		public:
			explicit ClipboardDataObjectProxy(IDataObject* inner) : m_inner(inner)
			{
				if (m_inner)
				{
					m_inner->AddRef();
					build_mapping();
				}
			}

			bool empty() const
			{
				return m_entries.empty();
			}

			// IUnknown
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
			{
				if (!ppvObject)
				{
					return E_INVALIDARG;
				}
				*ppvObject = nullptr;
				if (riid == IID_IUnknown || riid == IID_IDataObject)
				{
					*ppvObject = static_cast<IDataObject*>(this);
					AddRef();
					return S_OK;
				}
				return E_NOINTERFACE;
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				return static_cast<ULONG>(::InterlockedIncrement(&m_ref));
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				const ULONG count = static_cast<ULONG>(::InterlockedDecrement(&m_ref));
				if (count == 0)
				{
					delete this;
				}
				return count;
			}

			// IDataObject
			HRESULT STDMETHODCALLTYPE GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override
			{
				if (!pformatetcIn || !pmedium || !m_inner)
				{
					return E_INVALIDARG;
				}
				const UINT fmt = pformatetcIn->cfFormat;
				const bool hglobal = (pformatetcIn->tymed & TYMED_HGLOBAL) != 0;
				const bool wework = (fmt == fmt_wework_message());
				if (!hglobal || m_entries.empty() || !(fmt == CF_HDROP || wework || is_text_bridged_format(fmt)))
				{
					return m_inner->GetData(pformatetcIn, pmedium);
				}

				STGMEDIUM medium{};
				const HRESULT hr = m_inner->GetData(pformatetcIn, &medium);
				if (FAILED(hr))
				{
					return hr;
				}
				if (medium.tymed != TYMED_HGLOBAL || !medium.hGlobal)
				{
					*pmedium = medium;
					return S_OK;
				}

				HGLOBAL rewritten = nullptr;
				try
				{
					if (fmt == CF_HDROP)
					{
						rewritten = rebuild_hdrop(medium.hGlobal);
					}
					else if (wework)
					{
						rewritten = rewrite_binary_equal_len(medium.hGlobal);
					}
					else
					{
						rewritten = rewrite_text(medium.hGlobal, fmt == fmt_html());
					}
				}
				catch (...)
				{
					rewritten = nullptr;
				}

				if (rewritten)
				{
					::ReleaseStgMedium(&medium);
					pmedium->tymed = TYMED_HGLOBAL;
					pmedium->hGlobal = rewritten;
					pmedium->pUnkForRelease = nullptr;
					return S_OK;
				}

				*pmedium = medium;
				return S_OK;
			}

			HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* pformatetc, STGMEDIUM* pmedium) override
			{
				return m_inner ? m_inner->GetDataHere(pformatetc, pmedium) : E_NOTIMPL;
			}

			HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* pformatetc) override
			{
				return m_inner ? m_inner->QueryGetData(pformatetc) : E_NOTIMPL;
			}

			HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* pformatectIn, FORMATETC* pformatetcOut) override
			{
				return m_inner ? m_inner->GetCanonicalFormatEtc(pformatectIn, pformatetcOut) : E_NOTIMPL;
			}

			HRESULT STDMETHODCALLTYPE SetData(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) override
			{
				return m_inner ? m_inner->SetData(pformatetc, pmedium, fRelease) : E_NOTIMPL;
			}

			HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) override
			{
				return m_inner ? m_inner->EnumFormatEtc(dwDirection, ppenumFormatEtc) : E_NOTIMPL;
			}

			HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override
			{
				return OLE_E_ADVISENOTSUPPORTED;
			}

			HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override
			{
				return OLE_E_ADVISENOTSUPPORTED;
			}

			HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override
			{
				return OLE_E_ADVISENOTSUPPORTED;
			}

		private:
			~ClipboardDataObjectProxy()
			{
				if (m_inner)
				{
					m_inner->Release();
				}
			}

			// 从内层读取 CF_HDROP，逐条构造映射并拷贝文件到共享目录。
			void build_mapping()
			{
				FORMATETC fe{};
				fe.cfFormat = CF_HDROP;
				fe.ptd = nullptr;
				fe.dwAspect = DVASPECT_CONTENT;
				fe.lindex = -1;
				fe.tymed = TYMED_HGLOBAL;
				STGMEDIUM med{};
				if (FAILED(m_inner->GetData(&fe, &med)))
				{
					return;
				}
				if (med.tymed == TYMED_HGLOBAL && med.hGlobal)
				{
					std::vector<std::wstring> paths;
					if (parse_hdrop_paths(med.hGlobal, paths))
					{
						bool copied = false;
						for (std::wstring& path : paths)
						{
							std::wstring dst;
							bool eq = false;
							if (!rewrite_single_path(path, dst, eq))
							{
								continue;
							}
							BridgeEntry e;
							e.original = std::move(path);
							e.shared = std::move(dst);
							e.equalLen = eq;
							e.asciiOriginal = is_ascii(e.original);
							e.asciiShared = is_ascii(e.shared);
							m_entries.push_back(std::move(e));
							copied = true;
						}
						if (copied)
						{
							maybe_sweep_shared_root();
						}
					}
				}
				::ReleaseStgMedium(&med);
			}

			const BridgeEntry* find_entry(const std::wstring& original) const
			{
				for (const BridgeEntry& e : m_entries)
				{
					if (e.original == original)
					{
						return &e;
					}
				}
				return nullptr;
			}

			// CF_HDROP：按映射重建路径列表并生成新 HGLOBAL。
			HGLOBAL rebuild_hdrop(HGLOBAL src)
			{
				std::vector<std::wstring> paths;
				if (!parse_hdrop_paths(src, paths))
				{
					return nullptr;
				}
				std::vector<std::wstring> out;
				out.reserve(paths.size());
				bool changed = false;
				for (std::wstring& p : paths)
				{
					if (const BridgeEntry* e = find_entry(p))
					{
						out.push_back(e->shared);
						changed = true;
					}
					else
					{
						out.push_back(std::move(p));
					}
				}
				if (!changed)
				{
					return nullptr;
				}
				return build_hdrop_global(out);
			}

			// 文本类格式（uri-list / moz-url / HTML）：按 UTF-8 / ACP / UTF-16LE 三种编码子串替换。
			// HTML 含头部字节偏移，故仅做长度守恒替换（lengthPreservingOnly）。
			HGLOBAL rewrite_text(HGLOBAL src, bool lengthPreservingOnly)
			{
				const SIZE_T sz = ::GlobalSize(src);
				if (sz == 0)
				{
					return nullptr;
				}
				const void* base = ::GlobalLock(src);
				if (!base)
				{
					return nullptr;
				}
				std::string buf(static_cast<const char*>(base), sz);
				::GlobalUnlock(src);

				bool changed = false;
				for (const BridgeEntry& e : m_entries)
				{
					const std::wstring froms[2] = { e.original, to_forward_slash(e.original) };
					const std::wstring tos[2] = { e.shared, to_forward_slash(e.shared) };
					for (int i = 0; i < 2; ++i)
					{
						const std::string utf8F = wide_to(froms[i], CP_UTF8);
						const std::string utf8T = wide_to(tos[i], CP_UTF8);
						if (!lengthPreservingOnly || utf8F.size() == utf8T.size())
						{
							if (replace_all(buf, utf8F, utf8T))
							{
								changed = true;
							}
						}
						const std::string acpF = wide_to(froms[i], CP_ACP);
						const std::string acpT = wide_to(tos[i], CP_ACP);
						if (!lengthPreservingOnly || acpF.size() == acpT.size())
						{
							if (replace_all(buf, acpF, acpT))
							{
								changed = true;
							}
						}
						const std::string wF = wide_to_utf16le_bytes(froms[i]);
						const std::string wT = wide_to_utf16le_bytes(tos[i]);
						if (!lengthPreservingOnly || wF.size() == wT.size())
						{
							if (replace_all(buf, wF, wT))
							{
								changed = true;
							}
						}
					}
				}
				if (!changed)
				{
					return nullptr;
				}
				return build_global_bytes(buf);
			}

			// 二进制格式（WeWork Message / Boost 档案）：仅做等长原地替换，长度守恒，
			// 不破坏单字节长度前缀与后续偏移。要求原文与目标均为纯 ASCII。
			HGLOBAL rewrite_binary_equal_len(HGLOBAL src)
			{
				const SIZE_T sz = ::GlobalSize(src);
				if (sz == 0)
				{
					return nullptr;
				}
				const void* base = ::GlobalLock(src);
				if (!base)
				{
					return nullptr;
				}
				std::string buf(static_cast<const char*>(base), sz);
				::GlobalUnlock(src);

				bool changed = false;
				for (const BridgeEntry& e : m_entries)
				{
					if (!e.equalLen || !e.asciiOriginal || !e.asciiShared)
					{
						continue;
					}
					const std::wstring fwdF = to_forward_slash(e.original);
					const std::wstring fwdT = to_forward_slash(e.shared);
					const std::string f1 = wide_to(e.original, CP_ACP);
					const std::string t1 = wide_to(e.shared, CP_ACP);
					const std::string f2 = wide_to(fwdF, CP_ACP);
					const std::string t2 = wide_to(fwdT, CP_ACP);
					if (f1.size() == t1.size() && replace_all(buf, f1, t1))
					{
						changed = true;
					}
					if (f2.size() == t2.size() && replace_all(buf, f2, t2))
					{
						changed = true;
					}
				}
				if (!changed)
				{
					return nullptr;
				}
				return build_global_bytes(buf);
			}

			IDataObject* m_inner;
			LONG m_ref{1};
			std::vector<BridgeEntry> m_entries;
		};
	}

	// 主路径：企业微信（Chromium/CEF）经 OleSetClipboard(IDataObject*) 设置剪贴板。
	template <auto Trampoline>
	HRESULT STDAPICALLTYPE OleSetClipboard(IDataObject* pDataObj)
	{
		if (!pDataObj)
		{
			return Trampoline(pDataObj);
		}
		ClipboardDataObjectProxy* proxy = nullptr;
		try
		{
			if (!data_object_has_hdrop(pDataObj))
			{
				return Trampoline(pDataObj); // 无文件引用（文本/图片）→ 完全不干预
			}
			proxy = new ClipboardDataObjectProxy(pDataObj); // 构造内建立映射并拷贝文件
			if (proxy->empty())
			{
				proxy->Release();
				return Trampoline(pDataObj); // 无可桥接文件 → 不干预
			}
		}
		catch (...)
		{
			if (proxy)
			{
				proxy->Release();
			}
			return Trampoline(pDataObj);
		}
		const HRESULT hr = Trampoline(proxy);
		proxy->Release(); // OleSetClipboard 内部会 AddRef
		return hr;
	}

	// 备路径：应用直接用 Win32 剪贴板 SetClipboardData(CF_HDROP, HGLOBAL)。
	template <auto Trampoline>
	HANDLE WINAPI SetClipboardData(UINT uFormat, HANDLE hMem)
	{
		if (uFormat != CF_HDROP || !hMem)
		{
			return Trampoline(uFormat, hMem);
		}
		try
		{
			std::vector<std::wstring> paths;
			if (parse_hdrop_paths(static_cast<HGLOBAL>(hMem), paths))
			{
				std::vector<std::wstring> out;
				out.reserve(paths.size());
				bool changed = false;
				for (std::wstring& p : paths)
				{
					std::wstring dst;
					bool eq = false;
					if (rewrite_single_path(p, dst, eq))
					{
						out.push_back(std::move(dst));
						changed = true;
					}
					else
					{
						out.push_back(std::move(p));
					}
				}
				if (changed)
				{
					if (HGLOBAL rewritten = build_hdrop_global(out))
					{
						maybe_sweep_shared_root();
						return Trampoline(uFormat, rewritten);
					}
				}
			}
		}
		catch (...)
		{
		}
		return Trampoline(uFormat, hMem);
	}

	void hook_clipboard()
	{
		create_hook_by_func_ptr<&::OleSetClipboard>().setHookFromGetter([&](auto trampolineConst)
		{
			return HookInfo{&OleSetClipboard<trampolineConst.value>};
		});
		create_hook_by_func_ptr<&::SetClipboardData>().setHookFromGetter([&](auto trampolineConst)
		{
			return HookInfo{&SetClipboardData<trampolineConst.value>};
		});
	}
}
