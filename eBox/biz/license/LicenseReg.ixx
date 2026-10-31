module;
#include <Windows.h>
export module biz.LicenseReg;

import std;

// 授权注册表根键兼容层：主键 Software\eBox + 旧键 Software\2Box 双存在。
// 规则见 docs/账号会员体系/eBox-账号会员体系-开发计划.md 3.6.1。
//
//  - 授权组（License/BoundFp/ActivatedAt/ActivatedCode/Unbind*）：以 License 为锚点选根，
//    整组同源，禁止逐值跨键混读。
//  - 服务端组（ServerUrl/ServerStatus/Notice* 等）：逐值主键优先、缺失回退旧键。
//  - 写：先写主键，再镜像写旧键（失败忽略，不影响业务结果）。
//  - 删：两端都删。
export namespace biz::licensereg
{
	enum class RootKey
	{
		Primary, // HKCU\Software\eBox
		Legacy,  // HKCU\Software\2Box
	};

	// 按 3.6.1 规则选授权组根键（含回滚保护：旧键 ActivatedAt 更新时选旧键并回补主键）
	RootKey selectAuthRoot();

	// 从指定根键读取授权组值（type 校验与旧实现一致：QWORD 仅接受 REG_QWORD）
	bool readAuthString(RootKey root, const wchar_t* name, std::wstring& out);
	bool readAuthQword(RootKey root, const wchar_t* name, std::int64_t& out);
	bool readAuthDword(RootKey root, const wchar_t* name, DWORD& out);

	// 单值便捷读取（内部选根）
	std::wstring authString(const wchar_t* name);
	std::int64_t authQword(const wchar_t* name);
	DWORD authDword(const wchar_t* name);

	// 授权组写入：主键 + 旧键镜像
	void writeAuthString(const wchar_t* name, std::wstring_view value);
	void writeAuthQword(const wchar_t* name, std::int64_t value);
	void writeAuthDword(const wchar_t* name, DWORD value);

	// 授权组删除：两端都删
	void deleteAuthValue(const wchar_t* name);

	// 服务端组：逐值主键优先、缺失回退旧键
	bool serverReadString(const wchar_t* name, std::wstring& out);
	std::int64_t serverReadQword(const wchar_t* name, std::int64_t def = 0);
	DWORD serverReadDword(const wchar_t* name, DWORD def = 0);

	void serverWriteString(const wchar_t* name, std::wstring_view value);
	void serverWriteQword(const wchar_t* name, std::int64_t value);
	void serverWriteDword(const wchar_t* name, DWORD value);

	void serverDeleteValue(const wchar_t* name);
}

namespace biz::licensereg
{
	namespace
	{
		constexpr wchar_t kPrimaryKey[] = L"Software\\eBox";
		constexpr wchar_t kLegacyKey[] = L"Software\\2Box";
		constexpr wchar_t kLicenseValue[] = L"License";
		constexpr wchar_t kActivatedAtValue[] = L"ActivatedAt";

		// 阶段 1：写入镜像旧键。阶段 2 把此开关改为 false 即可停写（读兼容保留）。
		constexpr bool kMirrorLegacyOnWrite = true;

		const wchar_t* sub_key_of(RootKey root)
		{
			return root == RootKey::Legacy ? kLegacyKey : kPrimaryKey;
		}

