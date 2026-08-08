// ReSharper disable CppClangTidyClangDiagnosticUnusedFunction
module;
#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
module biz.LicenseServerClient;

import std;

namespace
{
	// 授权服务器默认地址（生产环境：abc222.cn，宝塔 Nginx 反代到后端 3008）：
	//   - 必须使用 https：WinHTTP 的 POST 不会跟随 http->https 的 301 跳转，
	//     直连 https 才能保证在线激活/心跳/解绑生效
	//   - 客户端优先读注册表 HKCU\Software\2Box\ServerUrl 覆盖（联调/换域名免重新编译）
	//   - 未配置注册表时使用下方默认地址
	//   - 服务器不可达时按离线方式放行（forceOnlineActivate 默认 false），
	//     本机开发机的离线激活码与在线版本互不干扰
	constexpr wchar_t kDefaultServerUrl[] = L"https://abc222.cn";

	// 与 biz.License 共享注册表子键
	constexpr wchar_t kRegSubKey[] = L"Software\\2Box";
	constexpr wchar_t kRegServerUrl[] = L"ServerUrl";
	constexpr wchar_t kRegHeartbeatHours[] = L"HeartbeatIntervalHours";
	constexpr wchar_t kRegGraceDays[] = L"OfflineGraceDays";
	constexpr wchar_t kRegForceOnline[] = L"ForceOnlineActivate";
	constexpr wchar_t kRegServerStatus[] = L"ServerStatus";
	constexpr wchar_t kRegWasOnline[] = L"WasOnline";
	constexpr wchar_t kRegLastHeartbeatAt[] = L"LastHeartbeatAt";
	constexpr wchar_t kRegGraceUntil[] = L"GraceUntil";
	constexpr wchar_t kRegNotice[] = L"Notice";

