module;
#include <Windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <ctime>
#undef max   // Windows.h 定义 min/max 宏，会破坏 std::numeric_limits<T>::max()
#undef min
#pragma comment(lib, "bcrypt.lib")
module biz.License;

import std;
import biz.LicenseServerClient;

namespace
{
	// ---------------------------------------------------------------------------
	// 密钥材料（ECDSA P-256）
	// 公钥：BCRYPT_ECCPUBLIC_BLOB（dwMagic=BCRYPT_ECDSA_PUBLIC_P256_MAGIC, cbKey=32, X, Y）
	// 私钥：仅编译进发码工具（LICENSE_GENERATOR 宏），绝不分发给客户端
	// ---------------------------------------------------------------------------
#ifdef LICENSE_GENERATOR
	// 私钥 blob：BCRYPT_ECCPRIVATE_BLOB（dwMagic=BCRYPT_ECDSA_PRIVATE_P256_MAGIC, cbKey=32, X, Y, d）
	// 【密钥由 KeyGen genkey 生成后回填，禁止分发】
	static constexpr BYTE kPrivateKeyBlob[] = {
		0x45, 0x43, 0x53, 0x32, 0x20, 0x00, 0x00, 0x00, 0x2A, 0x89, 0x13, 0xC4,
		0x61, 0xD1, 0x44, 0xA1, 0x86, 0x59, 0xBB, 0xA9, 0x2F, 0xB7, 0x96, 0x2D,
		0x38, 0x07, 0xF2, 0x43, 0xA7, 0xD2, 0xFC, 0x39, 0x52, 0xCB, 0x1D, 0x1A,
		0x34, 0xAD, 0xC5, 0xA5, 0x81, 0x64, 0xBE, 0x69, 0x47, 0x23, 0xAE, 0xEA,
		0xEA, 0x4D, 0xF5, 0x4F, 0xDF, 0x6E, 0xFF, 0x09, 0x79, 0x4C, 0xCE, 0x71,
		0x36, 0x87, 0x58, 0x44, 0xD1, 0x12, 0xED, 0x23, 0x41, 0x16, 0x12, 0xF6,
		0x1E, 0x34, 0xAB, 0x09, 0x73, 0xD3, 0xC6, 0x0A, 0x02, 0xC1, 0xFD, 0xAE,
		0x95, 0x4C, 0x4D, 0x4D, 0x94, 0x92, 0x08, 0x4F, 0xAF, 0xB6, 0x42, 0x8A,
		0x17, 0xA8, 0x69, 0x91, 0xA2, 0x77, 0xD6, 0x6E,
	};
#endif

	// 公钥 blob：BCRYPT_ECCPUBLIC_BLOB
	// 【密钥由 KeyGen genkey 生成后回填】
	static constexpr BYTE kPublicKeyBlob[] = {
		0x45, 0x43, 0x53, 0x31, 0x20, 0x00, 0x00, 0x00, 0x2A, 0x89, 0x13, 0xC4,
		0x61, 0xD1, 0x44, 0xA1, 0x86, 0x59, 0xBB, 0xA9, 0x2F, 0xB7, 0x96, 0x2D,
		0x38, 0x07, 0xF2, 0x43, 0xA7, 0xD2, 0xFC, 0x39, 0x52, 0xCB, 0x1D, 0x1A,
		0x34, 0xAD, 0xC5, 0xA5, 0x81, 0x64, 0xBE, 0x69, 0x47, 0x23, 0xAE, 0xEA,
		0xEA, 0x4D, 0xF5, 0x4F, 0xDF, 0x6E, 0xFF, 0x09, 0x79, 0x4C, 0xCE, 0x71,
		0x36, 0x87, 0x58, 0x44, 0xD1, 0x12, 0xED, 0x23, 0x41, 0x16, 0x12, 0xF6,
	};

	// 激活码 magic（2 字节）：0x42 0x4F（"BO"）
	constexpr BYTE kMagic0 = 0x42;
	constexpr BYTE kMagic1 = 0x4F;
	// 激活码载荷格式版本（与产品版本无关，仅约束激活码格式）：
	//   <=7：旧格式，[4..7] 存"绝对到期时间戳"（秒）
	//   ==8：离线时长制（KeyGen 发码，服务器未托管），[4..7] 存"有效时长"（秒，0=永久），
	//        到期时间 = 客户首次激活时间 + 有效时长（提前生成库存码不受影响）
	//   ==9：在线时长制（面板发码，服务端托管），[4..7] 存"有效时长"（秒，0=永久）
	//   >9：未来格式，当前客户端拒绝
	constexpr BYTE kVersionMajor = 2;
	constexpr BYTE kVersionMinor = 9;      // 当前客户端支持的最高载荷版本
	constexpr BYTE kDurationMinor = 8;     // 离线时长制（KeyGen 发码，未托管）
	constexpr BYTE kOnlineMinor = 9;       // 在线时长制（面板发码，服务端托管）
	// 换机码格式（旧格式）：[4..7] 存"绝对到期时间戳"（秒）。
	// 换机/续期场景由发码工具生成，到期 = 原码剩余时间对应的日期，且新旧客户端均兼容
	constexpr BYTE kLegacyFormatMinor = 7;

	// 应用版本号（联网上报用）：与 MainApp::appVersion 保持同步（v2.8.4）
	// License 模块不依赖 MainApp 以免循环依赖，故在此单独维护
	constexpr wchar_t kAppVersion[] = L"v2.9.6";

	// 最近一次激活失败原因（供 UI 展示具体拒绝原因）
	std::wstring& lastActivateErrorStorage()
	{
		static std::wstring error;
		return error;
	}

	void setLastActivateError(std::wstring_view msg)
	{
		lastActivateErrorStorage() = msg;
	}

