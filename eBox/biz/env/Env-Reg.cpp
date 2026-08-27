module Env;

import "sys_defs.h";
#ifndef _SYS_DEFS_H_
#pragma message("Just for IntelliSense. You should not see this message!")
import "sys_defs.hpp";
#endif

import MainApp;

namespace
{
	constexpr wchar_t ENV_KEY_NAME[] = L"Env";
	constexpr wchar_t INDEX_PROP_NAME[] = L"Index";
	constexpr wchar_t FLAG_PROP_NAME[] = L"Flag";
	constexpr wchar_t NAME_PROP_NAME[] = L"Name";
	constexpr wchar_t APP_PATH_PROP_NAME[] = L"AppPath";
	constexpr wchar_t PROC_ENV_KEY_NAME[] = L"ProcEnv";

	// 注册表值名不能包含路径分隔符，因此把完整路径 hash 成十六进制作为值名
	std::wstring hash_proc_path_to_value_name(std::wstring_view procFullPath)
	{
		std::uint64_t hash = 0xcbf29ce484222325ull;
		for (wchar_t ch : procFullPath)
		{
			hash ^= static_cast<std::uint64_t>(ch);
			hash *= 0x100000001b3ull;
		}
		return std::format(L"{:016X}", hash);
	}

	struct EnvProperty
	{
		std::uint32_t index{0};
		std::uint64_t flag{0};
		std::wstring name;
		std::wstring appPath;
	};

	EnvProperty get_env_property(HKEY hRootEnvKey, std::wstring_view subKeyName)
	{
		EnvProperty result;
		DWORD dwType = REG_DWORD;
		DWORD dwSize = sizeof(result.index);
		LSTATUS status = RegGetValueW(hRootEnvKey, subKeyName.data(), INDEX_PROP_NAME, RRF_RT_REG_DWORD, &dwType, &result.index, &dwSize);
		if (status != ERROR_SUCCESS)
		{
			throw std::runtime_error(std::format("Failed to get index, status:{}", status));
		}
		dwType = REG_QWORD;
		dwSize = sizeof(result.flag);
		status = RegGetValueW(hRootEnvKey, subKeyName.data(), FLAG_PROP_NAME, RRF_RT_REG_QWORD, &dwType, &result.flag, &dwSize);
		if (status != ERROR_SUCCESS)
		{
			throw std::runtime_error(std::format("Failed to get flag, status:{}", status));
		}
		dwType = REG_SZ;
		dwSize = 0;
		status = RegGetValueW(hRootEnvKey, subKeyName.data(), NAME_PROP_NAME, RRF_RT_REG_SZ, &dwType, nullptr, &dwSize);
		if (status != ERROR_SUCCESS)
		{
			throw std::runtime_error(std::format("Failed to get name length, status:{}", status));
		}
		result.name.resize(dwSize / sizeof(wchar_t));
		status = RegGetValueW(hRootEnvKey, subKeyName.data(), NAME_PROP_NAME, RRF_RT_REG_SZ, &dwType, result.name.data(), &dwSize);
		if (status != ERROR_SUCCESS)
		{
			throw std::runtime_error(std::format("Failed to get name, status:{}", status));
		}
		// 关键修复：RegGetValueW 返回的 dwSize 含 REG_SZ 结尾的 null 终止符，
		// resize 会把 \0 带进 wstring。若直接用于复制，剪贴板读到第一个 \0 就截断，
		// 导致只复制出第一行。这里剥离所有尾部 \0（旧数据可能已累积多个）。
		while (!result.name.empty() && result.name.back() == L'\0')
		{
			result.name.pop_back();
		}

		// AppPath 是可选的（旧环境没有），缺失时保持空
		dwType = REG_SZ;
		dwSize = 0;
		status = RegGetValueW(hRootEnvKey, subKeyName.data(), APP_PATH_PROP_NAME, RRF_RT_REG_SZ, &dwType, nullptr, &dwSize);
		if (status == ERROR_SUCCESS && dwSize > 0)
		{
			result.appPath.resize(dwSize / sizeof(wchar_t));
			status = RegGetValueW(hRootEnvKey, subKeyName.data(), APP_PATH_PROP_NAME, RRF_RT_REG_SZ, &dwType, result.appPath.data(), &dwSize);
			if (status != ERROR_SUCCESS)
			{
				result.appPath.clear();
			}
			while (!result.appPath.empty() && result.appPath.back() == L'\0')
			{
				result.appPath.pop_back();
			}
		}
		return result;
	}