	// ---------------------------------------------------------------------------
	// UTF-8 <-> 宽字符
	// ---------------------------------------------------------------------------
	std::string wideToUtf8(const std::wstring& w)
	{
		if (w.empty())
		{
			return {};
		}
		const int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
		if (size <= 0)
		{
			return {};
		}
		std::string out(static_cast<std::size_t>(size), 0);
		WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), size, nullptr, nullptr);
		return out;
	}

	std::wstring utf8ToWide(std::string_view s)
	{
		if (s.empty())
		{
			return {};
		}
		const int size = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		if (size <= 0)
		{
			return {};
		}
		std::wstring out(static_cast<std::size_t>(size), 0);
		MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), size);
		return out;
	}

	// ---------------------------------------------------------------------------
	// 极简 JSON 解析（仅需读取服务器响应中的已知字段）
	// ---------------------------------------------------------------------------
	struct Json
	{
		enum class Type { Null, Bool, Number, String, Array, Object };
		Type type{Type::Null};
		bool b{false};
		double num{0};
		std::string str;
		std::vector<Json> arr;
		std::map<std::string, Json, std::less<>> obj;

		const Json* find(std::string_view key) const
		{
			if (type != Type::Object)
			{
				return nullptr;
			}
			const auto it = obj.find(key);
			return it == obj.end() ? nullptr : &it->second;
		}

		std::string_view stringValue(std::string_view key) const
		{
			const Json* v = find(key);
			return (v && v->type == Type::String) ? std::string_view{v->str} : std::string_view{};
		}

		std::int64_t numberValue(std::string_view key) const
		{
			const Json* v = find(key);
			return (v && v->type == Type::Number) ? static_cast<std::int64_t>(v->num) : 0;
		}

		bool boolValue(std::string_view key, bool def = false) const
		{
			const Json* v = find(key);
			return (v && v->type == Type::Bool) ? v->b : def;
		}
	};

	class JsonParser
	{
	public:
		explicit JsonParser(std::string_view text) : m_text(text) {}

		bool parse(Json& out)
		{
			skipWs();
			if (!parseValue(out))
			{
				return false;
			}
			skipWs();
			return m_pos == m_text.size();
		}

	private:
		std::string_view m_text;
		std::size_t m_pos{0};

		void skipWs()
		{
			while (m_pos < m_text.size() &&
			       (m_text[m_pos] == ' ' || m_text[m_pos] == '\t' || m_text[m_pos] == '\r' || m_text[m_pos] == '\n'))
			{
				++m_pos;
			}
		}

		bool parseValue(Json& v)
		{
			if (m_pos >= m_text.size())
			{
				return false;
			}
			const char c = m_text[m_pos];
			if (c == '{')
			{
				return parseObject(v);
			}
			if (c == '[')
			{
				return parseArray(v);
			}
			if (c == '"')
			{
				v.type = Json::Type::String;
				return parseString(v.str);
			}
			if (m_text.substr(m_pos, 4) == "true")
			{
				v.type = Json::Type::Bool;
				v.b = true;
				m_pos += 4;
				return true;
			}
			if (m_text.substr(m_pos, 5) == "false")
			{
				v.type = Json::Type::Bool;
				v.b = false;
				m_pos += 5;
				return true;
			}
			if (m_text.substr(m_pos, 4) == "null")
			{
				v.type = Json::Type::Null;
				m_pos += 4;
				return true;
			}
			return parseNumber(v);
		}

		bool parseString(std::string& out)
		{
			if (m_pos >= m_text.size() || m_text[m_pos] != '"')
			{
				return false;
			}
			++m_pos;
			out.clear();
			while (m_pos < m_text.size())
			{
				const char c = m_text[m_pos++];
				if (c == '"')
				{
					return true;
				}
				if (c == '\\')
				{
					if (m_pos >= m_text.size())
					{
						return false;
					}
					const char e = m_text[m_pos++];
					switch (e)
					{
					case '"': out.push_back('"'); break;
					case '\\': out.push_back('\\'); break;
					case '/': out.push_back('/'); break;
					case 'b': out.push_back('\b'); break;
					case 'f': out.push_back('\f'); break;
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					case 'u':
						if (m_pos + 4 > m_text.size())
						{
							return false;
						}
						{
							unsigned code = 0;
							for (int i = 0; i < 4; ++i)
							{
								const char h = m_text[m_pos++];
								code <<= 4;
								if (h >= '0' && h <= '9')
								{
									code |= static_cast<unsigned>(h - '0');
								}
								else if (h >= 'a' && h <= 'f')
								{
									code |= static_cast<unsigned>(h - 'a' + 10);
								}
								else if (h >= 'A' && h <= 'F')
								{
									code |= static_cast<unsigned>(h - 'A' + 10);
								}
								else
								{
									return false;
								}
							}
							if (code < 0x80)
							{
								out.push_back(static_cast<char>(code));
							}
							else if (code < 0x800)
							{
								out.push_back(static_cast<char>(0xC0 | (code >> 6)));
								out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
							}
							else
							{
								out.push_back(static_cast<char>(0xE0 | (code >> 12)));
								out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
								out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
							}
						}
						break;
					default:
						return false;
					}
				}
				else
				{
					out.push_back(c);
				}
			}
			return false;
		}

		bool parseNumber(Json& v)
		{
			const std::size_t start = m_pos;
			if (m_pos < m_text.size() && m_text[m_pos] == '-')
			{
				++m_pos;
			}
			while (m_pos < m_text.size() &&
			       (std::isdigit(static_cast<unsigned char>(m_text[m_pos])) || m_text[m_pos] == '.' ||
			        m_text[m_pos] == 'e' || m_text[m_pos] == 'E' || m_text[m_pos] == '+' || m_text[m_pos] == '-'))
			{
				++m_pos;
			}
			if (m_pos == start)
			{
				return false;
			}
			try
			{
				v.type = Json::Type::Number;
				v.num = std::stod(std::string{m_text.substr(start, m_pos - start)});
				return true;
			}
			catch (...)
			{
				return false;
			}
		}

		bool parseArray(Json& v)
		{
			v.type = Json::Type::Array;
			++m_pos; // '['
			skipWs();
			if (m_pos < m_text.size() && m_text[m_pos] == ']')
			{
				++m_pos;
				return true;
			}
			for (;;)
			{
				Json item;
				if (!parseValue(item))
				{
					return false;
				}
				v.arr.push_back(std::move(item));
				skipWs();
				if (m_pos >= m_text.size())
				{
					return false;
				}
				const char c = m_text[m_pos++];
				if (c == ']')
				{
					return true;
				}
				if (c != ',')
				{
					return false;
				}
				skipWs();
			}
		}

		bool parseObject(Json& v)
		{
			v.type = Json::Type::Object;
			++m_pos; // '{'
			skipWs();
			if (m_pos < m_text.size() && m_text[m_pos] == '}')
			{
				++m_pos;
				return true;
			}
			for (;;)
			{
				skipWs();
				std::string key;
				if (!parseString(key))
				{
					return false;
				}
				skipWs();
				if (m_pos >= m_text.size() || m_text[m_pos] != ':')
				{
					return false;
				}
				++m_pos;
				Json value;
				if (!parseValue(value))
				{
					return false;
				}
				v.obj.emplace(std::move(key), std::move(value));
				skipWs();
				if (m_pos >= m_text.size())
				{
					return false;
				}
				const char c = m_text[m_pos++];
				if (c == '}')
				{
					return true;
				}
				if (c != ',')
				{
					return false;
				}
			}
		}
	};

	// ---------------------------------------------------------------------------
	// URL 解析与同步 WinHTTP 请求
	// ---------------------------------------------------------------------------
	struct ParsedUrl
	{
		std::wstring host;
		unsigned short port{0};
		std::wstring path;
		bool secure{false};
	};

	bool parseUrl(const std::wstring& url, ParsedUrl& out)
	{
		const std::size_t schemeEnd = url.find(L"://");
		if (schemeEnd == std::wstring::npos)
		{
			return false;
		}
		const std::wstring scheme = url.substr(0, schemeEnd);
		const std::size_t start = schemeEnd + 3;
		const std::size_t end = url.find_first_of(L"/?#", start);
		std::wstring authority = (end == std::wstring::npos) ? url.substr(start) : url.substr(start, end - start);
		if (authority.empty())
		{
			return false;
		}
		const std::size_t colon = authority.find(L':');
		if (colon == std::wstring::npos)
		{
			out.host = authority;
			out.port = 0;
		}
		else
		{
			out.host = authority.substr(0, colon);
			try
			{
				out.port = static_cast<unsigned short>(std::stoul(authority.substr(colon + 1)));
			}
			catch (...)
			{
				return false;
			}
		}
		if (out.host.empty())
		{
			return false;
		}
		out.path = (end == std::wstring::npos) ? L"/" : url.substr(end);
		if (out.path.empty())
		{
			out.path = L"/";
		}
		out.secure = (scheme == L"https");
		if (out.port == 0)
		{
			out.port = out.secure ? 443 : 80;
		}
		return true;
	}

	struct HttpResponse
	{
		bool ok{false};
		DWORD status{0};
		std::string body;
		std::wstring error;
	};

	HttpResponse httpPostJson(const std::wstring& url, const std::string& jsonBody, unsigned timeoutMs = 10000)
	{
		HttpResponse resp;
		ParsedUrl parsed;
		if (!parseUrl(url, parsed))
		{
			resp.error = L"无效的授权服务器地址";
			return resp;
		}

		HINTERNET hSession = WinHttpOpen(L"eBox License Agent", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!hSession)
		{
			resp.error = std::format(L"WinHttpOpen 失败: {}", GetLastError());
			return resp;
		}
		WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

		HINTERNET hConnect = WinHttpConnect(hSession, parsed.host.c_str(), parsed.port, 0);
		if (!hConnect)
		{
			resp.error = std::format(L"WinHttpConnect 失败: {}", GetLastError());
			WinHttpCloseHandle(hSession);
			return resp;
		}

		const DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
		HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", parsed.path.c_str(),
		                                        nullptr, WINHTTP_NO_REFERER,
		                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
		if (!hRequest)
		{
			resp.error = std::format(L"WinHttpOpenRequest 失败: {}", GetLastError());
			WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			return resp;
		}
		if (parsed.secure)
		{
			// 兼容自签名证书（自建/内网部署联调）
			DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
			                 SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
			WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
		}

		constexpr wchar_t kContentType[] = L"Content-Type: application/json\r\n";
		const std::vector<BYTE> body(jsonBody.begin(), jsonBody.end());
		const BOOL sent = WinHttpSendRequest(hRequest, kContentType, static_cast<DWORD>(std::size(kContentType) - 1),
		                                     const_cast<BYTE*>(body.data()), static_cast<DWORD>(body.size()),
		                                     static_cast<DWORD>(body.size()), 0);
		if (!sent)
		{
			resp.error = std::format(L"WinHttpSendRequest 失败: {}", GetLastError());
			WinHttpCloseHandle(hRequest);
			WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			return resp;
		}
		if (!WinHttpReceiveResponse(hRequest, nullptr))
		{
			resp.error = std::format(L"WinHttpReceiveResponse 失败: {}", GetLastError());
			WinHttpCloseHandle(hRequest);
			WinHttpCloseHandle(hConnect);
			WinHttpCloseHandle(hSession);
			return resp;
		}

		DWORD statusCode = 0;
		DWORD statusSize = sizeof(statusCode);
		WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		                    WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
		resp.status = statusCode;

		DWORD available = 0;
		while (WinHttpQueryDataAvailable(hRequest, &available) && available > 0)
		{
			std::vector<char> buf(available);
			DWORD read = 0;
			if (!WinHttpReadData(hRequest, buf.data(), available, &read))
			{
				break;
			}
			resp.body.append(buf.data(), read);
		}

		resp.ok = true;
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return resp;
	}

	// ---------------------------------------------------------------------------
	// JSON 请求体构造
	// ---------------------------------------------------------------------------
	std::string jsonEscape(std::string_view s)
	{
		std::string out;
		out.reserve(s.size() + 8);
		for (const char c : s)
		{
			switch (c)
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20)
				{
					out += std::format("\\u{:04x}", static_cast<unsigned>(c));
				}
				else
				{
					out.push_back(c);
				}
			}
		}
		return out;
	}

	std::string randomNonce()
	{
		std::random_device rd;
		std::mt19937_64 gen(rd());
		return std::format("{:016x}", gen());
	}

	std::string buildJsonBody(const std::wstring& code, const std::wstring& machineFp,
	                          const std::wstring& appVersion, const std::wstring& os)
	{
		const std::int64_t ts = static_cast<std::int64_t>(std::time(nullptr));
		std::string body;
		body.reserve(128 + (code.size() + machineFp.size() + appVersion.size() + os.size()) * 2);
		body += "{\"code\":\"";
		body += jsonEscape(wideToUtf8(code));
		body += "\",\"machineFp\":\"";
		body += jsonEscape(wideToUtf8(machineFp));
		body += "\",\"appVersion\":\"";
		body += jsonEscape(wideToUtf8(appVersion));
		body += "\",\"os\":\"";
		body += jsonEscape(wideToUtf8(os));
		body += "\",\"timestamp\":";
		body += std::to_string(ts);
		body += ",\"nonce\":\"";
		body += randomNonce();
		body += "\"}";
		return body;
	}

	// ---------------------------------------------------------------------------
	// 注册表读写（HKCU\Software\2Box）
	// ---------------------------------------------------------------------------
	bool regReadString(const wchar_t* valueName, std::wstring& out)
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
		{
			return false;
		}
		DWORD size = 0;
		LSTATUS st = RegQueryValueExW(hKey, valueName, nullptr, nullptr, nullptr, &size);
		if (st == ERROR_SUCCESS && size > 0)
		{
			out.resize(size / sizeof(wchar_t));
			st = RegQueryValueExW(hKey, valueName, nullptr, nullptr,
			                      reinterpret_cast<LPBYTE>(out.data()), &size);
			if (st == ERROR_SUCCESS && !out.empty() && out.back() == L'\0')
			{
				out.pop_back();
			}
			else
			{
				st = ERROR_FILE_NOT_FOUND;
			}
		}
		RegCloseKey(hKey);
		return st == ERROR_SUCCESS && !out.empty();
	}

	void regWriteString(const wchar_t* valueName, const std::wstring& value)
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
			RegSetValueExW(hKey, valueName, 0, REG_SZ,
			               reinterpret_cast<const BYTE*>(value.c_str()), bytes);
			RegCloseKey(hKey);
		}
	}

	std::int64_t regReadQword(const wchar_t* valueName, std::int64_t def = 0)
	{
		HKEY hKey = nullptr;
		std::int64_t result = def;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			DWORD type = 0;
			DWORD cb = sizeof(result);
			if (RegQueryValueExW(hKey, valueName, nullptr, &type,
			                     reinterpret_cast<LPBYTE>(&result), &cb) != ERROR_SUCCESS || type != REG_QWORD)
			{
				result = def;
			}
			RegCloseKey(hKey);
		}
		return result;
	}

	void regWriteQword(const wchar_t* valueName, std::int64_t value)
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExW(hKey, valueName, 0, REG_QWORD,
			               reinterpret_cast<const BYTE*>(&value), sizeof(value));
			RegCloseKey(hKey);
		}
	}

	DWORD regReadDword(const wchar_t* valueName, DWORD def = 0)
	{
		HKEY hKey = nullptr;
		DWORD result = def;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			DWORD cb = sizeof(result);
			RegQueryValueExW(hKey, valueName, nullptr, nullptr,
			                 reinterpret_cast<LPBYTE>(&result), &cb);
			RegCloseKey(hKey);
		}
		return result;
	}

	void regWriteDword(const wchar_t* valueName, DWORD value)
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExW(hKey, valueName, 0, REG_DWORD,
			               reinterpret_cast<const BYTE*>(&value), sizeof(value));
			RegCloseKey(hKey);
		}
	}
}