	void clearLastActivateError()
	{
		lastActivateErrorStorage().clear();
	}

	// ---------------------------------------------------------------------------
	// Base32 编解码（RFC 4648，去 padding）
	// ---------------------------------------------------------------------------
	constexpr char kBase32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

	std::string base32Encode(const std::vector<BYTE>& data)
	{
		std::string out;
		out.reserve((data.size() * 8 + 4) / 5);
		std::uint32_t buffer = 0;
		int bits = 0;
		for (const BYTE b : data)
		{
			buffer = (buffer << 8) | b;
			bits += 8;
			while (bits >= 5)
			{
				out.push_back(kBase32Alphabet[(buffer >> (bits - 5)) & 0x1F]);
				bits -= 5;
			}
		}
		if (bits > 0)
		{
			out.push_back(kBase32Alphabet[(buffer << (5 - bits)) & 0x1F]);
		}
		return out;
	}

	std::vector<BYTE> base32Decode(const std::string& text)
	{
		std::vector<BYTE> out;
		std::uint32_t buffer = 0;
		int bits = 0;
		for (const char c : text)
		{
			if (c == '=')
			{
				continue;
			}
			int value = -1;
			if (c >= 'A' && c <= 'Z')
			{
				value = c - 'A';
			}
			else if (c >= '2' && c <= '7')
			{
				value = c - '2' + 26;
			}
			else if (c >= 'a' && c <= 'z')
			{
				value = c - 'a';
			}
			if (value < 0)
			{
				continue;
			}
			buffer = (buffer << 5) | static_cast<std::uint32_t>(value);
			bits += 5;
			if (bits >= 8)
			{
				out.push_back(static_cast<BYTE>((buffer >> (bits - 8)) & 0xFF));
				bits -= 8;
			}
		}
		return out;
	}

	// ---------------------------------------------------------------------------
	// SHA-256 摘要
	// ---------------------------------------------------------------------------
	std::vector<BYTE> sha256(const BYTE* data, std::size_t size)
	{
		std::vector<BYTE> digest(32);
		BCRYPT_ALG_HANDLE hAlg = nullptr;
		BCRYPT_HASH_HANDLE hHash = nullptr;
		if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
		{
			// 注意：不能用 BCryptHash() —— 该 API 是 Win8/Server2012+ 新增，
			// Win7 的 bcrypt.dll 没有此导出，进程启动时直接报"无法定位程序输入点"。
			// 改用 BCryptCreateHash + BCryptHashData + BCryptFinishHash（Vista+ 可用，结果一致）。
			if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) == 0)
			{
				if (BCryptHashData(hHash, const_cast<BYTE*>(data),
				                   static_cast<ULONG>(size), 0) == 0)
				{
					BCryptFinishHash(hHash, digest.data(),
					                 static_cast<ULONG>(digest.size()), 0);
				}
				BCryptDestroyHash(hHash);
			}
			BCryptCloseAlgorithmProvider(hAlg, 0);
		}
		return digest;
	}

	// ---------------------------------------------------------------------------
	// ECDSA P-256：验签（客户端） / 签名（仅发码工具）
	// ---------------------------------------------------------------------------
	bool verifySignature(const BYTE* payload, std::size_t payloadSize,
	                     const BYTE* signature, std::size_t signatureSize)
	{
		const std::vector<BYTE> hash = sha256(payload, payloadSize);
		BCRYPT_ALG_HANDLE hAlg = nullptr;
		BCRYPT_KEY_HANDLE hKey = nullptr;
		bool ok = false;
		if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0)
		{
			if (BCryptImportKeyPair(hAlg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &hKey,
			                        const_cast<BYTE*>(kPublicKeyBlob), sizeof(kPublicKeyBlob), 0) == 0)
			{
				ok = (BCryptVerifySignature(hKey, nullptr,
				                            const_cast<BYTE*>(hash.data()), static_cast<ULONG>(hash.size()),
				                            const_cast<BYTE*>(signature), static_cast<ULONG>(signatureSize), 0) == 0);
				BCryptDestroyKey(hKey);
			}
			BCryptCloseAlgorithmProvider(hAlg, 0);
		}
		return ok;
	}

#ifdef LICENSE_GENERATOR
	bool signPayload(const BYTE* payload, std::size_t payloadSize, std::vector<BYTE>& signature)
	{
		const std::vector<BYTE> hash = sha256(payload, payloadSize);
		BCRYPT_ALG_HANDLE hAlg = nullptr;
		BCRYPT_KEY_HANDLE hKey = nullptr;
		bool ok = false;
		if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) == 0)
		{
			if (BCryptImportKeyPair(hAlg, nullptr, BCRYPT_ECCPRIVATE_BLOB, &hKey,
			                        const_cast<BYTE*>(kPrivateKeyBlob), sizeof(kPrivateKeyBlob), 0) == 0)
			{
				signature.resize(64);
				ULONG cb = 0;
				ok = (BCryptSignHash(hKey, nullptr,
				                     const_cast<BYTE*>(hash.data()), static_cast<ULONG>(hash.size()),
				                     signature.data(), static_cast<ULONG>(signature.size()), &cb, 0) == 0);
				if (ok)
				{
					signature.resize(cb);
				}
				BCryptDestroyKey(hKey);
			}
			BCryptCloseAlgorithmProvider(hAlg, 0);
		}
		return ok;
	}
