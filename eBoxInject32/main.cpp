// eBoxInject32：32 位注入器。
//
// 背景：64 位 eBox 需要启动 32 位目标时，Detours 的 64→32 注入无法就地完成，
// 会退化为 SysWOW64\rundll32.exe helper（DetourProcessViaHelperW）；该 helper 在
// 部分安全软件（如天锐绿盾）环境下被拦截，导致目标进程被 TerminateProcess。
// 因此改由本进程（与目标同为 32 位）直接 CreateProcess + 注入。
//
// 命令行：eBoxInject32.exe <job 文件路径>
// job 文件（二进制）：wchar_t magic[4] = L"EBI1"，随后 8 个字段，
// 每个字段 = uint32 字符数 + 该数量的 wchar_t（UTF-16LE）。
// 字段顺序：DLL 路径 / 目标 exe / 参数 / 工作目录 / 环境数据根目录 /
//           envFlag(hex) / envIndex(hex) / versionBits(hex)
// 退出码：0 成功 / 2 job 读取失败 / 3 创建目标进程失败 / 4 写入注入参数失败
// 失败原因同时写入 <job 文件>.log（UTF-16LE），供 eBox 侧读取并写入环境日志。

#include "sys_defs.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	constexpr wchar_t JOB_MAGIC[4] = {L'E', L'B', L'I', L'1'};
	constexpr std::uint32_t JOB_FIELD_COUNT = 8;
	// 单字段字符数上限，避免损坏的 job 文件触发超大分配
	constexpr std::uint32_t JOB_FIELD_MAX_CHARS = 32768;

	struct JobData
	{
		std::wstring dllPath;
		std::wstring targetExe;
		std::wstring params;
		std::wstring workDir;
		std::wstring rootPath;
		ULONGLONG envFlag{0};
		DWORD envIndex{0};
		std::uint32_t versionBits{0};
	};

	bool read_field(std::ifstream& file, std::wstring& out)
	{
		std::uint32_t charCount = 0;
		file.read(reinterpret_cast<char*>(&charCount), sizeof(charCount));
		if (file.gcount() != static_cast<std::streamsize>(sizeof(charCount)) || charCount > JOB_FIELD_MAX_CHARS)
		{
			return false;
		}
		out.resize(charCount);
		if (charCount == 0)
		{
			return true;
		}
		const auto byteCount = static_cast<std::streamsize>(charCount * sizeof(wchar_t));
		file.read(reinterpret_cast<char*>(out.data()), byteCount);
		return file.gcount() == byteCount;
	}

	bool read_job(const std::wstring& jobPath, JobData& job)
	{
		std::ifstream file{std::filesystem::path{jobPath}, std::ios::binary};
		if (!file)
		{
			return false;
		}

		wchar_t magic[4]{};
		file.read(reinterpret_cast<char*>(magic), sizeof(magic));
		if (file.gcount() != static_cast<std::streamsize>(sizeof(magic)) || std::memcmp(magic, JOB_MAGIC, sizeof(magic)) != 0)
		{
			return false;
		}

		std::wstring fields[JOB_FIELD_COUNT];
		for (std::wstring& field : fields)
		{
			if (!read_field(file, field))
			{
				return false;
			}
		}

		job.dllPath = std::move(fields[0]);
		job.targetExe = std::move(fields[1]);
		job.params = std::move(fields[2]);
		job.workDir = std::move(fields[3]);
		job.rootPath = std::move(fields[4]);
		job.envFlag = std::wcstoull(fields[5].c_str(), nullptr, 16);
		job.envIndex = static_cast<DWORD>(std::wcstoul(fields[6].c_str(), nullptr, 16));
		job.versionBits = static_cast<std::uint32_t>(std::wcstoul(fields[7].c_str(), nullptr, 16));

		return !job.dllPath.empty() && !job.targetExe.empty();
	}

	std::string to_ansi(std::wstring_view text)
	{
		if (text.empty())
		{
			return {};
		}
		const int len = WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		if (len <= 0)
		{
			return {};
		}
		std::string result(static_cast<std::size_t>(len), '\0');
		WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), result.data(), len, nullptr, nullptr);
		return result;
	}

	void write_log(const std::wstring& jobPath, std::wstring_view message)
	{
		std::ofstream logFile{std::filesystem::path{jobPath + L".log"}, std::ios::binary | std::ios::trunc};
		if (!logFile)
		{
			return;
		}
		logFile.write(reinterpret_cast<const char*>(message.data()),
		              static_cast<std::streamsize>(message.size() * sizeof(wchar_t)));
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv == nullptr || argc < 2)
	{
		if (argv != nullptr)
		{
			LocalFree(argv);
		}
		return 2;
	}
	const std::wstring jobPath{argv[1]};
	LocalFree(argv);

	JobData job;
	if (!read_job(jobPath, job))
	{
		return 2;
	}

	const std::string dllAnsi = to_ansi(job.dllPath);
	if (dllAnsi.empty())
	{
		write_log(jobPath, L"注入 DLL 路径转换为 ANSI 失败");
		return 2;
	}

	std::wstring cmdLine = std::format(L"\"{}\"", job.targetExe);
	if (!job.params.empty())
	{
		cmdLine += L' ';
		cmdLine += job.params;
	}

	// 目标通常是 GUI 程序：STARTUPINFO 必须保持零值。
	// 若沿用 cmd.exe 路径的 STARTF_USESHOWWINDOW + SW_HIDE，目标主窗口会隐藏启动。
	STARTUPINFOW startupInfo{sizeof(startupInfo)};
	PROCESS_INFORMATION procInfo{};
	if (DetourCreateProcessWithDllExW(job.targetExe.c_str(), cmdLine.data(), nullptr, nullptr, FALSE,
	                                  CREATE_DEFAULT_ERROR_MODE | CREATE_SUSPENDED, nullptr,
	                                  job.workDir.empty() ? nullptr : job.workDir.c_str(),
	                                  &startupInfo, &procInfo, dllAnsi.c_str(), &::CreateProcessW) == FALSE)
	{
		write_log(jobPath, std::format(L"创建目标进程并注入失败，错误码 {}，目标 {}", GetLastError(), job.targetExe));
		return 3;
	}

	// payload 必须在 ResumeThread 之前写入：MemoryDll 的 DllMain 找不到 payload 会直接终止自身。
	const auto rootPathCount = static_cast<std::uint32_t>(job.rootPath.size());
	const auto paramsSize =
		static_cast<std::uint32_t>(sizeof(DetourInjectParams) + rootPathCount * sizeof(wchar_t));
	std::vector<std::byte> buffer(paramsSize);
	auto* injectParams = reinterpret_cast<DetourInjectParams*>(buffer.data());
	injectParams->version.isWindows8Point1OrGreater = (job.versionBits & VERSION_BIT_WIN8_1) != 0;
	injectParams->version.isWindows8OrGreater = (job.versionBits & VERSION_BIT_WIN8) != 0;
	injectParams->version.isWindowsVistaOrGreater = (job.versionBits & VERSION_BIT_VISTA) != 0;
	injectParams->version.is32BitSystem = (job.versionBits & VERSION_BIT_32BIT_SYSTEM) != 0;
	injectParams->envFlag = job.envFlag;
	injectParams->envIndex = job.envIndex;
	injectParams->rootPathCount = rootPathCount;
	std::memcpy(injectParams->rootPath, job.rootPath.data(), rootPathCount * sizeof(wchar_t));

	if (DetourCopyPayloadToProcess(procInfo.hProcess, DETOUR_INJECT_PARAMS_GUID, injectParams, paramsSize) == FALSE)
	{
		write_log(jobPath, std::format(L"写入注入参数失败，错误码 {}", GetLastError()));
		TerminateProcess(procInfo.hProcess, 0);
		CloseHandle(procInfo.hThread);
		CloseHandle(procInfo.hProcess);
		return 4;
	}

	ResumeThread(procInfo.hThread);
	CloseHandle(procInfo.hThread);
	CloseHandle(procInfo.hProcess);
	return 0;
}