	void set_env_property_to_reg(const biz::Env* env, HKEY hEnvKey)
	{
		DWORD dwType = REG_DWORD;
		const std::uint32_t index = env->getIndex();
		LSTATUS status = RegSetValueExW(hEnvKey, INDEX_PROP_NAME, 0, dwType, reinterpret_cast<const BYTE*>(&index), sizeof(index));
		if (status != ERROR_SUCCESS)
		{
			throw std::runtime_error(std::format("Failed to set index, status:{}", status));
		}
		dwType = REG_QWORD;
		const std::uint64_t flag = env->getFlag();
		status = RegSetValueExW(hEnvKey, FLAG_PROP_NAME, 0, dwType, reinterpret_cast<const BYTE*>(&flag), sizeof(flag));
		if (status != ERROR_SUCCESS)
		{
			throw std::runtime_error(std::format("Failed to set flag, status:{}", status));
		}
		dwType = REG_SZ;
		std::wstring_view name = env->getName();
		status = RegSetValueExW(hEnvKey, NAME_PROP_NAME, 0, dwType,
		                        reinterpret_cast<const BYTE*>(name.data()), static_cast<DWORD>((name.size() + 1) * sizeof(wchar_t)));
		if (status != ERROR_SUCCESS)
		{
			throw std::runtime_error(std::format("Failed to set name, status:{}", status));
		}
		dwType = REG_SZ;
		std::wstring_view appPath = env->getAppPath();
		status = RegSetValueExW(hEnvKey, APP_PATH_PROP_NAME, 0, dwType,
		                        reinterpret_cast<const BYTE*>(appPath.data()), static_cast<DWORD>((appPath.size() + 1) * sizeof(wchar_t)));
		if (status != ERROR_SUCCESS)
		{
			throw std::runtime_error(std::format("Failed to set app path, status:{}", status));
		}
	}

	std::filesystem::path get_data_path()
	{
		namespace fs = std::filesystem;
		struct PathWrapper
		{
			PathWrapper()
			{
				dataPath = fs::weakly_canonical(fs::path{app().envDataRoot()} / fs::path{L"Env\\data"});
				fs::create_directories(dataPath);
			}

			fs::path dataPath;
		};
		static PathWrapper pathWrapper;
		return pathWrapper.dataPath;
	}

	const biz::RegKey& get_app_key()
	{
		namespace fs = std::filesystem;
		struct AppKeyWrapper
		{
			AppKeyWrapper()
			{
				appKey = biz::RegKey{
					[&]()-> HKEY
					{
						fs::path path = fs::weakly_canonical(get_data_path() / fs::path{MainApp::appName});
						HKEY hAppKey;
						if (LSTATUS status = RegLoadAppKeyW(path.native().c_str(), &hAppKey, KEY_ALL_ACCESS, 0, 0);
							status != ERROR_SUCCESS)
						{
							if (status == ERROR_ACCESS_DENIED)
							{
								DWORD count{0};
								GetUserNameW(nullptr, &count);
								if (count)
								{
									std::wstring name;
									name.resize(count);
									if (GetUserNameW(name.data(), &count))
									{
										name.resize(count - 1);
										path = fs::weakly_canonical(get_data_path() / fs::path{std::format(L"{}_{}", MainApp::appName, name)});
										status = RegLoadAppKeyW(path.native().c_str(), &hAppKey, KEY_ALL_ACCESS, 0, 0);
										if (status == ERROR_SUCCESS)
										{
											return hAppKey;
										}
									}
								}
							}
							throw std::runtime_error(std::format("RegLoadAppKeyW failed, error code:{}", status));
						}
						return hAppKey;
					}
				};
			}

			biz::RegKey appKey;
		};
		static const AppKeyWrapper appKeyWrapper{};
		return appKeyWrapper.appKey;
	}
}