namespace biz
{
	namespace licenseserver
	{
		std::wstring serverBaseUrl()
		{
			std::wstring overrideUrl;
			if (regReadString(kRegServerUrl, overrideUrl))
			{
				return overrideUrl;
			}
			return kDefaultServerUrl;
		}

		// 实时在线状态（内存）：最近一次激活/心跳是否连通服务器。
		// 区别于持久化的 WasOnline（仅表示"曾在线"），供 UI 区分"在线授权"与"离线宽限"
		std::atomic<bool>& onlineFlag()
		{
			static std::atomic<bool> flag{false};
			return flag;
		}

		void markOnline(bool online)
		{
			onlineFlag().store(online);
		}

		bool onlineNow()
		{
			return onlineFlag().load();
		}

		int heartbeatIntervalHours()
		{
			const DWORD v = regReadDword(kRegHeartbeatHours, 6);
			return (v >= 1 && v <= 24) ? static_cast<int>(v) : 6;
		}

		int offlineGraceDays()
		{
			const DWORD v = regReadDword(kRegGraceDays, 7);
			return (v >= 1 && v <= 30) ? static_cast<int>(v) : 7;
		}

		bool forceOnlineActivate()
		{
			return regReadDword(kRegForceOnline, 0) != 0;
		}

		void setServerUrl(const std::wstring& url)
		{
			if (url.empty())
			{
				// 空串 = 删除覆盖，回退默认地址
				HKEY hKey = nullptr;
				if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
				{
					RegDeleteValueW(hKey, kRegServerUrl);
					RegCloseKey(hKey);
				}
				return;
			}
			regWriteString(kRegServerUrl, url);
		}

