export module biz.Update;

import std;
import Coroutine;

// 在线升级模块（manifest 检查 + 下载 + SHA-256 校验 + 一键安装替换）：
//   - manifest 托管在固定 URL（HTTP/HTTPS），JSON 格式，包含版本号/下载链接/SHA-256/changelog
//   - 客户端启动后异步拉取 manifest，与本地 kVerCode 比较判断是否有新版本
//   - 用户在标题栏红点入口点"立即更新" → 二次确认 → 后台下载 → SHA-256 校验 → 生成
//     updater.bat → 当前进程退出 → bat 覆盖 exe → 重启新版本
//   - 全程不强制更新，红点可被"以后再说"忽略（按版本号记录）
//   - 数据完整保留：注册表配置、环境数据目录均不受 exe 替换影响
namespace biz
{
	export namespace update
	{
		// ===== manifest 数据结构（对应服务端 JSON）=====
		export struct UpdateManifest
		{
			std::wstring latestVersion;       // 显示用，如 "v2.8.0"
			int latestVersionCode{0};         // 版本比较主依据（单调递增数字）
			std::wstring releaseDate;         // 如 "2026-09-01"
			std::wstring downloadUrl;         // exe 直链
			std::wstring downloadSha256;      // 64 字符小写十六进制
			std::uint64_t downloadSize{0};    // 字节数，用于显示"约 X MB"
			std::vector<std::wstring> changelog; // 更新日志，每条一行
			int minSkipVersionCode{0};        // 低于此版本不允许忽略（用于修关键 bug）
			bool forceUpdate{false};          // true=强制更新（红点不可熄灭）
		};

		// 检查结果
		export enum class CheckResult
		{
			NoUpdate,             // 远程版本不高于本地
			HasUpdate,            // 有新版本可用
			SkippedThisVersion,   // 用户曾点"以后再说"且非强制
			NetworkError,         // 拉取/解析失败
		};

		// 检查结果综合（result==HasUpdate 时 manifest 有效）
		export struct CheckOutcome
		{
			CheckResult result{CheckResult::NetworkError};
			UpdateManifest manifest;
		};

		// 下载进度
		export struct DownloadProgress
		{
			std::uint64_t downloaded{0};
			std::uint64_t total{0};  // 0 表示未知大小
		};

		// 下载+校验综合结果
		export enum class DownloadResult
		{
			Success,
			Cancelled,
			NetworkError,
			DiskError,
			VerifyFailed,  // SHA-256 不匹配
		};

		export struct DownloadOutcome
		{
			DownloadResult result{DownloadResult::NetworkError};
			std::wstring filePath;     // 临时 exe 路径（仅 Success 时有效）
			std::wstring errorMessage; // 失败原因（用于日志）
		};

		// ===== 异步接口（coro::LazyTask，UI 层用 asyncScope.spawn 启动）=====

		// 拉取 manifest + 版本比较 + 查忽略状态
		// 失败/无更新时 manifest 字段保持默认空值
		export coro::LazyTask<CheckOutcome> checkUpdateAsync();

		// 下载更新包到 %TEMP%\2Box_update_v<code>.exe 并做 SHA-256 校验
		// progressCb 在工作线程回调；支持 LazyTask 取消（co_await get_current_cancellation_token）
		export coro::LazyTask<DownloadOutcome> downloadAndVerifyAsync(
			const UpdateManifest& manifest,
			std::function<void(const DownloadProgress&)> progressCb);

		// ===== 同步辅助接口（非协程，立即返回）=====

		// 记录用户忽略此版本号（写入注册表 IgnoredVersionCode）
		export void ignoreVersion(int versionCode);

		// 获取下载临时文件路径（不创建文件，仅返回路径）
		export std::wstring getTempDownloadPath(int versionCode);

		// 生成 updater.bat 并启动；本进程应在调用后立即退出
		// 失败返回 false（不应继续退出，由调用方决定如何处理）
		export bool applyUpdate(const std::wstring& downloadedExePath);

		// 新版本启动后调用：检测 2Box.exe.bak 存在则删除（标记升级成功）
		export void cleanupBackupIfNeeded();
	}
}
