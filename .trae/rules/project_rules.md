# 私钥零入库与提交前检查（强制）

## 红线
- ECDSA P-256 私钥标量 d（64 位 hex）与 `BCRYPT_ECCPRIVATE_BLOB`（ECS2）：**禁止提交、禁止 push 到 GitHub**，也禁止写入脚本、注释、文档、变更说明。
- 私钥只允许存在两处：本机 `license-server/backend/.env*`（已忽略）与离线文件 `*.private*.bak`（已忽略）。两者都不得 `git add`。
- 客户端 exe 不得定义 `LICENSE_GENERATOR`；`eBox/biz/license/License.cpp` 中 `kPrivateKeyBlob` 必须保持 `0xCC` 占位，禁止还原为真实 blob 后提交。
- 公钥是公开信息（ECS1 blob、X/Y），可以提交。

## 提交前必做检查
每次 `git commit` / push 前，先看暂存清单再扫内容：

```powershell
git diff --cached --name-only
git diff --cached -U0 | Select-String -Pattern 'LICENSE_PRIVATE_KEY_D\s*[=:]\s*[0-9a-fA-F]{64}', '0x45,\s*0x43,\s*0x53,\s*0x32', '-----BEGIN[A-Z ]*PRIVATE KEY-----'
```

- 暂存清单出现 `.env`、`.env.local`、`*.bak`、`*.pem`、`*.key`、`*.pfx` → 立即移除。
- 任一正则命中 → **立即停止，不提交**，向用户报告文件与行号。
- 全部为空/无命中 → 才可以提交。

## 行为约定
- 发现私钥（或疑似私钥）时：先停止、再报告、等用户指示，不要自行改写历史或强推。
- 若私钥已进入一次 commit：只提醒用户，不强推；处置方案由用户决定。

## 当前密钥状态
- 过渡期 `LICENSE_PRIVATE_KEY_D` 仍为旧私钥；新私钥仅离线保存于 `License.cpp.private.new.bak`。
- 客户端 `kPublicKeyBlobs[]` 同时含旧、新公钥；旧码归零前不得移除旧公钥。
- 旧私钥曾泄漏，其值不得出现在任何新提交中（含文档与注释）。
