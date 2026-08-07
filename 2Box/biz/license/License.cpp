module;
#include <Windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <ctime>
#pragma comment(lib, "bcrypt.lib")
module biz.License;

import std;

namespace
{
	// ---------------------------------------------------------------------------
	// 密钥材料（ECDSA P-256）
	// 公钥：BCRYPT_ECCPUBLIC_BLOB（dwMagic=BCRYPT_ECDSA_PUBLIC_P256_MAGIC, cbKey=32, X, Y）
	// 私钥：仅编译进发码工具（LICENSE_GENERATOR 宏），绝不分发给客户端
	// ---------------------------------------------------------------------------
#ifdef LICENSE_GENERATOR
	// 私钥 blob：BCRYPT_ECCPRIVATE_BLOB（dwMagic=BCRYPT_ECDSA_PRIVATE_P256_MAGIC, cbKey=32, X, Y, d）
	// ⚠ 安全提示：真实私钥字节已从公开仓库中移除（占位为 0xCC）。
	//   - 完整私钥保存在本地 License.cpp.private.bak（已加入 .gitignore，禁止推送）
	//   - KeyGen 工具编译前，请从 .private.bak 还原此处的真实字节
	//   - 严禁将真实私钥提交到任何公开仓库（会导致激活码可被伪造）
	static constexpr BYTE kPrivateKeyBlob[] = {
		0x45, 0x43, 0x53, 0x32, 0x20, 0x00, 0x00, 0x00, // BCRYPT_ECDSA_PRIVATE_P256_MAGIC + cbKey=32
		// 以下 96 字节为占位符（X[32] + Y[32] + d[32]），真实值见本地 .private.bak
		0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
		0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
		0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // X (32B)
		0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
		0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
		0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // Y (32B)
		0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
		0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
		0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // d (32B)
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
	// 当前版本号（与 MainApp::appVersion vX.Y 对应）
	constexpr BYTE kVersionMajor = 2;
	constexpr BYTE kVersionMinor = 7;

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
		if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0)
		{
			BCryptHash(hAlg, nullptr, 0,
			           const_cast<BYTE*>(data), static_cast<ULONG>(size),
			           digest.data(), static_cast<ULONG>(digest.size()));
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
	//   [0]=kMagic0 [1]=kMagic1 [2]=verMajor [3]=verMinor
	//   [4..7]=到期时间戳(LE, 秒)
	//   [8]=指纹标志(1=绑定本机 0=不绑定)
	//   [9..16]=机器指纹前 8 字节（不绑定填 0）
	//   [17]=每自然月最大解绑次数（0=禁止解绑，0xFF=不限，其他=次数）
	// 二进制 = 载荷(18) + 签名(64) = 82 字节
	// ---------------------------------------------------------------------------
	struct ParsedLicense
	{
		bool valid{false};
		std::int64_t expire{0};
		bool bound{false};
		int unbindMax{0};             // -1=不限
		std::vector<BYTE> fp;
	};

	ParsedLicense parseAndVerifyCode(const std::wstring& code)
	{
		ParsedLicense result;

		// 1) 文本规范化：去空格、去前缀 2BOX- 与分隔符 '-'，仅保留 A-Z 0-9
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
		constexpr std::wstring_view prefix{L"2BOX"};
		if (upper.rfind(prefix, 0) == 0)
		{
			upper.erase(0, prefix.size());
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

		// 5) 解析载荷
		std::int64_t expire = 0;
		for (int i = 0; i < 4; ++i)
		{
			expire |= static_cast<std::int64_t>(payload[4 + i]) << (i * 8);
		}
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
		result.expire = expire;
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
			const ParsedLicense parsed = parseAndVerifyCode(code);
			if (!parsed.valid)
			{
				return false;
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
					return false;
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
			return true;
		}

		bool canLaunch()
		{
			if (!isActivated())
			{
				return false;
			}
			const ParsedLicense parsed = parseAndVerifyCode(loadStoredCode());
			if (!parsed.valid)
			{
				return false;
			}
			const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
			return parsed.expire > now;
		}

		std::int64_t expireTime()
		{
			const ParsedLicense parsed = parseAndVerifyCode(loadStoredCode());
			return parsed.valid ? parsed.expire : 0;
		}

		std::wstring expireDateText()
		{
			const std::int64_t expire = expireTime();
			if (expire <= 0)
			{
				return {};
			}
			const std::time_t t = static_cast<std::time_t>(expire);
			std::tm tm{};
			localtime_s(&tm, &t);
			return std::format(L"{:04d}-{:02d}-{:02d}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
		}

		void deactivate()
		{
			clearStoredCode();
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
			const std::wstring code = loadStoredCode();
			if (code.empty())
			{
				return UnbindResult::NotActivated;
			}
			const ParsedLicense parsed = parseAndVerifyCode(code);
			if (!parsed.valid)
			{
				return UnbindResult::NotActivated;
			}
			if (!parsed.bound)
			{
				return UnbindResult::NotBound;
			}
			// 有其他 2Box 进程在运行：必须全部关闭才能解绑
			if (countOtherBoxProcesses() > 0)
			{
				return UnbindResult::OtherInstancesRunning;
			}
			// 次数上限：0=禁止解绑，-1=不限
			if (parsed.unbindMax != -1)
			{
				if (parsed.unbindMax == 0 || loadUnbindCount() >= parsed.unbindMax)
				{
					return UnbindResult::ExceededLimit;
				}
			}
			// 记录本次解绑（跨月自动重置）
			incrementUnbindCount();
			// 清除本机激活状态和绑定指纹
			clearStoredCode();
			clearBoundFp();
			return UnbindResult::Success;
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
			result.expire = parsed.expire;
			result.bound = parsed.bound;
			result.unbindMax = parsed.unbindMax;
			return result;
		}

#ifdef LICENSE_GENERATOR
		std::wstring generateActivationCode(std::int64_t expireUnix, bool bound,
		                                    const std::wstring& /*machineCode*/,
		                                    int unbindMaxPerMonth)
		{
			// 构造载荷（18 字节）
			std::vector<BYTE> payload(18, 0);
			payload[0] = kMagic0;
			payload[1] = kMagic1;
			payload[2] = kVersionMajor;
			payload[3] = kVersionMinor;
			for (int i = 0; i < 4; ++i)
			{
				payload[4 + i] = static_cast<BYTE>((expireUnix >> (i * 8)) & 0xFF);
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

			std::wstring text{L"2BOX"};
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
	}
}