		ActivateResult activate(const std::wstring& code, const std::wstring& machineFp,
		                        const std::wstring& appVersion, const std::wstring& os)
		{
			ActivateResult result;
			const HttpResponse resp = httpPostJson(serverBaseUrl() + L"/api/v1/activate",
			                                       buildJsonBody(code, machineFp, appVersion, os));
			if (!resp.ok || resp.status != 200)
			{
				result.msg = resp.error;
				return result;
			}
			Json root;
			if (!JsonParser(resp.body).parse(root))
			{
				result.msg = L"服务器响应解析失败";
				return result;
			}
			if (root.numberValue("code") != 0)
			{
				result.msg = utf8ToWide(root.stringValue("msg"));
				return result;
			}
			const Json* data = root.find("data");
			if (!data)
			{
				result.msg = L"服务器响应缺少 data";
				return result;
			}
			result.ok = true;
			result.online = data->boolValue("online");
			result.revoked = data->boolValue("revoked");
			result.expired = data->boolValue("expired");
			result.exceeded = data->boolValue("exceeded");
			result.serverTime = data->numberValue("serverTime");
			return result;
		}

		HeartbeatResult heartbeat(const std::wstring& code, const std::wstring& machineFp,
		                          const std::wstring& appVersion)
		{
			HeartbeatResult result;
			const HttpResponse resp = httpPostJson(serverBaseUrl() + L"/api/v1/heartbeat",
			                                       buildJsonBody(code, machineFp, appVersion, L""));
			if (!resp.ok || resp.status != 200)
			{
				return result;
			}
			Json root;
			if (!JsonParser(resp.body).parse(root) || root.numberValue("code") != 0)
			{
				return result;
			}
			const Json* data = root.find("data");
			if (!data)
			{
				return result;
			}
			result.ok = true;
			result.online = data->boolValue("online");
			result.status = utf8ToWide(data->stringValue("status"));
			result.graceUntil = data->numberValue("graceUntil");
			result.notice = utf8ToWide(data->stringValue("notice"));
			return result;
		}

