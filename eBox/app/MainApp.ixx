export module MainApp;

import std;
import "sys_defs.h";
import Scheduler;

export int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow);
export class MainApp;

export struct AppCommonTextFormat
{
	UniqueComPtr<IDWriteTextFormat> pTitleFormat;
	UniqueComPtr<IDWriteTextFormat> pMainFormat;
	UniqueComPtr<IDWriteTextFormat> pErrorMsgFormat;
	UniqueComPtr<IDWriteTextFormat> pTipsFormat;
	UniqueComPtr<IDWriteTextFormat> pToolBtnFormat;
	// 加粗正文（14px 半粗），用于标题行内需要强调的文本（如当前环境名称）
	UniqueComPtr<IDWriteTextFormat> pBoldFormat;

	UniqueComPtr<IDWriteInlineObject> pTitleEllipsisTrimmingSign;
	UniqueComPtr<IDWriteInlineObject> pMainEllipsisTrimmingSign;
	UniqueComPtr<IDWriteInlineObject> pErrorMsgEllipsisTrimmingSign;
	UniqueComPtr<IDWriteInlineObject> pTipsEllipsisTrimmingSign;
	UniqueComPtr<IDWriteInlineObject> pToolBtnEllipsisTrimmingSign;
	UniqueComPtr<IDWriteInlineObject> pBoldEllipsisTrimmingSign;

	void setAllTextEllipsisTrimming() const
	{
		setTitleEllipsisTrimming();
		setMainEllipsisTrimming();
		setErrorMsgEllipsisTrimming();
		setTipsEllipsisTrimming();
		setToolBtnEllipsisTrimming();
		setBoldEllipsisTrimming();
	}

	void clearAllTextEllipsisTrimming() const
	{
		clearTitleTrimming();
		clearMainEllipsisTrimming();
		clearErrorMsgTrimming();
		clearTipsEllipsisTrimming();
		clearToolBtnEllipsisTrimming();
		clearBoldTrimming();
	}

	void setTitleEllipsisTrimming() const
	{
		pTitleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER};
		pTitleFormat->SetTrimming(&trimming, pTitleEllipsisTrimmingSign);
	}

	void clearTitleTrimming() const
	{
		pTitleFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_NONE};
		pTitleFormat->SetTrimming(&trimming, nullptr);
	}

	void setMainEllipsisTrimming() const
	{
		pMainFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER};
		pMainFormat->SetTrimming(&trimming, pMainEllipsisTrimmingSign);
	}

	void clearMainEllipsisTrimming() const
	{
		pMainFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_NONE};
		pMainFormat->SetTrimming(&trimming, nullptr);
	}

	void setErrorMsgEllipsisTrimming() const
	{
		pErrorMsgFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER};
		pErrorMsgFormat->SetTrimming(&trimming, pErrorMsgEllipsisTrimmingSign);
	}

	void clearErrorMsgTrimming() const
	{
		pErrorMsgFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_NONE};
		pErrorMsgFormat->SetTrimming(&trimming, nullptr);
	}

	void setTipsEllipsisTrimming() const
	{
		pTipsFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER};
		pTipsFormat->SetTrimming(&trimming, pTipsEllipsisTrimmingSign);
	}

	void clearTipsEllipsisTrimming() const
	{
		pTipsFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_NONE};
		pTipsFormat->SetTrimming(&trimming, nullptr);
	}

	void setToolBtnEllipsisTrimming() const
	{
		pToolBtnFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER};
		pToolBtnFormat->SetTrimming(&trimming, pToolBtnEllipsisTrimmingSign);
	}

	void clearToolBtnEllipsisTrimming() const
	{
		pToolBtnFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_NONE};
		pToolBtnFormat->SetTrimming(&trimming, nullptr);
	}

	void setBoldEllipsisTrimming() const
	{
		pBoldFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER};
		pBoldFormat->SetTrimming(&trimming, pBoldEllipsisTrimmingSign);
	}

	void clearBoldTrimming() const
	{
		pBoldFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
		constexpr DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_NONE};
		pBoldFormat->SetTrimming(&trimming, nullptr);
	}