#endif

	// ---------------------------------------------------------------------------
	// 本机机器指纹：SHA-256(系统盘卷序列号 | CPU 品牌) 取前 8 字节
	// ---------------------------------------------------------------------------
	std::vector<BYTE> machineFingerprintBytes()
	{
		// 1) 系统盘卷序列号
		wchar_t volumePath[MAX_PATH]{};
		DWORD sysLen = GetEnvironmentVariableW(L"SystemDrive", nullptr, 0);
		if (sysLen > 0)
		{
			std::wstring sysDrive(sysLen, L'\0');
			GetEnvironmentVariableW(L"SystemDrive", sysDrive.data(), sysLen);
			if (!sysDrive.empty())
			{
				wcsncpy_s(volumePath, sysDrive.c_str(), _TRUNCATE);
			}
		}
		DWORD volSerial = 0;
		GetVolumeInformationW(volumePath, nullptr, 0, &volSerial, nullptr, nullptr, nullptr, 0);

		// 2) CPU 品牌字符串（注册表 HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0\ProcessorNameString）
		wchar_t cpuName[256]{};
		{
			HKEY hKey = nullptr;
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
			                  L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
			                  0, KEY_READ, &hKey) == ERROR_SUCCESS)
			{
				DWORD size = sizeof(cpuName);
				RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr,
				                 reinterpret_cast<LPBYTE>(cpuName), &size);
				RegCloseKey(hKey);
			}
		}

		// 拼接原始字节
		std::vector<BYTE> raw;
		raw.reserve(sizeof(volSerial) + sizeof(cpuName));
		for (std::size_t i = 0; i < sizeof(volSerial); ++i)
		{
			raw.push_back(static_cast<BYTE>((volSerial >> (i * 8)) & 0xFF));
		}
		const BYTE* cpuBytes = reinterpret_cast<const BYTE*>(cpuName);
		raw.insert(raw.end(), cpuBytes, cpuBytes + sizeof(cpuName));

		const std::vector<BYTE> digest = sha256(raw.data(), raw.size());
		std::vector<BYTE> fp(digest.begin(), digest.begin() + 8);
		return fp;
	}

	std::wstring toHex(const BYTE* data, std::size_t size)
	{
		std::wstring out;
		out.reserve(size * 2);
		constexpr wchar_t hex[] = L"0123456789abcdef";
		for (std::size_t i = 0; i < size; ++i)
		{
			out.push_back(hex[(data[i] >> 4) & 0x0F]);
			out.push_back(hex[data[i] & 0x0F]);
		}
		return out;
	}

	// ---------------------------------------------------------------------------
	// 激活状态持久化：HKCU\Software\2Box\License = 激活码文本
	// ---------------------------------------------------------------------------
	constexpr wchar_t kRegSubKey[] = L"Software\\2Box";
	constexpr wchar_t kRegValue[] = L"License";

	std::wstring loadStoredCode()
	{
		HKEY hKey = nullptr;
		wchar_t buf[1024]{};
		DWORD size = sizeof(buf);
		std::wstring code;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			if (RegQueryValueExW(hKey, kRegValue, nullptr, nullptr,
			                     reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS)
			{
				code = buf;
			}
			RegCloseKey(hKey);
		}
		return code;
	}

	void storeCode(const std::wstring& code)
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExW(hKey, kRegValue, 0, REG_SZ,
			               reinterpret_cast<const BYTE*>(code.c_str()),
			               static_cast<DWORD>((code.size() + 1) * sizeof(wchar_t)));
			RegCloseKey(hKey);
		}
	}

	void clearStoredCode()
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
		{
			RegDeleteValueW(hKey, kRegValue);
			RegCloseKey(hKey);
		}
	}

	// ---------------------------------------------------------------------------
	// 解绑次数持久化：HKCU\Software\2Box\UnbindMonth(REG_SZ "yyyyMM") + UnbindCount(REG_DWORD)
	// 跨月自动重置：读取时发现月份与当前不一致，计数按 0 处理
	// ---------------------------------------------------------------------------
	constexpr wchar_t kRegUnbindMonth[] = L"UnbindMonth";
	constexpr wchar_t kRegUnbindCount[] = L"UnbindCount";
	constexpr wchar_t kRegBoundFp[] = L"BoundFp";  // 首次激活时绑定的机器指纹（十六进制字符串）
	constexpr wchar_t kRegActivatedAt[] = L"ActivatedAt";  // 时长制：首次激活时间戳（REG_QWORD，Unix 秒）
	constexpr wchar_t kRegActivatedCode[] = L"ActivatedCode";  // 与 ActivatedAt 配套：该时间对应的激活码身份（判断是否同一码）

	// 加载本地绑定的机器指纹
	std::wstring loadBoundFp()
	{
		HKEY hKey = nullptr;
		std::wstring result;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			DWORD size = 0;
			if (RegQueryValueExW(hKey, kRegBoundFp, nullptr, nullptr, nullptr, &size) == ERROR_SUCCESS && size > 0)
			{
				result.resize(size / sizeof(wchar_t));
				RegQueryValueExW(hKey, kRegBoundFp, nullptr, nullptr,
				                 reinterpret_cast<LPBYTE>(result.data()), &size);
				if (!result.empty() && result.back() == L'\0')
				{
					result.pop_back();
				}
			}
			RegCloseKey(hKey);
		}
		return result;
	}

	void storeBoundFp(const std::wstring& fp)
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			const DWORD bytes = static_cast<DWORD>((fp.size() + 1) * sizeof(wchar_t));
			RegSetValueExW(hKey, kRegBoundFp, 0, REG_SZ, reinterpret_cast<const BYTE*>(fp.c_str()), bytes);
			RegCloseKey(hKey);
		}
	}

	void clearBoundFp()
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
		{
			RegDeleteValueW(hKey, kRegBoundFp);
			RegCloseKey(hKey);
		}
	}

	// 时长制：首次激活时间戳（Unix 秒）。同一激活码重复激活不刷新（以首次激活计时）；更换新激活码才重新起算
	std::int64_t loadActivatedAt()
	{
		HKEY hKey = nullptr;
		std::int64_t result = 0;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			DWORD type = 0;
			DWORD cb = sizeof(result);
			if (RegQueryValueExW(hKey, kRegActivatedAt, nullptr, &type,
			                     reinterpret_cast<LPBYTE>(&result), &cb) != ERROR_SUCCESS || type != REG_QWORD)
			{
				result = 0;
			}
			RegCloseKey(hKey);
		}
		return result;
	}

	void storeActivatedAt(std::int64_t activatedAt)
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExW(hKey, kRegActivatedAt, 0, REG_QWORD,
			               reinterpret_cast<const BYTE*>(&activatedAt), sizeof(activatedAt));
			RegCloseKey(hKey);
		}
	}

	void clearActivatedAt()
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
		{
			RegDeleteValueW(hKey, kRegActivatedAt);
			RegCloseKey(hKey);
		}
	}

	// 加载激活码身份（与 ActivatedAt 配套）
	std::wstring loadActivatedCode()
	{
		HKEY hKey = nullptr;
		wchar_t buf[1024]{};
		DWORD size = sizeof(buf);
		std::wstring id;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			if (RegQueryValueExW(hKey, kRegActivatedCode, nullptr, nullptr,
			                     reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS)
			{
				id = buf;
			}
			RegCloseKey(hKey);
		}
		return id;
	}

	void storeActivatedCode(const std::wstring& id)
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExW(hKey, kRegActivatedCode, 0, REG_SZ,
			               reinterpret_cast<const BYTE*>(id.c_str()),
			               static_cast<DWORD>((id.size() + 1) * sizeof(wchar_t)));
			RegCloseKey(hKey);
		}
	}

	void clearActivatedCode()
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
		{
			RegDeleteValueW(hKey, kRegActivatedCode);
			RegCloseKey(hKey);
		}
	}

	// 激活码身份：规范化文本（去空格/前缀/分隔符，转大写），用于判断是否同一激活码
	std::wstring activationCodeId(const std::wstring& code)
	{
		std::wstring upper;
		upper.reserve(code.size());
		for (const wchar_t c : code)
		{
			if (c == L' ' || c == L'-')
			{
				continue;
			}
			if (c >= L'a' && c <= L'z')
			{
				upper.push_back(static_cast<wchar_t>(c - L'a' + L'A'));
			}
			else if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))
			{
				upper.push_back(c);
			}
		}
		constexpr std::wstring_view kPrefix2Box{L"2BOX"};
		constexpr std::wstring_view kPrefixEBox{L"EBOX"};
		if (upper.rfind(kPrefix2Box, 0) == 0)
		{
			upper.erase(0, kPrefix2Box.size());
		}
		else if (upper.rfind(kPrefixEBox, 0) == 0)
		{
			upper.erase(0, kPrefixEBox.size());
		}
		return upper;
	}

	std::wstring currentMonthKey()
	{
		const std::time_t now = std::time(nullptr);
		std::tm tm{};
		localtime_s(&tm, &now);
		return std::format(L"{:04d}{:02d}", tm.tm_year + 1900, tm.tm_mon + 1);
	}

	int loadUnbindCount(); // 前向声明：incrementUnbindCount 需要读取当前次数后递增

	void incrementUnbindCount()
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			const std::wstring month = currentMonthKey();
			RegSetValueExW(hKey, kRegUnbindMonth, 0, REG_SZ,
			               reinterpret_cast<const BYTE*>(month.c_str()),
			               static_cast<DWORD>((month.size() + 1) * sizeof(wchar_t)));
			DWORD count = static_cast<DWORD>(loadUnbindCount()) + 1;
			RegSetValueExW(hKey, kRegUnbindCount, 0, REG_DWORD,
			               reinterpret_cast<const BYTE*>(&count), sizeof(count));
			RegCloseKey(hKey);
		}
	}

	int loadUnbindCount()
	{
		HKEY hKey = nullptr;
		wchar_t month[16]{};
		DWORD monthSize = sizeof(month);
		DWORD count = 0;
		if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
		{
			if (RegQueryValueExW(hKey, kRegUnbindMonth, nullptr, nullptr,
			                     reinterpret_cast<LPBYTE>(month), &monthSize) == ERROR_SUCCESS)
			{
				DWORD cbCount = sizeof(count);
				if (RegQueryValueExW(hKey, kRegUnbindCount, nullptr, nullptr,
				                     reinterpret_cast<LPBYTE>(&count), &cbCount) != ERROR_SUCCESS)
				{
					count = 0;
				}
				// 跨月自动重置
				if (month != currentMonthKey())
				{
					count = 0;
				}
			}
			RegCloseKey(hKey);
		}
		return static_cast<int>(count);
	}

	// 统计除当前进程外的 2Box.exe 实例数（解绑前必须关闭其他进程）
	int countOtherBoxProcesses()
	{
		int count = 0;
		HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (hSnap == INVALID_HANDLE_VALUE)
		{
			return count;
		}
		const DWORD selfPid = GetCurrentProcessId();
		PROCESSENTRY32W pe{sizeof(pe)};
		if (Process32FirstW(hSnap, &pe))
		{
			do
			{
				if (pe.th32ProcessID == selfPid)
				{
					continue;
				}
				if (_wcsicmp(pe.szExeFile, L"2Box.exe") == 0)
				{
					++count;
				}
			} while (Process32NextW(hSnap, &pe));
		}
		CloseHandle(hSnap);
		return count;
	}

	// ---------------------------------------------------------------------------
	// 激活码解析与校验
	// 载荷布局（18 字节）：
	//   [0]=kMagic0 [1]=kMagic1 [2]=verMajor [3]=verMinor（载荷格式版本）
	//   [4..7]=(LE, 秒) —— verMinor<=7 旧格式：绝对到期时间戳
	//                      verMinor==8 时长制：有效时长（0=永久）
	//   [8]=指纹标志(1=绑定本机 0=不绑定)
	//   [9..16]=机器指纹前 8 字节（不绑定填 0）
	//   [17]=每自然月最大解绑次数（0=禁止解绑，0xFF=不限，其他=次数）
	// 二进制 = 载荷(18) + 签名(64) = 82 字节
	// ---------------------------------------------------------------------------
	struct ParsedLicense
	{
		bool valid{false};
		std::int64_t expire{0};         // 旧格式：绝对到期时间戳
		std::int64_t durationSec{0};    // 时长制：有效时长（秒），0=永久
		bool isDurationFormat{false};   // 是否时长制（verMinor==8 离线 / ==9 在线）
		bool onlineManaged{false};      // 是否在线托管码（verMinor==9，面板生成、服务端登记）
		bool bound{false};
		int unbindMax{0};             // -1=不限
		std::vector<BYTE> fp;
	};

	ParsedLicense parseAndVerifyCode(const std::wstring& code)
	{
		ParsedLicense result;

		// 1) 文本规范化：去空格、去前缀 2BOX-/EBOX- 与分隔符 '-'，仅保留 A-Z 0-9
		std::wstring upper;
		for (const wchar_t c : code)
		{
			if (c == L' ' || c == L'-')
			{
				continue;
			}
			if (c >= L'a' && c <= L'z')
			{
				upper.push_back(static_cast<wchar_t>(c - L'a' + L'A'));
			}
			else if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))
			{
				upper.push_back(c);
			}
		}
		// 兼容旧前缀 2BOX 与新前缀 EBOX（4 字符）
		constexpr std::wstring_view kPrefix2Box{L"2BOX"};
		constexpr std::wstring_view kPrefixEBox{L"EBOX"};
		if (upper.rfind(kPrefix2Box, 0) == 0)
		{
			upper.erase(0, kPrefix2Box.size());
		}
		else if (upper.rfind(kPrefixEBox, 0) == 0)
		{
			upper.erase(0, kPrefixEBox.size());
		}
		std::string ascii;
		for (const wchar_t c : upper)
		{
			if (c <= 0x7F)
			{
				ascii.push_back(static_cast<char>(c));
			}
			else
			{
				return result;
			}
		}

		const std::vector<BYTE> raw = base32Decode(ascii);
		if (raw.size() != 82)
		{
			return result;
		}
		const BYTE* payload = raw.data();
		const BYTE* signature = raw.data() + 18;

		// 2) magic 校验
		if (payload[0] != kMagic0 || payload[1] != kMagic1)
		{
			return result;
		}
		// 3) 版本校验（主版本必须一致，次版本必须 <= 当前）
		if (payload[2] != kVersionMajor || payload[3] > kVersionMinor)
		{
			return result;
		}
		// 4) 签名校验
		if (!verifySignature(payload, 18, signature, 64))
		{
			return result;
		}

		// 5) 解析载荷：verMinor==8/9 为时长制（[4..7]=有效时长秒数，0=永久），
		//    verMinor<=7 为旧格式（[4..7]=绝对到期时间戳）；
		//    verMinor==9 为在线托管码（面板生成、服务端登记），==8 为离线码（KeyGen 发码、未托管）
		std::int64_t field = 0;
		for (int i = 0; i < 4; ++i)
		{
			field |= static_cast<std::int64_t>(payload[4 + i]) << (i * 8);
		}
		const bool isDurationFormat = (payload[3] == kDurationMinor || payload[3] == kOnlineMinor);
		const bool onlineManaged = (payload[3] == kOnlineMinor);
		const bool bound = payload[8] != 0;
		std::vector<BYTE> fp(payload + 9, payload + 17);
		int unbindMax = payload[17];
		if (unbindMax == 0xFF)
		{
			unbindMax = -1; // 不限
		}

		// 6) 绑定校验已移至 tryActivate/isActivated：绑定码首次激活时写入本机指纹到注册表，
		//    后续验证时检查注册表中的指纹是否与当前机器一致

		result.valid = true;
		result.isDurationFormat = isDurationFormat;
		result.onlineManaged = onlineManaged;
		if (isDurationFormat)
		{
			result.durationSec = field;  // 0=永久
		}
		else
		{
			result.expire = field;
		}
		result.bound = bound;
		result.unbindMax = unbindMax;
		result.fp = std::move(fp);
		return result;
	}
}

