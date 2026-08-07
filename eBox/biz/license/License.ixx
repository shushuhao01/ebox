export module biz.License;

import std;

// 离线激活码体系（纯离线 + ECDSA P-256 签名 + 机器软绑定）：
//   - 激活码 = 载荷(17B) + ECDSA 签名(64B)，Base32 编码，文本形如 2BOX-XXXXX-XXXXX-...
//   - 客户端内置公钥验签（不可伪造、不可篡改），私钥只在发码工具中
//   - 载荷含：magic(2B) + 版本号(2B) + 到期时间戳(4B) + 指纹标志(1B) + 机器指纹(8B)
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

		// 纯校验激活码（不持久化），返回有效性/到期时间/是否绑定/解绑上限；发码工具校验用
		export struct VerifyResult
		{
			bool valid{false};
			std::int64_t expire{0};
			bool bound{false};
			int unbindMax{0};         // -1=不限
		};
		export VerifyResult verifyCode(const std::wstring& code);

#ifdef LICENSE_GENERATOR
		// 【仅发码工具】生成激活码：expireUnix 为到期时间戳(秒)，bound 为是否绑定机器，
		// machineCode 为本机指纹十六进制串（bound=true 时必填，长度 16），
		// unbindMaxPerMonth 为绑定码每自然月最大解绑次数（-1=不限，0=禁止，1..255=次数）
		export std::wstring generateActivationCode(std::int64_t expireUnix, bool bound,
		                                           const std::wstring& machineCode,
		                                           int unbindMaxPerMonth);
#endif
	}
}