		UnbindResult unbind(const std::wstring& code, const std::wstring& machineFp,
		                    const std::wstring& appVersion)
		{
			UnbindResult result;
			const HttpResponse resp = httpPostJson(serverBaseUrl() + L"/api/v1/unbind",
			                                       buildJsonBody(code, machineFp, appVersion, L""));
			if (!resp.ok || resp.status != 200)
			{
				return result;
			}
			Json root;
			if (!JsonParser(resp.body).parse(root))
			{
				return result;
			}
			if (root.numberValue("code") == 1006)
			{
				// 本月解绑次数已达上限
				result.ok = true;
				result.exceed = true;
				return result;
			}
			if (root.numberValue("code") != 0)
			{
				return result;
			}
			const Json* data = root.find("data");
			if (!data)
			{
				return result;
			}
			result.ok = true;
			result.newCode = utf8ToWide(data->stringValue("newCode"));
			if (!data->boolValue("online"))
			{
				result.offlineCode = true;  // 未登记码：服务器不托管解绑
			}
			return result;
		}

		void storeServerStatus(const std::wstring& status)
		{
			if (status.empty())
			{
				HKEY hKey = nullptr;
				if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
				{
					RegDeleteValueW(hKey, kRegServerStatus);
					RegCloseKey(hKey);
				}
				return;
			}
			regWriteString(kRegServerStatus, status);
		}