namespace biz
{
	namespace license
	{
		std::wstring machineFingerprint()
		{
			const std::vector<BYTE> fp = machineFingerprintBytes();
			return toHex(fp.data(), fp.size());
		}

		bool tryActivate(const std::wstring& code)
		{
			clearLastActivateError();
			const ParsedLicense parsed = parseAndVerifyCode(code);
			if (!parsed.valid)
			{
				setLastActivateError(L"激活码无效或签名校验失败");
				return false;
			}
			// ===== 在线校验（联网授权版）=====
			// 已登记码：作废/过期/绑定其他机器 → 拒绝
			// 未登记码（服务器未托管的离线码）：宽容放行，保持双轨兼容
			// 服务器不可达：forceOnlineActivate=true 时拒绝；否则按离线方式放行
			const std::wstring fp = machineFingerprint();
			// 应用版本号：与 MainApp::appVersion 保持同步（License 模块不依赖 MainApp 以免循环）
			const std::wstring appVersion = kAppVersion;
			const licenseserver::ActivateResult ar = licenseserver::activate(code, fp, appVersion, L"Windows");
			if (ar.revoked)
			{
				setLastActivateError(L"该激活码已被作废，请联系作者获取新激活码。");
				return false;
			}
			if (ar.expired)
			{
				setLastActivateError(L"该激活码已过期，请联系作者续期。");
				return false;
			}
			if (ar.exceeded)
			{
				setLastActivateError(L"该激活码已绑定其他电脑，请先在那台电脑上解绑。");
				return false;
			}
			if (!ar.ok)
			{
				// 服务器不可达
				licenseserver::markOnline(false);
				if (licenseserver::forceOnlineActivate())
				{
					setLastActivateError(L"当前为强制在线模式，无法连接授权服务器，请联网后重试。");
					return false;
				}
				// 未启用强制在线：按离线方式放行（双轨并存，开发机离线可用）
			}
			else
			{
				// 服务器可达且已登记：记录在线状态并写入离线可用截止（=授权真实到期，服务端下发），
				// 此后离线也能用完整个授权期限
				licenseserver::markOnline(true);
				licenseserver::storeOnlineHeartbeat(
					static_cast<std::int64_t>(std::time(nullptr)), ar.graceUntil);
			}
			// 绑定码：首次激活写入本机指纹，后续激活校验本机指纹是否一致
			if (parsed.bound)
			{
				const std::wstring storedFp = loadBoundFp();
				if (storedFp.empty())
				{
					// 首次激活：绑定当前机器
					storeBoundFp(machineFingerprint());
				}
				else if (storedFp != machineFingerprint())
				{
					// 已绑定到其他机器：激活失败
					setLastActivateError(L"该激活码已绑定其他电脑，无法在此电脑激活。");
					return false;
				}
			}
			// 时长制：记录"首次激活时间"，到期 = 激活时间 + 有效时长。
			// 同一激活码重复激活（含被作废/踢下线后重新激活）不重置起算点；
			// 更换新激活码才重新计时（新授权从激活当天起算）
			if (parsed.isDurationFormat)
			{
				const std::wstring codeId = activationCodeId(code);
				if (loadActivatedCode() != codeId)
				{
					storeActivatedAt(static_cast<std::int64_t>(std::time(nullptr)));
					storeActivatedCode(codeId);
				}
			}
			storeCode(code);
			return true;
		}

