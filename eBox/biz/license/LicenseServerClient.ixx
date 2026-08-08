export module biz.LicenseServerClient;

import std;

// ============================================================
// 联网授权客户端（v2.8.0+）
//   同步 WinHTTP 调用授权服务器（/api/v1/*），与后端 license-server 对接。
//   服务器地址：默认 kDefaultServerUrl（见 .cpp），
//               可在注册表 HKCU\Software\2Box\ServerUrl 覆盖（便于联调与部署）。
//   在线状态持久化同样写入 HKCU\Software\2Box（与 biz.License 共享子键）：
//     ServerStatus        REG_SZ   ok/revoked/expired/kicked（空=无记录）
//     WasOnline           REG_DWORD 1=曾成功联网（此后启用离线宽限管控）
//     LastHeartbeatAt     REG_QWORD Unix 秒
//     GraceUntil          REG_QWORD Unix 秒（服务端下发的宽限截止）
// ============================================================
export namespace biz
{
	export namespace licenseserver
	{
		// 服务器地址（含协议与端口，如 https://license.example.com），注册表可覆盖
		export std::wstring serverBaseUrl();

		// 心跳周期（小时，默认 6）与离线宽限天数（默认 7），注册表可覆盖
		export int heartbeatIntervalHours();
		export int offlineGraceDays();
		// 是否强制在线激活（默认 false：服务器不可达时按离线方式放行）
		export bool forceOnlineActivate();

		// 激活结果
		export struct ActivateResult
		{
			bool ok{false};        // 请求成功且业务成功（含已登记放行 / 未登记离线码放行）
			bool online{false};    // 已登记码 true；未登记有效码 false
			bool revoked{false};   // 已作废
			bool expired{false};   // 已过期
			bool exceeded{false};  // 绑定码已绑定其他设备
			std::wstring msg;
			std::int64_t serverTime{0};
			std::int64_t graceUntil{0};  // 授权真实到期（服务端下发）：离线可用到该时刻
			int heartbeatIntervalHours{6}; // 服务端授权策略：心跳间隔（小时），同步写入本地注册表
			bool forceOnlineActivate{false}; // 服务端授权策略：是否强制在线激活，同步写入本地注册表
		};
		export ActivateResult activate(const std::wstring& code, const std::wstring& machineFp,
		                               const std::wstring& appVersion, const std::wstring& os);

		// 心跳结果
		export struct HeartbeatResult
		{
			bool ok{false};        // 请求成功且业务成功
			bool online{false};
			std::wstring status;   // ok / revoked / expired / kicked
			std::int64_t graceUntil{0};  // 授权真实到期（离线可用到该时刻）
			std::wstring notice;
			int heartbeatIntervalHours{6}; // 服务端授权策略：心跳间隔（小时），同步写入本地注册表
			bool forceOnlineActivate{false}; // 服务端授权策略：是否强制在线激活，同步写入本地注册表
		};
		export HeartbeatResult heartbeat(const std::wstring& code, const std::wstring& machineFp,
		                                 const std::wstring& appVersion);

		// 服务端解绑换机结果
		export struct UnbindResult
		{
			bool ok{false};
			bool exceed{false};        // 本月解绑次数已达上限
			bool offlineCode{false};   // 未登记码（服务器不托管）
			std::wstring newCode;      // 服务端签发的新换机码
		};
		export UnbindResult unbind(const std::wstring& code, const std::wstring& machineFp,
		                           const std::wstring& appVersion);

		// ---- 在线状态持久化 ----
		export void storeServerStatus(const std::wstring& status);   // ok/revoked/expired/kicked/空=清除
		export std::wstring serverStatus();                          // 空=无记录
		export bool serverLocked();                                  // revoked/expired/kicked
		export void storeOnlineHeartbeat(std::int64_t lastAt, std::int64_t graceUntil); // 成功心跳后刷新基线并置 WasOnline
		export std::int64_t graceUntil();
		export bool wasOnlineBefore();
		export void recordOnline();                                  // 激活成功后标记已纳入在线管理

		// 实时在线状态（内存）：最近一次激活/心跳是否连通服务器，
		// 供授权信息界面区分"在线授权"与"离线宽限"，避免连上服务器却显示离线宽限
		export void markOnline(bool online);
		export bool onlineNow();

		// 服务端系统公告（最新一条）：心跳获取后持久化（注册表 Notice），供 UI 公告栏展示；
		// 传入空串表示清除（服务端已撤下公告）。纯离线码不经过心跳，不受影响。
		export void storeNotice(const std::wstring& notice);
		export std::wstring currentNotice();

		// 联调用：写注册表覆盖服务器地址
		export void setServerUrl(const std::wstring& url);
	}
}