		HKEY open_key(const wchar_t* subKey, REGSAM sam)
		{
			HKEY hKey = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, sam, &hKey) == ERROR_SUCCESS)
			{
				return hKey;
			}
			return nullptr;
		}

		bool query_value(HKEY hKey, const wchar_t* name, DWORD& type, std::vector<BYTE>& data)
		{
			DWORD size = 0;
			LSTATUS st = RegQueryValueExW(hKey, name, nullptr, &type, nullptr, &size);
			if (st != ERROR_SUCCESS)
			{
				return false;
			}
			data.clear();
			if (size == 0)
			{
				return true;
			}
			data.resize(size);
			st = RegQueryValueExW(hKey, name, nullptr, &type, data.data(), &size);
			if (st != ERROR_SUCCESS)
			{
				return false;
			}
			data.resize(size);
			return true;
		}

		std::wstring decode_string(const std::vector<BYTE>& data)
		{
			if (data.size() < sizeof(wchar_t))
			{
				return {};
			}
			std::wstring text(data.size() / sizeof(wchar_t), L'\0');
			std::memcpy(text.data(), data.data(), text.size() * sizeof(wchar_t));
			while (!text.empty() && text.back() == L'\0')
			{
				text.pop_back();
			}
			return text;
		}

		bool has_value(const wchar_t* subKey, const wchar_t* name)
		{
			HKEY hKey = open_key(subKey, KEY_READ);
			if (!hKey)
			{
				return false;
			}
			DWORD type = 0;
			DWORD size = 0;
			const LSTATUS st = RegQueryValueExW(hKey, name, nullptr, &type, nullptr, &size);
			RegCloseKey(hKey);
			return st == ERROR_SUCCESS && size > 0;
		}

		bool read_string_raw(const wchar_t* subKey, const wchar_t* name, std::wstring& out)
		{
			HKEY hKey = open_key(subKey, KEY_READ);
			if (!hKey)
			{
				return false;
			}
			DWORD type = 0;
			std::vector<BYTE> data;
			const bool ok = query_value(hKey, name, type, data);
			RegCloseKey(hKey);
			if (!ok)
			{
				return false;
			}
			out = decode_string(data);
			return !out.empty();
		}

		bool read_qword_raw(const wchar_t* subKey, const wchar_t* name, std::int64_t& out)
		{
			HKEY hKey = open_key(subKey, KEY_READ);
			if (!hKey)
			{
				return false;
			}
			DWORD type = 0;
			std::vector<BYTE> data;
			const bool ok = query_value(hKey, name, type, data);
			RegCloseKey(hKey);
			if (!ok || type != REG_QWORD || data.size() < sizeof(std::int64_t))
			{
				return false;
			}
			std::memcpy(&out, data.data(), sizeof(std::int64_t));
			return true;
		}

		bool read_dword_raw(const wchar_t* subKey, const wchar_t* name, DWORD& out)
		{
			HKEY hKey = open_key(subKey, KEY_READ);
			if (!hKey)
			{
				return false;
			}
			DWORD type = 0;
			std::vector<BYTE> data;
			const bool ok = query_value(hKey, name, type, data);
			RegCloseKey(hKey);
			if (!ok || data.size() < sizeof(DWORD))
			{
				return false;
			}
			std::memcpy(&out, data.data(), sizeof(DWORD));
			return true;
		}

		void write_raw(const wchar_t* subKey, const wchar_t* name, DWORD type,
		               const void* data, DWORD bytes)
		{
			HKEY hKey = nullptr;
			if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr, 0, KEY_SET_VALUE,
			                    nullptr, &hKey, nullptr) != ERROR_SUCCESS)
			{
				return;
			}
			RegSetValueExW(hKey, name, 0, type, static_cast<const BYTE*>(data), bytes);
			RegCloseKey(hKey);
		}

		void delete_raw(const wchar_t* subKey, const wchar_t* name)
		{
			HKEY hKey = open_key(subKey, KEY_SET_VALUE);
			if (!hKey)
			{
				return;
			}
			RegDeleteValueW(hKey, name);
			RegCloseKey(hKey);
		}

		void write_string_mirrored(const wchar_t* name, std::wstring_view value)
		{
			const std::wstring text{value};
			const DWORD bytes = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
			write_raw(kPrimaryKey, name, REG_SZ, text.c_str(), bytes);
			if (kMirrorLegacyOnWrite)
			{
				write_raw(kLegacyKey, name, REG_SZ, text.c_str(), bytes);
			}
		}

		// 旧键值整组回补主键（回滚保护用：仅在旧键 ActivatedAt 更新时触发）
		void sync_auth_group_to_primary()
		{
			static constexpr const wchar_t* kAuthValues[] = {
				L"License", L"BoundFp", L"ActivatedAt", L"ActivatedCode",
				L"UnbindMonth", L"UnbindCount", L"UnbindKey",
			};
			for (const wchar_t* name : kAuthValues)
			{
				HKEY hKey = open_key(kLegacyKey, KEY_READ);
				if (!hKey)
				{
					return;
				}
				DWORD type = 0;
				std::vector<BYTE> data;
				const bool ok = query_value(hKey, name, type, data);
				RegCloseKey(hKey);
				if (!ok)
				{
					continue;
				}
				write_raw(kPrimaryKey, name, type, data.empty() ? nullptr : data.data(),
				          static_cast<DWORD>(data.size()));
			}
		}

		// 一次性迁移：主键无 License 且旧键有 → 把旧键中主键缺失的值整体复制到主键。
		// 只补缺失值、不覆盖已有值；失败不影响业务（读取仍有回退）。
		void migrate_legacy_values()
		{
			HKEY hKey = open_key(kLegacyKey, KEY_READ);
			if (!hKey)
			{
				return;
			}
			for (DWORD index = 0;; ++index)
			{
				wchar_t name[512]{};
				DWORD nameLength = 512;
				DWORD type = 0;
				DWORD size = 0;
				const LSTATUS st = RegEnumValueW(hKey, index, name, &nameLength, nullptr,
				                                 &type, nullptr, &size);
				if (st == ERROR_NO_MORE_ITEMS)
				{
					break;
				}
				if (st != ERROR_SUCCESS)
				{
					continue;
				}
				if (has_value(kPrimaryKey, name))
				{
					continue;
				}
				std::vector<BYTE> data(size);
				DWORD bytes = size;
				DWORD actualType = type;
				// 第一次调用把 nameLength 改成了实际字符数（不含结尾 NUL），
				// 直接复用会让第二次调用因缓冲区不足返回 ERROR_MORE_DATA，必须重置。
				nameLength = 512;
				if (RegEnumValueW(hKey, index, name, &nameLength, nullptr, &actualType,
				                  data.empty() ? nullptr : data.data(), &bytes) == ERROR_SUCCESS)
				{
					write_raw(kPrimaryKey, name, actualType, data.empty() ? nullptr : data.data(),
					          static_cast<DWORD>(data.size()));
				}
			}
			RegCloseKey(hKey);
		}

		void ensure_migrated()
		{
			// 常量初始化，无 magic static guard；exchange 先置位，避免并发重入。
			static std::atomic<bool> started{false};
			if (started.exchange(true, std::memory_order_acq_rel))
			{
				return;
			}
			if (has_value(kPrimaryKey, kLicenseValue))
			{
				return;
			}
			if (!has_value(kLegacyKey, kLicenseValue))
			{
				return;
			}
			migrate_legacy_values();
		}
	}

	RootKey selectAuthRoot()
	{
		ensure_migrated();

		if (!has_value(kPrimaryKey, kLicenseValue))
		{
			return has_value(kLegacyKey, kLicenseValue) ? RootKey::Legacy : RootKey::Primary;
		}

		// 回滚保护：升级后又运行过旧版本时，旧键 ActivatedAt 更新 → 整组改读旧键并回补主键
		std::int64_t primaryActivatedAt = 0;
		std::int64_t legacyActivatedAt = 0;
		const bool hasPrimary = read_qword_raw(kPrimaryKey, kActivatedAtValue, primaryActivatedAt);
		const bool hasLegacy = read_qword_raw(kLegacyKey, kActivatedAtValue, legacyActivatedAt);
		if (hasLegacy && legacyActivatedAt > (hasPrimary ? primaryActivatedAt : 0))
		{
			sync_auth_group_to_primary();
			return RootKey::Legacy;
		}
		return RootKey::Primary;
	}

	bool readAuthString(RootKey root, const wchar_t* name, std::wstring& out)
	{
		return read_string_raw(sub_key_of(root), name, out);
	}

	bool readAuthQword(RootKey root, const wchar_t* name, std::int64_t& out)
	{
		return read_qword_raw(sub_key_of(root), name, out);
	}

	bool readAuthDword(RootKey root, const wchar_t* name, DWORD& out)
	{
		return read_dword_raw(sub_key_of(root), name, out);
	}

	std::wstring authString(const wchar_t* name)
	{
		std::wstring out;
		readAuthString(selectAuthRoot(), name, out);
		return out;
	}

	std::int64_t authQword(const wchar_t* name)
	{
		std::int64_t out = 0;
		readAuthQword(selectAuthRoot(), name, out);
		return out;
	}

	DWORD authDword(const wchar_t* name)
	{
		DWORD out = 0;
		readAuthDword(selectAuthRoot(), name, out);
		return out;
	}

	void writeAuthString(const wchar_t* name, std::wstring_view value)
	{
		write_string_mirrored(name, value);
	}

	void writeAuthQword(const wchar_t* name, std::int64_t value)
	{
		write_raw(kPrimaryKey, name, REG_QWORD, &value, sizeof(value));
		if (kMirrorLegacyOnWrite)
		{
			write_raw(kLegacyKey, name, REG_QWORD, &value, sizeof(value));
		}
	}

	void writeAuthDword(const wchar_t* name, DWORD value)
	{
		write_raw(kPrimaryKey, name, REG_DWORD, &value, sizeof(value));
		if (kMirrorLegacyOnWrite)
		{
			write_raw(kLegacyKey, name, REG_DWORD, &value, sizeof(value));
		}
	}

	void deleteAuthValue(const wchar_t* name)
	{
		delete_raw(kPrimaryKey, name);
		delete_raw(kLegacyKey, name);
	}

	bool serverReadString(const wchar_t* name, std::wstring& out)
	{
		ensure_migrated();
		if (read_string_raw(kPrimaryKey, name, out))
		{
			return true;
		}
		std::wstring legacy;
		if (read_string_raw(kLegacyKey, name, legacy))
		{
			out = std::move(legacy);
			return true;
		}
		return false;
	}

	std::int64_t serverReadQword(const wchar_t* name, std::int64_t def)
	{
		ensure_migrated();
		std::int64_t out = 0;
		if (read_qword_raw(kPrimaryKey, name, out))
		{
			return out;
		}
		if (read_qword_raw(kLegacyKey, name, out))
		{
			return out;
		}
		return def;
	}

	DWORD serverReadDword(const wchar_t* name, DWORD def)
	{
		ensure_migrated();
		DWORD out = 0;
		if (read_dword_raw(kPrimaryKey, name, out))
		{
			return out;
		}
		if (read_dword_raw(kLegacyKey, name, out))
		{
			return out;
		}
		return def;
	}

	void serverWriteString(const wchar_t* name, std::wstring_view value)
	{
		write_string_mirrored(name, value);
	}

	void serverWriteQword(const wchar_t* name, std::int64_t value)
	{
		write_raw(kPrimaryKey, name, REG_QWORD, &value, sizeof(value));
		if (kMirrorLegacyOnWrite)
		{
			write_raw(kLegacyKey, name, REG_QWORD, &value, sizeof(value));
		}
	}

	void serverWriteDword(const wchar_t* name, DWORD value)
	{
		write_raw(kPrimaryKey, name, REG_DWORD, &value, sizeof(value));
		if (kMirrorLegacyOnWrite)
		{
			write_raw(kLegacyKey, name, REG_DWORD, &value, sizeof(value));
		}
	}

	void serverDeleteValue(const wchar_t* name)
	{
		delete_raw(kPrimaryKey, name);
		delete_raw(kLegacyKey, name);
	}
}