		bool isActivated()
		{
			const std::wstring code = loadStoredCode();
			if (code.empty())
			{
				return false;
			}
			const ParsedLicense parsed = parseAndVerifyCode(code);
			if (!parsed.valid)
			{
				return false;
			}
			// 绑定码：检查本机指纹是否与首次激活时一致
			if (parsed.bound)
			{
				const std::wstring storedFp = loadBoundFp();
				if (storedFp.empty())
				{
					return false;
				}
				if (storedFp != machineFingerprint())
				{
					return false;
				}
			}
			// 时长制：必须已记录激活时间（正常流程 tryActivate 会写入）
			if (parsed.isDurationFormat && loadActivatedAt() <= 0)
			{
				return false;
			}
			return true;
		}

		bool canLaunch()
		{
			// ===== 服务端锁定检查（联网授权版）=====
			// 曾通过心跳收到 revoked/expired/kicked → 直接拒绝启动
			if (licenseserver::serverLocked())
			{
				return false;
			}
			if (!isActivated())
			{
				return false;
			}
			const std::int64_t exp = expireTime();
			if (exp <= 0)
			{
				return false;
			}
			const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
			// 在线管理的授权：服务端心跳会把 graceUntil 刷新为授权真实到期时间，
			// 激活后即使长时间离线也能用完整个授权期限；已到期且未能成功心跳才拒绝。
			// 纯离线码（从未在线）不受此管控，按本地到期时间判断
			if (licenseserver::wasOnlineBefore() && licenseserver::graceUntil() > 0)
			{
				if (now < licenseserver::graceUntil())
				{
					return true;   // 授权期内（含离线），放行
				}
				return false;      // 授权已到期且未能成功心跳 → 拒绝
			}
			return exp > now;      // 永久码 exp=INT64_MAX，恒为 true
		}

