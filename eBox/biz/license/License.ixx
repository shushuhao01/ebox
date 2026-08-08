export module biz.License;

import std;

// 离线激活码体系（纯离线 + ECDSA P-256 签名 + 机器软绑定）：
//   - 激活码 = 载荷(18B) + ECDSA 签名(64B)，Base32 编码，文本形如 EBOX-XXXXX-XXXXX-...（旧码 2BOX- 前缀仍兼容）
//   - 客户端内置公钥验签（不可伪造、不可篡改），私钥只在发码工具中
//   - 载荷含：magic(2B) + 版本号(2B) + 到期字段(4B) + 指纹标志(1B) + 机器指纹(8B) + 解绑次数(1B)
//   - 到期字段：旧格式(verMinor<=7)为绝对到期时间戳；时长制(verMinor==8 离线/==9 在线)
//     为有效时长秒数（0=永久），到期时间 = 客户首次激活时间 + 有效时长，提前生成的库存码不受影响；
//     KeyGen 发离线码(==8，未托管)，面板发在线托管码(==9，服务端登记)
namespace biz
{
	export namespace license
	{
		// 本机机器指纹（8 字节哈希的十六进制字符串，16 字符）
		export std::wstring machineFingerprint();

		// 尝试用激活码激活；校验通过返回 true 并持久化激活状态
		export bool tryActivate(const std::wstring& code);

		// 是否已激活（激活码有效且机器指纹匹配）
		export bool isActivated();

		// 当前持久化的激活码原文（未激活返回空串）
		export std::wstring currentActivationCode();

		// 是否可启动新进程/新建环境（已激活且未过期）
		export bool canLaunch();

		// 过期时间戳（Unix 秒），未激活返回 0
		export std::int64_t expireTime();

		// 过期时间显示文本（如 2027-12-31），未激活返回空串
		export std::wstring expireDateText();

		// 剩余天数（向上取整，到期提醒用）：未激活/永久码/无到期信息返回 -1；已到期返回 0
		export int remainingDays();

		// 清除激活状态（调试/测试用）
		export void deactivate();

		// 当前激活码是否绑定机器（true=绑定单用，false=不绑定通用）
		export bool isBound();

		// 当前激活码每自然月最大解绑次数：-1=不限，0=禁止解绑，n=最多 n 次
		export int unbindMaxPerMonth();

		// 本月已解绑次数（按月自动重置）
		export int unbindCountThisMonth();

		// 解绑结果
		export enum class UnbindResult
		{
			Success,                  // 解绑成功
			NotActivated,             // 未激活
			NotBound,                 // 当前是非绑定码，无需解绑
			OtherInstancesRunning,    // 有其他 eBox 进程在运行，禁止解绑
			ExceededLimit,            // 本月解绑次数已达上限
		};

		// 解绑本机：清除本机激活状态（激活码可转到其他机器使用）。
		// 约束：绑定码才可解绑；存在其他运行中的 eBox 进程时拒绝；受每自然月解绑次数上限约束
		export UnbindResult unbind();

		// 在线换机解绑结果（联网版）：
		//   已登记码：服务端校验次数并签发换机码（继承剩余时长），newCode 非空
		//   未登记/纯离线码：服务端不托管（offlineCode），回退本地解绑，newCode 为空
		export struct UnbindOutcome
		{
			UnbindResult result{UnbindResult::NotActivated};
			std::wstring newCode;   // 服务端签发的换机码（成功且在线托管时非空）
			std::wstring message;   // 附加提示（如被锁定原因）
		};
		export UnbindOutcome unbindForSwitch();

		// 最近一次激活失败原因（空串=成功或无失败记录），供 UI 展示具体拒绝原因
		export std::wstring lastActivateError();

		// 在线授权状态文本（如"在线授权"/"离线宽限剩 X 天"/"已锁定"），供授权信息界面展示
		export std::wstring onlineStatusText();

		// ---- 后台心跳线程（联网授权版）----
		// 每 heartbeatIntervalHours() 小时向服务端上报一次运行状态。
		// 服务端返回 revoked/expired/kicked 时自动锁定本机（清空授权并弹提示），
		// 使作废/过期/强制下线在客户端即时生效。纯离线码（未登记）不受影响。
		// 宿主（MainApp）在生命周期内调用 start/stop。
		export void startHeartbeatLoop();
		export void stopHeartbeatLoop();

		// 纯校验激活码（不持久化），返回有效性/到期时间/是否绑定/解绑上限；发码工具校验用
		export struct VerifyResult
		{
			bool valid{false};
			std::int64_t expire{0};         // 旧格式：绝对到期时间戳；时长制：0（未激活无法确定具体日期）
			std::int64_t durationSec{0};    // 时长制：有效时长（秒），0=永久
			bool isDurationFormat{false};   // 是否时长制（自客户首次激活起算）
			bool onlineManaged{false};      // 是否在线托管码（verMinor==9，面板生成、服务端登记）
			bool bound{false};
			int unbindMax{0};         // -1=不限
		};
		export VerifyResult verifyCode(const std::wstring& code);

#ifdef LICENSE_GENERATOR
		// 【仅发码工具】生成激活码。两种格式：
		//   durationFormat=true （库存码）：field 为有效时长（秒，0=永久），
		//     到期时间 = 客户首次激活时间 + 有效时长，提前生成的库存码不受影响
		//   durationFormat=false（换机码）：field 为绝对到期时间戳（秒），
		//     用于换机/续期接续剩余时间（到期 = 原码剩余时间对应的日期），新旧客户端均兼容
		// bound 为是否绑定机器，machineCode 保留参数（当前不写入指纹，激活时自动绑定本机），
		// unbindMaxPerMonth 为绑定码每自然月最大解绑次数（-1=不限，0=禁止，1..255=次数）
		export std::wstring generateActivationCode(std::int64_t field, bool bound,
		                                           const std::wstring& machineCode,
		                                           int unbindMaxPerMonth, bool durationFormat = true);
#endif
	}
}