		std::wstring serverStatus()
		{
			std::wstring out;
			regReadString(kRegServerStatus, out);
			return out;
		}

		bool serverLocked()
		{
			const std::wstring st = serverStatus();
			return st == L"revoked" || st == L"expired" || st == L"kicked";
		}

		void storeOnlineHeartbeat(std::int64_t lastAt, std::int64_t graceUntil)
		{
			regWriteQword(kRegLastHeartbeatAt, lastAt);
			regWriteQword(kRegGraceUntil, graceUntil);
			regWriteDword(kRegWasOnline, 1);
			regWriteString(kRegServerStatus, L"ok");
		}

		std::int64_t graceUntil()
		{
			return regReadQword(kRegGraceUntil);
		}

		bool wasOnlineBefore()
		{
			return regReadDword(kRegWasOnline) != 0;
		}

		void recordOnline()
		{
			regWriteDword(kRegWasOnline, 1);
			regWriteQword(kRegLastHeartbeatAt, static_cast<std::int64_t>(std::time(nullptr)));
		}

		void storeNotice(const std::wstring& notice)
		{
			if (notice.empty())
			{
				// 服务端已撤下公告：删除注册表记录
				HKEY hKey = nullptr;
				if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
				{
					RegDeleteValueW(hKey, kRegNotice);
					RegCloseKey(hKey);
				}
				return;
			}
			regWriteString(kRegNotice, notice);
		}

		std::wstring currentNotice()
		{
			std::wstring out;
			regReadString(kRegNotice, out);
			return out;
		}
	}
}
