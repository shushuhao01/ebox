# eBox 授权服务平台（license-server）

> 文中 `<your-domain>` 为占位符，请替换为你的真实部署域名（如 `abc.example.cn`）。

> eBox 客户端的在线授权管理后台：**激活码签发 / 设备绑定 / 心跳监控 / 换机 / 作废 / 公告发布 / 回收站** 全链路服务端实现。
>
> 包含两部分：`backend`（Node.js + TypeScript + TypeORM API 服务）与 `admin`（Vue3 + Element Plus 管理面板）。

---

## 目录

- [功能总览](#功能总览)
- [技术栈](#技术栈)
- [目录结构](#目录结构)
- [本地开发](#本地开发)
- [宝塔服务器部署](#宝塔服务器部署)
- [环境变量说明](#环境变量说明)
- [客户端对接](#客户端对接)
- [仓库说明与同步](#仓库说明与同步)
- [许可证](#许可证)

---

## 功能总览

| 模块 | 说明 |
|---|---|
| 激活码管理 | 在线生成激活码（`EBOX-` 前缀，Base32 + ECDSA P-256 签名）、批次管理、导出 CSV、批量作废/删除、回收站恢复 |
| 设备管理 | 设备绑定/换机/踢下线（踢下线后客户端下一次心跳即被锁定） |
| 心跳监控 | 客户端周期性上报（默认 6h，注册表可调），离线宽限期（默认 7 天） |
| 操作日志 | 全量记录生成/作废/换机/踢下线/删除等管理操作，含详细内容 |
| 数据统计 | 总览仪表盘：激活码存量、状态分布、时长分布、设备在线情况、最近心跳流水 |
| 系统公告 | 在线发布公告，客户端心跳拉取后于公告栏展示 |
| 用户权限 | 管理员账号（JWT 登录），可修改密码 |

### 激活码载荷格式（与客户端严格一致）

- magic：`42 4F`（"BO"）
- 版本：major=2；minor：7=旧换机码（绝对到期）、8=离线时长制（KeyGen 发码）、9=在线时长制（面板发码，服务端托管）
- `[4..7]` 时长/到期字段，`[8]` 绑定标志，`[17]` 解绑上限（0xFF=不限）
- 首次激活计时锚定：服务端以 `key.usedAt` 为准，同码重复激活不重置计时

---

## 技术栈

| 端 | 技术 |
|---|---|
| 后端 | Node.js 22+ · TypeScript · Express · TypeORM · MySQL 8 · JWT · Joi · winston |
| 前端 | Vue 3 · Element Plus · Vite · Pinia · Vue Router · ECharts |
| 部署 | PM2 · Nginx 反向代理 · 宝塔面板（也可裸机） |

---

## 目录结构

```
license-server/
├── backend/                 # 后端 API 服务（Node/Express/TypeORM）
│   ├── src/
│   │   ├── crypto/          #   激活码编解码/签名（与客户端公钥配对）
│   │   ├── entities/        #   TypeORM 实体
│   │   ├── routes/          #   admin/（管理接口）client.ts（客户端接口）
│   │   ├── services/        #   KeyService / StatsService / ConfigService / LogService
│   │   ├── scripts/         #   init-db / init-admin / gen-key
│   │   └── app.ts
│   ├── .env.example         # 环境配置模板（复制为 .env 使用）
│   ├── ecosystem.config.js  # PM2 进程配置
│   └── package.json
├── admin/                   # 管理面板（Vue3 + Element Plus）
│   ├── src/views/           #   Dashboard/Licenses/Batches/Devices/Heartbeats/Logs/RecycleBin/Settings...
│   └── package.json
├── database/schema.sql      # MySQL 表结构（部署时导入）
├── deploy/                  # 服务器脚本
│   ├── deploy.sh            #   首次部署（依赖/构建/.env/建库/PM2）
│   ├── update.sh            #   增量更新（重新构建 + 重启）
│   ├── backup.sh            #   数据库备份（保留最近 30 份）
│   └── nginx/<your-domain>.conf #   Nginx 站点配置（443 → 3008 反代）
└── README.md
```

---

## 本地开发

要求：Node.js >= 22。

```bash
# 1. 后端依赖 + 启动（开发模式，自动监听编译）
cd backend
npm install
npm run dev:local        # 读取 .env.local（本地配置，不入库）

# 2. 前端依赖 + 启动（开发服务器，/api 代理到 127.0.0.1:3008）
cd ../admin
npm install
npm run dev              # http://localhost:5173

# 3. 数据库初始化（首次）
cd ../backend
npm run init:db          # 校验/初始化表结构
npm run init:admin -- --username admin --password admin123
```

常用脚本（`backend` 目录）：

| 命令 | 说明 |
|---|---|
| `npm run build` | TypeScript 编译到 `dist/` |
| `npm start` | 运行编译产物（生产模式） |
| `npm run gen:key` | 查看/核对当前私钥配置的公钥（应与客户端内置公钥一致） |
| `npm run typecheck` | 类型检查 |

> 本地联调客户端：注册表 `HKCU\Software\2Box` 下新建字符串 `ServerUrl=http://127.0.0.1:3008`，即可让 eBox 客户端连接本地后端（见[客户端对接](#客户端对接)）。

---

## 宝塔服务器部署

> 完整图文步骤见仓库内 [docs/宝塔部署.md](https://github.com/shushuhao01/ebox/blob/main/docs/%E5%AE%9D%E5%A1%94%E9%83%A8%E7%BD%B2.md)（主仓库同步维护）。

**部署拓扑**：

```
eBox 客户端 (WinHTTP) ──https──► <your-domain>:443 (Nginx)
管理面板 (浏览器)      ──https──► <your-domain>:443 (Nginx 静态 + /api 反代)
                                      │
                                      ▼
                             127.0.0.1:3008 (Node/Express/PM2)
                                      │
                                      ▼
                               MySQL 8 (license_server)
```

**快速部署（宝塔）**：

1. **建站**：宝塔「网站 → 添加站点」→ 域名 `<your-domain>`、根目录 `/www/wwwroot/license-server`。
2. **Node.js**：宝塔安装「Node.js 版本管理器」，启用版本 **>= 22**；终端执行 `npm install -g pm2`。
3. **MySQL**：安装 MySQL 8；创建库 `license_server`、账号 `license`。
4. **拉代码**（本仓库直接 clone 到站点根目录）：
   ```bash
   cd /www/wwwroot
   git clone https://github.com/shushuhao01/eBox-online.git license-server
   cd license-server
   chmod +x deploy/*.sh
   ```
5. **首次部署**：`bash deploy/deploy.sh`
6. **改配置**：编辑 `backend/.env`，把 `DB_PASSWORD`、`JWT_SECRET` 改成真实值；`LICENSE_PRIVATE_KEY_D` **保留默认出厂密钥**（与客户端公钥配套）。
7. **Nginx + SSL**：宝塔「网站 → <your-domain> → 配置文件」替换为 `deploy/nginx/<your-domain>.conf` 内容；申请 Let's Encrypt 证书并重载 Nginx。
8. **验证**：`curl -k https://<your-domain>/health`，浏览器打开 `https://<your-domain>` 登录管理面板。

**日常更新**（服务器上一条命令完成，无需手动 git pull）：

```bash
cd /www/wwwroot/license-server
bash deploy/update.sh
```

`update.sh` 会依次完成：环境检查（Node≥22）→ `.env` 配置检查 → 域名解析/SSL 证书/Nginx 配置自检 → 备份配置 → 拉取最新代码 → 重建前后端 → **PM2 启停** → 健康验证。任意步骤失败都会输出错误定位 + 日志尾部详情。

PM2 逻辑：进程 `<your-domain>-backend` 已存在则 `pm2 restart`；服务器重启后进程不存在则自动 `pm2 start ecosystem.config.js`。

**备份/恢复**：

```bash
bash deploy/backup.sh     # 备份到 deploy/backups/（保留最近 30 份）
# 恢复示例
gunzip -c backups/license_server_时间戳.sql.gz | mysql -ulicense -p你的密码 license_server
```

---

## 环境变量说明

| 变量 | 必填 | 说明 |
|---|---|---|
| `PORT` | 是 | 后端监听端口（Nginx 反代目标，默认 3008） |
| `NODE_ENV` | 是 | `production` / `development` |
| `DB_HOST` / `DB_PORT` | 是 | MySQL 连接 |
| `DB_USER` / `DB_PASSWORD` | 是 | MySQL 账号（生产务必改强密码） |
| `DB_DATABASE` | 是 | 库名，默认 `license_server` |
| `JWT_SECRET` | 是 | 管理接口签名密钥（随机长字符串） |
| `JWT_EXPIRES` | 否 | 登录态有效期，默认 `7d` |
| `LICENSE_PRIVATE_KEY_D` | 是 | ECDSA P-256 私钥标量（hex，64 字符）；**必须与 eBox 客户端内置公钥配对**，默认出厂密钥已配对，勿随意修改 |
| `CORS_ORIGIN` | 否 | 允许跨域的源（开发用），生产同源可留空 |

> `.env` / `.env.local` 已 gitignore，绝不入库。

---

## 客户端对接

- eBox 客户端默认连接 **`https://<your-domain>`**（编译期内置），部署完成即可直接激活。
- 换域名 / 本地联调，用注册表覆盖，免重新编译：
  ```powershell
  reg add "HKCU\Software\2Box" /v ServerUrl /t REG_SZ /d "https://新域名" /f
  reg add "HKCU\Software\2Box" /v ServerUrl /t REG_SZ /d "http://127.0.0.1:3008" /f   # 本地联调
  ```
- 在线托管码（面板生成，格式版本 9）走 `POST /api/v1/activate` 激活；离线码（KeyGen 生成）不登记服务端，互不干扰。

---

## 仓库说明与同步

本项目是 **eBox 主仓库**（[shushuhao01/ebox](https://github.com/shushuhao01/ebox)）的 `license-server/` 子目录镜像，通过 **git subtree** 方式独立发布到本仓库（eBox-online），仅用于授权平台的独立部署与版本管理。

- **主仓库**：完整项目（eBox 客户端 + MemoryDll + common + 授权平台 + 文档），日常开发以主仓库为准。
- **本仓库（子仓库）**：只包含授权平台代码，供宝塔服务器 `git clone` 部署。
- **同步方式**：主仓库更新后，在根目录执行同步脚本即可把最新 `license-server/` 覆盖式同步到本仓库：
  ```powershell
  powershell -ExecutionPolicy Bypass -File .\sync-subrepo.ps1
  ```
  脚本基于 `git subtree split` + 强推（force push），保证子仓库始终等于主仓库 `license-server/` 最新内容。

---

## 许可证

[MIT](LICENSE)（与主仓库一致）。