private:
	friend class MainApp;

	static void createCommonTextAbout(IDWriteFactory* pDWriteFactory, DWRITE_FONT_WEIGHT weight, float fontSize, IDWriteTextFormat** ppTextFormat, IDWriteInlineObject** ppEllipsisTrimmingSign)
	{
		HRESULT hr = pDWriteFactory->CreateTextFormat(
			L"Segoe UI",
			nullptr,
			weight,
			DWRITE_FONT_STYLE_NORMAL,
			DWRITE_FONT_STRETCH_NORMAL,
			fontSize,
			L"",
			ppTextFormat);
		if (FAILED(hr))
		{
			throw std::runtime_error(std::format("CreateTextFormat fail, HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)));
		}

		hr = pDWriteFactory->CreateEllipsisTrimmingSign(
			*ppTextFormat,
			ppEllipsisTrimmingSign
		);
		if (FAILED(hr))
		{
			throw std::runtime_error(std::format("CreateEllipsisTrimmingSign fail, HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)));
		}
	}

	void createAllCommonTextFormat(IDWriteFactory* pDWriteFactory)
	{
		createCommonTextAbout(pDWriteFactory, DWRITE_FONT_WEIGHT_SEMI_BOLD, 16.f, &pTitleFormat, &pTitleEllipsisTrimmingSign);
		createCommonTextAbout(pDWriteFactory, DWRITE_FONT_WEIGHT_NORMAL, 14.f, &pMainFormat, &pMainEllipsisTrimmingSign);
		createCommonTextAbout(pDWriteFactory, DWRITE_FONT_WEIGHT_NORMAL, 13.f, &pErrorMsgFormat, &pErrorMsgEllipsisTrimmingSign);
		createCommonTextAbout(pDWriteFactory, DWRITE_FONT_WEIGHT_NORMAL, 12.f, &pTipsFormat, &pTipsEllipsisTrimmingSign);
		createCommonTextAbout(pDWriteFactory, DWRITE_FONT_WEIGHT_NORMAL, 11.f, &pToolBtnFormat, &pToolBtnEllipsisTrimmingSign);
		createCommonTextAbout(pDWriteFactory, DWRITE_FONT_WEIGHT_SEMI_BOLD, 14.f, &pBoldFormat, &pBoldEllipsisTrimmingSign);
	}
};

class MainApp
{
public:
	// ===== 版本号（单一可信源，.rc 与 License.cpp 均引用此处）=====
	static constexpr int kVerMajor = 2;
	static constexpr int kVerMinor = 8;
	static constexpr int kVerPatch = 6;
	static constexpr int kVerCode  = 20806;  // 单调递增数字，用于版本比较

	static constexpr std::wstring_view appName{L"eBox"};
	static constexpr std::string_view appNameA{"eBox"};
	static constexpr std::wstring_view appVersion{L"v2.8.6"};
	static constexpr std::wstring_view appUpdateDate{L"2026/8/11"};

	// ===== 自动升级配置 =====
	// manifest 由 jsDelivr CDN 加速 GitHub 仓库文件，客户端追加时间戳破除缓存
	// 发布新版本后访问 https://purge.jsdelivr.net/gh/shushuhao01/ebox@main/dist/update.json 刷新
	static constexpr std::wstring_view kUpdateManifestUrl{
		L"https://cdn.jsdelivr.net/gh/shushuhao01/ebox@main/dist/update.json"
	};
	static constexpr std::wstring_view kUpdateUserAgent{L"eBox-Updater/2.8"};

	// 授权相关外链（激活弹窗 / 授权信息弹窗的"购买激活码""联系客服"按钮点击跳转）
	static constexpr std::wstring_view kBuyLicenseUrl{L"https://noepay.cn/"};            // 购买激活码
	static constexpr std::wstring_view kServiceUrl{L"https://work.weixin.qq.com/kfid/kfce45838d309351f53"};  // WX 客服

public:
	HINSTANCE moduleInstance() const noexcept { return m_hInstance; }
	std::wstring_view cmdLine() const noexcept { return m_strCmdLine; }
	int cmdShow() const noexcept { return m_nCmdShow; }

	ID2D1Factory* d2d1Factory() const noexcept { return m_pDirect2dFactory.get(); }
	IDWriteFactory* dWriteFactory() const noexcept { return m_pDWriteFactory.get(); }
	const AppCommonTextFormat& textFormat() const noexcept { return m_commonTextFormat; }

	std::wstring_view exeFullName() const noexcept { return m_exeFullName; }
	std::wstring_view exeDir() const noexcept { return m_exeDir; }

	// 环境数据根目录（Env 文件夹的父目录，即"环境数据放哪"）：
	//  1) 注册表已有记录且目录可写 → 直接使用（保证多次启动一致）
	//     兼容老版本：优先读 Software\eBox，未命中则读 Software\2Box 并迁移
	//  2) 升级兼容：exe 目录下已存在 Env 数据 → 沿用原目录，避免老环境数据"丢失"
	//  3) 全新部署：默认 C:\eBoxData（C 盘不可用则 D:\eBoxData），并写入防误删提示文件
	//     兼容老版本：若 C:\2BoxData 已存在数据，直接沿用（不迁移）
	//  4) 兜底：exe 目录
	// 说明：分发给员工时 exe 往往放在桌面，若环境数据跟随 exe 落在桌面容易被误删，
	// 因此默认放到固定盘。MemoryDll 侧的路径基于注入的 rootPath（= 本函数返回值），自动跟随。
	std::wstring envDataRoot() const
	{
		namespace fs = std::filesystem;
		constexpr std::wstring_view regSubKey{L"Software\\eBox"};
		constexpr std::wstring_view legacyRegSubKey{L"Software\\2Box"};
		constexpr std::wstring_view regValueName{L"DataDir"};

		const auto tryDir = [](const std::wstring& dir) -> bool
		{
			std::error_code ec;
			fs::create_directories(fs::path{dir} / fs::path{L"Env"}, ec);
			return !ec;
		};

		// 1) 注册表已有记录且可写 → 直接使用
		//    优先读新键 Software\eBox；未命中则读旧键 Software\2Box（兼容老版本升级）
		{
			HKEY hKey = nullptr;
			wchar_t buf[MAX_PATH]{};
			DWORD bufSize = sizeof(buf);
			LSTATUS st = ERROR_FILE_NOT_FOUND;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, regSubKey.data(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
			{
				st = RegQueryValueExW(hKey, regValueName.data(), nullptr, nullptr,
				                      reinterpret_cast<LPBYTE>(buf), &bufSize);
				RegCloseKey(hKey);
			}
			// 兼容老版本：读旧键 Software\2Box
			if (st != ERROR_SUCCESS || buf[0] == L'\0')
			{
				buf[0] = L'\0'; bufSize = sizeof(buf);
				if (RegOpenKeyExW(HKEY_CURRENT_USER, legacyRegSubKey.data(), 0, KEY_READ, &hKey) == ERROR_SUCCESS)
				{
					st = RegQueryValueExW(hKey, regValueName.data(), nullptr, nullptr,
					                      reinterpret_cast<LPBYTE>(buf), &bufSize);
					RegCloseKey(hKey);
				}
			}
			if (st == ERROR_SUCCESS && buf[0] != L'\0')
			{
				const std::wstring recorded{buf};
				if (tryDir(recorded))
				{
					save_env_data_root(recorded);  // 写入新键
					return recorded;
				}
			}
		}

		// 2) 升级兼容：exe 目录已存在 Env 数据 → 沿用原目录（不迁移、不丢失）
		const fs::path legacyEnvDir = fs::path{exeDir()} / fs::path{L"Env"};
		std::error_code ec;
		if (fs::exists(legacyEnvDir, ec) && !ec)
		{
			bool empty = fs::is_empty(legacyEnvDir, ec);
			if (ec || !empty) // 非空（已有环境数据）或查询失败 → 沿用原目录
			{
				save_env_data_root(std::wstring{exeDir()});
				return std::wstring{exeDir()};
			}
		}

		// 3) 全新部署：固定盘 + 防误删提示文件
		// 为避免环境数据（聊天记录/缓存）挤占系统盘导致 C 盘爆满卡顿，
		// 优先放到剩余空间更大的盘（C/D 都可用时选剩余空间大的，再按顺序尝试）。
		// 兼容老版本：若 C:\2BoxData 已存在 Env 数据，直接沿用旧目录。
		const auto driveFreeBytes = [](std::wstring_view dataRoot) -> std::uint64_t
		{
			// 取盘根（如 C:\）查询该盘剩余空间；查询失败返回 0 使其排到最后
			ULARGE_INTEGER freeBytes{};
			const std::wstring root = std::wstring{dataRoot.substr(0, 2)} + L"\\";
			if (GetDiskFreeSpaceExW(root.c_str(), &freeBytes, nullptr, nullptr))
			{
				return freeBytes.QuadPart;
			}
			return 0;
		};

		// 兼容老版本：旧目录 C:\2BoxData / D:\2BoxData 已有 Env 数据则沿用
		std::array<std::wstring_view, 2> legacyRoots{L"C:\\2BoxData", L"D:\\2BoxData"};
		for (const std::wstring_view legacy : legacyRoots)
		{
			const fs::path legacyEnv = fs::path{legacy} / L"Env";
			std::error_code ecL;
			if (fs::exists(legacyEnv, ecL) && !ecL && !fs::is_empty(legacyEnv, ecL))
			{
				const std::wstring rootStr{legacy};
				if (tryDir(rootStr))
				{
					save_env_data_root(rootStr);
					return rootStr;
				}
			}
		}

		std::array<std::wstring_view, 2> candidateRoots{L"C:\\eBoxData", L"D:\\eBoxData"};
		std::sort(candidateRoots.begin(), candidateRoots.end(),
		          [&](std::wstring_view a, std::wstring_view b) { return driveFreeBytes(a) > driveFreeBytes(b); });

		for (const std::wstring_view root : candidateRoots)
		{
			const std::wstring rootStr{root};
			if (tryDir(rootStr))
			{
				const fs::path tipFile = fs::path{rootStr} / L"请勿删除此文件夹.txt";
				std::error_code ec2;
				if (!fs::exists(tipFile, ec2) || ec2)
				{
					std::ofstream ofs{tipFile, std::ios::binary | std::ios::trunc};
					if (ofs)
					{
						ofs << "\xEF\xBB\xBF"; // UTF-8 BOM
						ofs << "【重要】此文件夹是 eBox 多开环境的数据目录（存放 QYWX/WX 等应用的环境数据）。\n"
						       "请勿删除、移动或重命名本文件夹，否则所有环境（登录状态/聊天记录/配置）将全部丢失！\n"
						       "如磁盘空间不足，请保留本文件夹本身，可联系管理员清理内部缓存。\n";
					}
				}
				save_env_data_root(rootStr);
				return rootStr;
			}
		}

		// 4) 兜底
		return std::wstring{exeDir()};
	}

	void exit() const noexcept
	{
		m_eventLoop.finish();
	}

	// ReSharper disable CppInconsistentNaming
	auto get_scheduler() const noexcept
	{
		return sched::Scheduler<sched::EventLoopForWinUi>{&m_eventLoop};
	}

	// ReSharper restore CppInconsistentNaming

private:
	friend int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow);

	MainApp(HINSTANCE hInstance, std::wstring_view lpCmdLine, int nCmdShow)
	{
		HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pDirect2dFactory);
		if (FAILED(hr))
		{
			throw std::runtime_error(std::format("D2D1CreateFactory fail, HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)));
		}

		hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_pDWriteFactory));
		if (FAILED(hr))
		{
			throw std::runtime_error(std::format("DWriteCreateFactory fail, HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)));
		}

		m_commonTextFormat.createAllCommonTextFormat(m_pDWriteFactory);

		m_hInstance = hInstance;
		m_strCmdLine = lpCmdLine;
		m_nCmdShow = nCmdShow;

		constexpr DWORD pathLength = std::numeric_limits<short>::max();
		m_exeFullName.resize(pathLength);
		const DWORD resultSize = GetModuleFileNameW(nullptr, m_exeFullName.data(), pathLength);
		m_exeFullName.resize(resultSize);
		m_exeFullName = std::wstring(m_exeFullName);

		namespace fs = std::filesystem;
		const fs::path fsPath{m_exeFullName};
		const fs::path fsDir{fsPath.parent_path()};
		m_exeDir = fsDir.native();

		parseCmdLine();
	}

	void runMessageLoop() const
	{
		m_eventLoop.run();
	}

	void parseCmdLine() const;
	void waitAnotherInstanceEnd(std::wstring_view strPid) const;

	// 持久化环境数据根目录到 HKCU\Software\eBox\DataDir
	static void save_env_data_root(const std::wstring& dir)
	{
		HKEY hKey = nullptr;
		if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\eBox", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS)
		{
			RegSetValueExW(hKey, L"DataDir", 0, REG_SZ,
			               reinterpret_cast<const BYTE*>(dir.c_str()),
			               static_cast<DWORD>((dir.size() + 1) * sizeof(wchar_t)));
			RegCloseKey(hKey);
		}
	}

private:
	HINSTANCE m_hInstance{nullptr};
	std::wstring m_strCmdLine;
	int m_nCmdShow{SW_HIDE};
	std::wstring m_exeFullName;
	std::wstring m_exeDir;

private:
	struct ComInitGuard
	{
		ComInitGuard()
		{
			if (const HRESULT hr = CoInitialize(nullptr); FAILED(hr))
			{
				throw std::runtime_error(std::format("CoInitialize fail, HRESULT:{:#08x}", static_cast<std::uint32_t>(hr)));
			}
		}

		~ComInitGuard()
		{
			CoUninitialize();
		}
	};

	[[no_unique_address]] ComInitGuard m_comInitGuard;
	UniqueComPtr<ID2D1Factory> m_pDirect2dFactory;
	UniqueComPtr<IDWriteFactory> m_pDWriteFactory;
	AppCommonTextFormat m_commonTextFormat;

private:
	mutable sched::EventLoopForWinUi m_eventLoop;
};

inline MainApp* g_app{nullptr};

export MainApp& app() noexcept
{
	return *g_app;
}

export void show_error_message(std::wstring_view msg)
{
	app().get_scheduler().addTask([msg = std::wstring{msg}]
	{
		MessageBoxW(nullptr, msg.c_str(), MainApp::appName.data(), MB_OK | MB_ICONERROR | MB_TASKMODAL);
	});
}

export std::wstring utf8_to_wide_string(std::string_view utf8)
{
	const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	if (len == 0)
	{
		throw std::runtime_error{std::format("MultiByteToWideChar fail, error code: {}", GetLastError())};
	}
	std::wstring result(len, 0);
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), result.data(), len))
	{
		throw std::runtime_error{std::format("MultiByteToWideChar fail, error code: {}", GetLastError())};
	}
	return result;
}

export void show_utf8_error_message(std::string_view msg)
{
	show_error_message(utf8_to_wide_string(msg));
}

export void show_require_elevation_message(std::wstring_view requester, std::wstring_view path);