		std::int64_t expireTime()
		{
			const ParsedLicense parsed = parseAndVerifyCode(loadStoredCode());
			if (!parsed.valid)
			{
				return 0;
			}
			if (!parsed.isDurationFormat)
			{
				return parsed.expire;  // 旧格式：绝对到期时间戳
			}
			if (parsed.durationSec == 0)
			{
				return std::numeric_limits<std::int64_t>::max();  // 永久码
			}
			const std::int64_t activatedAt = loadActivatedAt();
			if (activatedAt <= 0)
			{
				return 0;  // 尚未激活（异常），视为未激活
			}
			return activatedAt + parsed.durationSec;
		}

		std::wstring expireDateText()
		{
			const std::int64_t expire = expireTime();
			if (expire <= 0)
			{
				return {};
			}
			if (expire == std::numeric_limits<std::int64_t>::max())
			{
				return L"永久";
			}
			const std::time_t t = static_cast<std::time_t>(expire);
			std::tm tm{};
			localtime_s(&tm, &t);
			return std::format(L"{:04d}-{:02d}-{:02d}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
		}

		int remainingDays()
		{
			// 剩余天数（向上取整）：到期提醒用。未激活/无到期信息返回 -1；永久码返回 -1
			if (!isActivated())
			{
				return -1;
			}
			const std::int64_t exp = expireTime();
			if (exp <= 0 || exp == std::numeric_limits<std::int64_t>::max())
			{
				return -1;
			}
			const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
			if (exp <= now)
			{
				return 0; // 已到期
			}
			return static_cast<int>((exp - now + 86399) / 86400);
		}

		void deactivate()
		{
			clearStoredCode();
			clearActivatedAt();
			clearActivatedCode();
		}

		std::wstring currentActivationCode()
		{
			return loadStoredCode();
		}

		bool isBound()
		{
			const ParsedLicense parsed = parseAndVerifyCode(loadStoredCode());
			return parsed.valid && parsed.bound;
		}

		int unbindMaxPerMonth()
		{
			const ParsedLicense parsed = parseAndVerifyCode(loadStoredCode());
			return parsed.valid ? parsed.unbindMax : 0;
		}

		int unbindCountThisMonth()
		{
			return loadUnbindCount();
		}

		UnbindResult unbind()
		{
			return unbindForSwitch().result;
		}

		UnbindOutcome unbindForSwitch()
		{
			const std::wstring code = loadStoredCode();
			if (code.empty())
			{
				return {UnbindResult::NotActivated, {}, {}};
			}
			const ParsedLicense parsed = parseAndVerifyCode(code);
			if (!parsed.valid)
			{
				return {UnbindResult::NotActivated, {}, {}};
			}
			if (!parsed.bound)
			{
				return {UnbindResult::NotBound, {}, {}};
			}
			// 有其他 2Box 进程在运行：必须全部关闭才能解绑
			if (countOtherBoxProcesses() > 0)
			{
				return {UnbindResult::OtherInstancesRunning, {}, {}};
			}
			// ===== 服务端解绑换机（联网授权版）=====
			// 已登记码：服务端校验月度次数并签发换机码（继承剩余时长），换机码自动复制给用户
			// 未登记码/服务器不可达：回退本地解绑（双轨并存）
			const licenseserver::UnbindResult sr = licenseserver::unbind(code, machineFingerprint(), kAppVersion);
			if (sr.exceed)
			{
				return {UnbindResult::ExceededLimit, {}, L"本月解绑次数已达上限，请下月再试。"};
			}
			if (sr.ok && !sr.offlineCode && !sr.newCode.empty())
			{
				// 服务端签发换机码成功：记录本次解绑并清空本机（换机码在新机激活）
				incrementUnbindCount();
				clearStoredCode();
				clearBoundFp();
				clearActivatedAt();
				clearActivatedCode();
				licenseserver::storeServerStatus({});
				return {UnbindResult::Success, sr.newCode, L"解绑成功！已生成换机激活码，请复制并在新电脑激活。"};
			}
			// 未登记码（offlineCode）或服务器不可达：走本地解绑（不影响纯离线用户）
			if (parsed.unbindMax != -1)
			{
				if (parsed.unbindMax == 0 || loadUnbindCount() >= parsed.unbindMax)
				{
					return {UnbindResult::ExceededLimit, {}, L"本月解绑次数已达上限，请下月再试。"};
				}
			}
			incrementUnbindCount();
			clearStoredCode();
			clearBoundFp();
			clearActivatedAt();
			clearActivatedCode();
			return {UnbindResult::Success, {}, L"解绑成功！本机已退出授权。"};
		}

		std::wstring lastActivateError()
		{
			return lastActivateErrorStorage();
		}

		std::wstring onlineStatusText()
		{
			if (licenseserver::serverLocked())
			{
				const std::wstring st = licenseserver::serverStatus();
				if (st == L"revoked")
				{
					return L"已作废（服务端锁定）";
				}
				if (st == L"expired")
				{
					return L"已过期（服务端锁定）";
				}
				if (st == L"kicked")
				{
					return L"已被强制下线";
				}
				return L"已锁定";
			}
			if (!licenseserver::wasOnlineBefore())
			{
				return L"离线授权（未接入服务器）";
			}
			const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
			// 最近一次激活/心跳成功连通服务器 → 显示"在线授权"
			if (licenseserver::onlineNow())
			{
				return L"在线授权";
			}
			// 离线可用截止 = 服务端下发的授权真实到期；从未成功心跳过则按本地到期时间
			std::int64_t deadline = licenseserver::graceUntil();
			if (deadline <= 0)
			{
				deadline = expireTime();
			}
			if (deadline >= std::numeric_limits<std::int64_t>::max() - 86400)
			{
				return L"离线授权（永久）";
			}
			if (now < deadline)
			{
				const std::int64_t days = (deadline - now) / 86400 + 1;
				return std::format(L"离线授权 · 剩余 {} 天", days);
			}
			return L"已到期（离线使用结束）";
		}

		VerifyResult verifyCode(const std::wstring& code)
		{
			const ParsedLicense parsed = parseAndVerifyCode(code);
			VerifyResult result;
			if (!parsed.valid)
			{
				return result;
			}
			result.valid = true;
			result.isDurationFormat = parsed.isDurationFormat;
			result.onlineManaged = parsed.onlineManaged;
			result.durationSec = parsed.durationSec;
			result.expire = parsed.isDurationFormat ? 0 : parsed.expire;
			result.bound = parsed.bound;
			result.unbindMax = parsed.unbindMax;
			return result;
		}

#ifdef LICENSE_GENERATOR
		std::wstring generateActivationCode(std::int64_t field, bool bound,
		                                    const std::wstring& /*machineCode*/,
		                                    int unbindMaxPerMonth, bool durationFormat)
		{
			// 构造载荷（18 字节）
			std::vector<BYTE> payload(18, 0);
			payload[0] = kMagic0;
			payload[1] = kMagic1;
			payload[2] = kVersionMajor;
			// durationFormat=true（库存码/离线码）：离线时长制格式 kDurationMinor==8，
			// [4..7]=有效时长（秒），自客户首次激活起算；完整期限，未托管到服务器
			// durationFormat=false（换机码）：旧格式，[4..7]=绝对到期时间戳，换机时接续剩余时间
			payload[3] = durationFormat ? kDurationMinor : kLegacyFormatMinor;
			for (int i = 0; i < 4; ++i)
			{
				payload[4 + i] = static_cast<BYTE>((field >> (i * 8)) & 0xFF);
			}
			payload[8] = bound ? 1 : 0;
			// 绑定码不写入具体指纹：客户首次激活时自动绑定本机
			// payload[9..16] 保持 0
			// 每自然月最大解绑次数：-1=不限(0xFF)，0=禁止，1..255=次数；非绑定码无解绑概念，写 0
			if (bound)
			{
				if (unbindMaxPerMonth < 0)
				{
					payload[17] = 0xFF;
				}
				else
				{
					payload[17] = static_cast<BYTE>(unbindMaxPerMonth > 255 ? 255 : unbindMaxPerMonth);
				}
			}

			// 签名
			std::vector<BYTE> signature;
			if (!signPayload(payload.data(), payload.size(), signature) || signature.size() != 64)
			{
				return {};
			}

			// 拼接 载荷+签名 → Base32 → 文本格式化
			std::vector<BYTE> raw = payload;
			raw.insert(raw.end(), signature.begin(), signature.end());
			const std::string b32 = base32Encode(raw);

			std::wstring text{L"EBOX"};
			for (std::size_t i = 0; i < b32.size(); ++i)
			{
				if (i % 5 == 0)
				{
					text.push_back(L'-');
				}
				text.push_back(static_cast<wchar_t>(b32[i]));
			}
			return text;
		}
#endif

		// ---------------------------------------------------------------------------
		// 后台心跳线程（联网授权版）
		//   周期：heartbeatIntervalHours() 小时（默认 6h，可注册表配置 1~24h）
		//   作用：向服务端上报运行状态；服务端返回 revoked/expired/kicked 时
		//         立即锁定本机（作废/过期/强制下线即时生效），并弹窗提示。
		//   纯离线码（未登记）心跳返回 offline，不触发锁定，双轨并存互不影响。
		// ---------------------------------------------------------------------------
		namespace
		{
			std::jthread g_heartbeatThread;

			// 与 UI.MainWindow 约定的通知消息：心跳线程收到服务端公告 → 通知 UI 刷新公告栏
			constexpr UINT WM_APP_LICENSENOTICE = WM_USER + 9529;
			// 主窗口类名（与 MainApp::appName 一致，用于跨线程定位窗口投递消息）
			constexpr wchar_t kMainWindowClassName[] = L"eBox";

			// 心跳线程体：循环上报，直到 stop 请求
			void heartbeatLoop(std::stop_token stopToken)
			{
				const std::wstring code = loadStoredCode();
				if (code.empty())
				{
					return; // 未激活，无需心跳
				}
				const std::wstring fp = machineFingerprint();
				while (!stopToken.stop_requested())
				{
					const licenseserver::HeartbeatResult hb = licenseserver::heartbeat(code, fp, kAppVersion);
					if (hb.ok)
					{
						// 服务器可达：标记实时在线，刷新在线基线（WasOnline / LastHeartbeatAt / GraceUntil / ServerStatus=ok）
						licenseserver::markOnline(true);
						licenseserver::storeOnlineHeartbeat(
							static_cast<std::int64_t>(std::time(nullptr)), hb.graceUntil);
						if (hb.status == L"revoked" || hb.status == L"expired" || hb.status == L"kicked")
						{
							// 服务端已锁定：记录锁定状态并清空本机授权，下次启动/启动环境即被拒绝
							licenseserver::storeServerStatus(hb.status);
							clearStoredCode();
							clearBoundFp();
							// 注意：保留 ActivatedAt/ActivatedCode（首次激活锚点），
							// 用户重新激活同一激活码时计时不重置；更换新码时在激活流程自动重写
							std::wstring msg;
							if (hb.status == L"revoked")
							{
								msg = L"该激活码已被作者作废，本机授权已失效。\n请联系作者获取新激活码。";
							}
							else if (hb.status == L"expired")
							{
								msg = L"该激活码已过期，本机授权已失效。\n请联系作者续期。";
							}
							else
							{
								msg = L"该激活码已被强制下线（在其他设备激活），本机授权已失效。";
							}
							// 切到 UI 线程弹窗提示
							MessageBoxW(nullptr, msg.c_str(), L"eBox 授权失效", MB_OK | MB_ICONWARNING | MB_TASKMODAL);
							return;
						}
						if (!hb.online)
						{
							// 未登记码（纯离线码）：服务器不托管，停止心跳，完全按离线方式运行
							return;
						}
						// 在线托管码：同步服务端系统公告（最新一条），变化时通知 UI 刷新公告栏
						if (hb.notice != licenseserver::currentNotice())
						{
							licenseserver::storeNotice(hb.notice);  // 空串=服务端已撤下公告
							PostMessageW(FindWindowW(kMainWindowClassName, nullptr),
							             WM_APP_LICENSENOTICE, 0, 0);
						}
					}
					else
					{
						// 服务器不可达：标记离线（按离线宽限管控，canLaunch 依据 graceUntil 判定），继续尝试
						licenseserver::markOnline(false);
					}
					// 等待下一个心跳周期（1~24h），分段睡眠以便响应停止
					const int intervalHours = licenseserver::heartbeatIntervalHours();
					const int totalSeconds = intervalHours * 3600;
					constexpr int kSleepStepSeconds = 60;
					for (int slept = 0; slept < totalSeconds && !stopToken.stop_requested(); slept += kSleepStepSeconds)
					{
						std::this_thread::sleep_for(std::chrono::seconds(kSleepStepSeconds));
					}
				}
			}
		}

		void startHeartbeatLoop()
		{
			if (g_heartbeatThread.joinable())
			{
				return; // 已在运行
			}
			g_heartbeatThread = std::jthread(heartbeatLoop);
		}

		void stopHeartbeatLoop()
		{
			if (g_heartbeatThread.joinable())
			{
				g_heartbeatThread.request_stop();
				g_heartbeatThread.join();
			}
		}

		void requestStopHeartbeat()
		{
			if (g_heartbeatThread.joinable())
			{
				g_heartbeatThread.request_stop();
			}
		}
	}
}