namespace biz
{
	void initialize_env_reg(const EnvInitializeNotify& notify)
	{
		const RegKey& appKey = get_app_key();
		const RegKey rootEnvKey{
			[&]()-> HKEY
			{
				HKEY hRootEnvKey;
				LSTATUS status = RegCreateKeyExW(appKey, ENV_KEY_NAME,
				                                 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
				                                 &hRootEnvKey, nullptr);
				if (status != ERROR_SUCCESS)
				{
					throw std::runtime_error(std::format("RegCreateKeyExW failed, error code:{}", status));
				}
				return hRootEnvKey;
			}
		};

		DWORD dwSubKeyCount = 0;
		DWORD dwMaxSubKeyLength = 0;
		LSTATUS status = RegQueryInfoKeyW(rootEnvKey, nullptr, nullptr, nullptr,
		                                  &dwSubKeyCount, &dwMaxSubKeyLength,
		                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
		if (status != ERROR_SUCCESS)
		{
			return;
		}
		dwMaxSubKeyLength += 1;

		for (DWORD i = 0; i < dwSubKeyCount; i++)
		{
			std::wstring strSubKeyName(dwMaxSubKeyLength, L'\0');
			DWORD subKeyNameLength = dwMaxSubKeyLength;
			status = RegEnumKeyExW(rootEnvKey, i, strSubKeyName.data(), &subKeyNameLength,
			                       nullptr, nullptr, nullptr, nullptr);
			if (status != ERROR_SUCCESS)
			{
				continue;
			}
			strSubKeyName.resize(subKeyNameLength);
			try
			{
				auto [index, flag, name, appPath] = get_env_property(rootEnvKey, strSubKeyName);
				notify(EnvInitializeData{index, flag, strSubKeyName, name, appPath});
			}
			catch (...)
			{
			}
		}
	}

	void add_env_to_reg(std::wstring_view flagName, const Env* env)
	{
		const RegKey& appKey = get_app_key();
		const RegKey rootEnvKey{
			[&]()-> HKEY
			{
				HKEY hRootEnvKey;
				if (LSTATUS status = RegOpenKeyExW(appKey, ENV_KEY_NAME, 0, KEY_ALL_ACCESS, &hRootEnvKey);
					status != ERROR_SUCCESS)
				{
					throw std::runtime_error(std::format("RegOpenKeyExW failed, error code:{}", status));
				}
				return hRootEnvKey;
			}
		};
		const RegKey newEnvKey{
			[&]()-> HKEY
			{
				HKEY hNewEnvKey;
				LSTATUS status = RegCreateKeyExW(rootEnvKey, flagName.data(),
				                                 0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
				                                 &hNewEnvKey, nullptr);
				if (status != ERROR_SUCCESS)
				{
					throw std::runtime_error(std::format("RegCreateKeyExW failed, error code:{}", status));
				}
				return hNewEnvKey;
			}
		};

		try
		{
			set_env_property_to_reg(env, newEnvKey);
		}
		catch (const std::exception&)
		{
			RegDeleteTreeW(rootEnvKey, flagName.data());
			throw;
		}
		catch (...)
		{
			RegDeleteTreeW(rootEnvKey, flagName.data());
			throw std::runtime_error("unknown error in set_env_property_to_reg");
		}
	}

	void delete_env_from_reg(std::wstring_view flagName)
	{
		const RegKey& appKey = get_app_key();
		const RegKey rootEnvKey{
			[&]()-> HKEY
			{
				HKEY hRootEnvKey;
				if (LSTATUS status = RegOpenKeyExW(appKey, ENV_KEY_NAME, 0, KEY_ALL_ACCESS, &hRootEnvKey);
					status != ERROR_SUCCESS)
				{
					throw std::runtime_error(std::format("RegOpenKeyExW failed, error code:{}", status));
				}
				return hRootEnvKey;
			}
		};
		RegDeleteTreeW(rootEnvKey, flagName.data());
	}

	void save_env_name_to_reg(std::wstring_view flagName, std::wstring_view name)
	{
		const RegKey& appKey = get_app_key();
		const RegKey rootEnvKey{
			[&]()-> HKEY
			{
				HKEY hRootEnvKey;
				if (LSTATUS status = RegOpenKeyExW(appKey, ENV_KEY_NAME, 0, KEY_ALL_ACCESS, &hRootEnvKey);
					status != ERROR_SUCCESS)
				{
					throw std::runtime_error(std::format("RegOpenKeyExW failed, error code:{}", status));
				}
				return hRootEnvKey;
			}
		};
		const RegKey envKey{
			[&]()-> HKEY
			{
				HKEY hEnvKey;
				if (LSTATUS status = RegOpenKeyExW(rootEnvKey, flagName.data(), 0, KEY_ALL_ACCESS, &hEnvKey);
					status != ERROR_SUCCESS)
				{
					throw std::runtime_error(std::format("RegOpenKeyExW failed, error code:{}", status));
				}
				return hEnvKey;
			}
		};
		DWORD dwType = REG_SZ;
		RegSetValueExW(envKey, NAME_PROP_NAME, 0, dwType,
		               reinterpret_cast<const BYTE*>(name.data()), static_cast<DWORD>((name.size() + 1) * sizeof(wchar_t)));
	}

	void save_env_app_path_to_reg(std::wstring_view flagName, std::wstring_view appPath)
	{
		const RegKey& appKey = get_app_key();
		const RegKey rootEnvKey{
			[&]()-> HKEY
			{
				HKEY hRootEnvKey;
				if (LSTATUS status = RegOpenKeyExW(appKey, ENV_KEY_NAME, 0, KEY_ALL_ACCESS, &hRootEnvKey);
					status != ERROR_SUCCESS)
				{
					throw std::runtime_error(std::format("RegOpenKeyExW failed, error code:{}", status));
				}
				return hRootEnvKey;
			}
		};
		const RegKey envKey{
			[&]()-> HKEY
			{
				HKEY hEnvKey;
				if (LSTATUS status = RegOpenKeyExW(rootEnvKey, flagName.data(), 0, KEY_ALL_ACCESS, &hEnvKey);
					status != ERROR_SUCCESS)
				{
					throw std::runtime_error(std::format("RegOpenKeyExW failed, error code:{}", status));
				}
				return hEnvKey;
			}
		};
		DWORD dwType = REG_SZ;
		RegSetValueExW(envKey, APP_PATH_PROP_NAME, 0, dwType,
		               reinterpret_cast<const BYTE*>(appPath.data()), static_cast<DWORD>((appPath.size() + 1) * sizeof(wchar_t)));
	}

	void save_proc_last_env(std::wstring_view procFullPath, std::uint64_t envFlag)
	{
		const RegKey& appKey = get_app_key();
		const RegKey procEnvKey{
			[&]()-> HKEY
			{
				HKEY hProcEnvKey;
				if (LSTATUS status = RegCreateKeyExW(appKey, PROC_ENV_KEY_NAME,
				                                     0, nullptr, 0, KEY_ALL_ACCESS, nullptr,
				                                     &hProcEnvKey, nullptr);
					status != ERROR_SUCCESS)
				{
					throw std::runtime_error(std::format("RegCreateKeyExW failed, error code:{}", status));
				}
				return hProcEnvKey;
			}
		};
		const std::wstring valueName = hash_proc_path_to_value_name(procFullPath);
		DWORD dwType = REG_QWORD;
		RegSetValueExW(procEnvKey, valueName.c_str(), 0, dwType,
		               reinterpret_cast<const BYTE*>(&envFlag), sizeof(envFlag));
	}

	std::optional<std::uint64_t> load_proc_last_env(std::wstring_view procFullPath)
	{
		const RegKey& appKey = get_app_key();
		HKEY hProcEnvKey;
		if (LSTATUS status = RegOpenKeyExW(appKey, PROC_ENV_KEY_NAME, 0, KEY_ALL_ACCESS, &hProcEnvKey);
			status != ERROR_SUCCESS)
		{
			return std::nullopt;
		}
		const std::wstring valueName = hash_proc_path_to_value_name(procFullPath);
		std::uint64_t envFlag = 0;
		DWORD dwType = REG_QWORD;
		DWORD dwSize = sizeof(envFlag);
		LSTATUS status = RegGetValueW(hProcEnvKey, nullptr, valueName.c_str(), RRF_RT_REG_QWORD, &dwType, &envFlag, &dwSize);
		RegCloseKey(hProcEnvKey);
		if (status != ERROR_SUCCESS)
		{
			return std::nullopt;
		}
		return envFlag;
	}
}
