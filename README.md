# eBox

> 轻量级 Windows 软件**多实例运行**工具，尽可能不影响软件原有功能的前提下实现多开隔离。

许多软件会阻止自身多实例运行（如 QYWX、DD、WX 等），这通常是为了简化逻辑或因为业务上并无必要。但在某些场景下，我们仍希望同时启动多个实例以满足特定需求（多账号同时登录、测试环境隔离等）。

eBox 通过 **PE 内存加载 + API Hook + 路径重定向** 的方式，让每个实例拥有独立的配置、缓存与聊天数据目录，实现真正的多开互不干扰。

> 说明：为避免对相关产品造成名称上的困扰，本文以汉语拼音首字母（QYWX / DD / WX）代指部分常见客户端，不直接写出其完整名称。

---

## 目录

- [核心功能](#核心功能)
- [技术架构](#技术架构)
- [项目结构](#项目结构)
- [构建指南](#构建指南)
- [使用说明](#使用说明)
- [eBox-cli 命令行工具](#ebox-cli-命令行工具)
- [授权服务平台（license-server）](#授权服务平台license-server)
- [授权激活机制](#授权激活机制)
- [自动升级机制](#自动升级机制)
- [数据存储与隐私](#数据存储与隐私)
- [版本发布与分发](#版本发布与分发)
- [许可证](#许可证)

---

## 核心功能

| 功能 | 说明 |
|---|---|
| **多实例运行** | 同时启动同一程序的多个实例，每个实例配置/缓存独立，互不干扰 |
| **环境隔离** | 每个实例对应独立的"环境"，路径重定向避免冲突 |
| **进程注入** | 通过 PE Loader 将 `MemoryDll.dll` 加载到目标进程，Hook 关键 API |
| **离线激活** | ECDSA P-256 签名的激活码体系，支持单机绑定/通用码两种模式 |
| **自动升级** | 启动后异步检查更新，红点提示不强制升级，一键下载安装替换 |
| **拖拽启动** | 直接将可执行文件拖到窗口，自动新建/选择环境运行 |
| **多账号支持** | 同时登录多个 QYWX 等客户端，方便多账号管理 |

---

## 技术架构

### 模块组成

```
┌─────────────────────────────────────────────────────┐
│                   eBox.exe (主程序)                  │
├──────────┬──────────┬──────────┬──────────┬──────────┤
│   UI     │   biz    │ MemoryDll│  common  │ 3rdparty│
│  (D2D)   │ (业务层)  │  (注入层) │ (基础设施)│ Detours │
└──────────┴──────────┴──────────┴──────────┴──────────┘
```

### 关键技术

- **C++20 Modules**：项目全面采用模块（`import std;` / `export module xxx;`），编译速度与代码隔离性大幅提升
- **Direct2D + DirectWrite**：自绘 UI（标题栏、按钮、卡片、滚动条），无第三方 UI 框架依赖
- **WinHTTP 异步协程**：基于 `coro::LazyTask<T>` 的异步 HTTP 客户端，用于在线升级与文件下载
- **PE Loader**：自实现的 PE 内存加载器，将 DLL 直接加载到目标进程内存，避免磁盘落地
- **Detours**：Microsoft Detours 库，用于 API Hook
- **ECDSA P-256**：BCrypt 实现的离线激活码签名验证体系
- **RPC**：基于 Windows RPC 的进程间通信（主程序 ↔ 注入 DLL）

### 关键模块说明

| 模块 | 路径 | 职责 |
|---|---|---|
| MainApp | [eBox/app/MainApp.ixx](eBox/app/MainApp.ixx) | 应用入口、全局配置、版本号单一可信源 |
| UI.MainWindow | [eBox/ui/UI.MainWindow.cpp](eBox/ui/UI.MainWindow.cpp) | 主窗口、标题栏自绘、拖拽启动 |
| biz.License | [eBox/biz/license/License.cpp](eBox/biz/license/License.cpp) | 离线激活码签发与验证、解绑次数统计 |
| biz.Update | [eBox/biz/update/Update.cpp](eBox/biz/update/Update.cpp) | 在线升级（manifest 检查/下载/SHA-256 校验/一键安装） |
| biz.Env | [eBox/biz/env/Env-EnvManager.cpp](eBox/biz/env/Env-EnvManager.cpp) | 环境管理、进程-环境映射 |
| biz.WinHttp | [eBox/biz/http/WinHttp.ixx](eBox/biz/http/WinHttp.ixx) | 异步 HTTP 客户端 |
| MemoryDll | [MemoryDll/](MemoryDll/) | 注入到目标进程的 DLL，Hook 各类 API |
| common | [common/](common/) | 协程、调度器、PE 加载器等基础设施 |

---

## 项目结构

```
2Box-master/                  # 仓库根目录（历史名，代码内已统一为 eBox）
├── eBox/                      # 主程序源码
│   ├── app/                   # 应用入口（MainApp）
│   ├── biz/                   # 业务层
│   │   ├── env/                #   环境管理
│   │   ├── license/            #   授权激活
│   │   ├── update/             #   自动升级
│   │   ├── http/               #   HTTP 客户端
│   │   ├── launcher/           #   进程启动器
│   │   ├── file_redirect/      #   路径重定向
│   │   ├── env_log/            #   环境日志
│   │   ├── wnd_enumerator/     #   窗口枚举
│   │   └── symbols/            #   符号加载
│   ├── ui/                     # UI 层（D2D 自绘）
│   │   ├── core/               #   核心 UI 框架
│   │   ├── control/            #   控件（按钮/滚动条/卡片）
│   │   ├── page/               #   页面（首页/下载页）
│   │   ├── UI.MainWindow.cpp   #   主窗口
│   │   └── UI.MainWindow.ixx
│   ├── res/                    # 资源（图标/RC/嵌入 DLL）
│   └── eBox.vcxproj
├── MemoryDll/                 # 注入 DLL 源码
│   ├── hook/                   #   各类 API Hook
│   ├── rpc/                    #   RPC 客户端
│   ├── global_data/            #   全局数据
│   └── dllmain.cpp
├── common/                     # 公共基础设施
│   ├── coroutine/              #   协程库
│   ├── scheduler/              #   调度器
│   ├── pe_loader/              #   PE 加载器
│   ├── dynamic_win_api/        #   动态 Win32 API
│   └── utility/                #   工具函数
├── 3rdparty/Detours/           # Microsoft Detours（API Hook）
├── BuildDllHelper/             # DLL 构建辅助
├── BuildIDL/                   # IDL 构建工具
├── DevTools/eBox-cli/          # 命令行工具
├── tools/KeyGen/               # 激活码生成工具（含私钥，仅内部使用，已 gitignore）
├── license-server/             # 授权服务平台（后端 API + 管理面板，独立镜像至 eBox-online 子仓库）
├── sync-subrepo.ps1            # 授权平台同步到子仓库的脚本（git subtree）
├── dist/                       # 分发产物（update.json 模板等）
├── docs/                       # 文档
├── eBox.sln                    # Visual Studio 解决方案
└── README.md
```

> 注：仓库根目录名 `2Box-master` 为历史命名，仅作克隆目录名，不影响构建与运行；代码内部已全部统一为 `eBox`。

---

## 构建指南

### 环境要求

- **Visual Studio 2022**（17.0+，需支持 C++20 Modules）
- **Windows SDK 10.0** 或更高
- **MSBuild**（随 VS 安装）
- 工作负载：**使用 C++ 的桌面开发**

### 构建步骤

1. 用 Visual Studio 2022 打开 `eBox.sln`
2. 选择配置（推荐 `Release | x64`）
3. 生成解决方案（`Ctrl+Shift+B`）

或使用命令行：

```powershell
# 定位 MSBuild
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -products * -requires Microsoft.Component.MSBuild `
    -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1

# 构建 Release|x64
& $msbuild "d:\Projects\2Box-master\eBox.sln" `
    /t:eBox /p:Configuration=Release /p:Platform=x64 /m /v:m
```

构建产物位于 `bin/x64/eBox.exe`。

### 配置说明

| 配置 | 平台 | 用途 |
|---|---|---|
| Debug | x64 | 开发调试 |
| Debug | x86 | 32 位调试 |
| **Release** | **x64** | **推荐发布配置** |
| Release | x86 | 32 位发布 |

### 关于激活码私钥（本地开发机构建必读）

为防止私钥泄露导致激活码可被伪造，仓库中 **已移除 ECDSA P-256 私钥**：

- [eBox/biz/license/License.cpp](eBox/biz/license/License.cpp) 中的私钥字节已用 `0xCC` 占位符替换
- 原始私钥备份在本地文件 `License.cpp.private.bak`（已被 `.gitignore` 忽略，不会推送）
- `tools/KeyGen/` 激活码生成工具整体被 `.gitignore` 忽略（含私钥，仅内部使用）

**本地开发机恢复激活功能与 KeyGen 签发能力步骤**：

1. 从本地备份恢复私钥（如备份文件 `License.cpp.private.bak` 仍在）：
   ```powershell
   Copy-Item "d:\Projects\2Box-master\eBox\biz\license\License.cpp.private.bak" `
             "d:\Projects\2Box-master\eBox\biz\license\License.cpp" -Force
   ```
2. 恢复 `tools/KeyGen/` 目录（从本地备份还原，不在仓库中）
3. 重新构建 `eBox.sln`，客户端即可正常验签激活码
4. 使用 `KeyGen.exe` 可签发新的激活码

> 拉取到新机器时，若无私钥备份，客户端仍可**验证**已有激活码（公钥内置），但**无法签发**新激活码。生产环境务必妥善保管 `License.cpp.private.bak` 与 `tools/KeyGen/`。

---

## 使用说明

### 图形界面

1. **启动**：双击 `eBox.exe`
2. **首次激活**：输入激活码（联系作者获取）
3. **启动程序**：
   - 点击左侧【启动进程】按钮选择可执行文件
   - 或直接将文件**拖动**到主窗口
4. **环境管理**：每个 .exe 自动选择无重名进程的环境运行；.lnk 快捷方式或 .url 只会选择空环境或新建环境

### 支持的文件类型

| 类型 | 行为 |
|---|---|
| `.exe` | 自动选择无重名进程的环境运行 |
| `.lnk` / `.url` 等关联可执行文件的后缀 | 选择空环境或新建环境运行（也可自行指定） |

### 使用事项

- 首次启动需输入激活码授权使用
- 授权到期前可在右上角【授权】中查看到期时间并续期
- 到期后可正常使用界面，但无法再启动环境
- 本软件支持多开同一程序互不干扰（如同时登录多个 QYWX）
- 每个环境拥有独立的配置、缓存与聊天数据
- 本软件只能简单地在环境之间隔离，不会阻止环境内进程访问环境外资源

---

## eBox-cli 命令行工具

### 简介

提供 CLI 工具主要为了帮助开发者使用命令行方式使用 eBox。注意，其**依赖 eBox，不能单独使用**，且必须与 `eBox.exe` 在同一目录。CLI 在必要的时候会自动隐式启动 eBox。

### 使用方法

```bash
eBox-cli.exe <target_program> [program_arguments...]
```

### 参数说明

- **`<target_program>`**（必需）- 要启动的目标程序路径（如果路径带空格，请用双引号包含完整路径）
- **`[program_arguments...]`**（可选）- 透传给目标程序的参数（如果某个参数带空格，也必须用双引号包含）

### 示例

```bash
# 启动 test.exe，不传递额外参数
eBox-cli.exe test.exe

# 启动 test.exe 并传递参数
eBox-cli.exe test.exe --config config.json --verbose

# 启动路径带空格，或参数带空格
eBox-cli.exe "C:\Program Files\My App\app.exe" --input "file name with spaces.txt"
```

### 退出码说明

#### 成功代码
- **`0`** - 成功：目标程序正常启动

#### 错误代码
- **`-1`** - 启动程序错误：无法启动指定的目标程序
- **`-2`** - 启动 eBox.exe 错误：特定的 eBox.exe 启动失败
- **`-3`** - 参数错误：未提供必需的参数或参数格式不正确
- **`-4`** - 参数解析错误：命令行参数解析失败

#### Windows API 错误代码
- **`正数`** - Windows API 调用失败时返回的系统错误码，具体含义请参考 [Microsoft 官方文档](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes)

---

## 授权服务平台（license-server）

> 位于 [license-server/](license-server/)，是 eBox 客户端的在线授权服务端：**激活码签发 / 设备绑定 / 心跳监控 / 换机 / 作废 / 公告发布 / 回收站**。
>
> 独立文档：[license-server/README.md](license-server/README.md)（含本地开发与宝塔部署）｜[docs/宝塔部署.md](docs/宝塔部署.md)
>
> 提示：本文档与部署文档中的 `<your-domain>` 为占位符，请替换为你的真实部署域名。

### 组成

| 端 | 技术 | 说明 |
|---|---|---|
| `license-server/backend` | Node 22+ / TypeScript / Express / TypeORM / MySQL 8 | 客户端接口（`/api/v1/*`）+ 管理接口（`/api/admin/*`） |
| `license-server/admin` | Vue 3 / Element Plus / Vite | 管理面板：激活码、批次、设备、心跳、日志、公告、回收站 |
| `license-server/deploy` | bash / PM2 / Nginx | 宝塔一键部署：`deploy.sh`（首次）/ `update.sh`（更新）/ `backup.sh`（备份） |

### 客户端对接

- 客户端默认连接 **`https://<your-domain>`**（Nginx 反代后端 3008）；换域名/本地联调可用注册表 `HKCU\Software\2Box\ServerUrl` 覆盖，免重新编译。
- 面板生成的**在线托管码**（格式版本 9）走 `/api/v1/activate` 激活并由服务端托管（心跳/作废/换机/离线宽限管控）；KeyGen 生成的**离线码**不受服务端管控，双轨互不干扰。

### 子仓库与同步

授权平台代码独立镜像到子仓库 **[shushuhao01/eBox-online](https://github.com/shushuhao01/eBox-online)**，供宝塔服务器 `git clone` 独立部署：

1. 主仓库提交（license-server 随主仓库正常提交）。
2. 根目录执行同步脚本，把最新 `license-server/` 覆盖式同步到子仓库：
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\sync-subrepo.ps1
   ```
3. 服务器更新：`cd /www/wwwroot/license-server && bash update.sh`（脚本自动拉代码+构建+重启）

> 同步基于 `git subtree split` + 强推，子仓库始终等于主仓库 `license-server/` 最新内容。

---

## 授权激活机制

### 激活码体系

eBox 采用**纯离线激活码体系**，无需联网即可激活：

- **算法**：ECDSA P-256 数字签名（不可伪造、不可篡改）
- **格式**：`EBOX-XXXXX-XXXXX-...`（Base32 编码）
- **载荷**：版本号 + 到期时间 + 指纹标志 + 机器指纹
- **验证**：客户端内置公钥验签，私钥仅在发码工具中（见上文「构建指南」恢复说明）

### 两种激活模式

| 模式 | 说明 | 解绑次数限制 |
|---|---|---|
| **绑定码** | 绑定单台机器，激活码可解绑后转移 | 每自然月可解绑 N 次（N 由发码工具配置） |
| **通用码** | 不绑定机器，可在任意机器激活 | 无需解绑 |

### 解绑规则

- 解绑按**自然月**计数，跨月自动重置
- 解绑次数存储于注册表 `HKCU\Software\eBox\UnbindCount`
- 解绑时若有其他 eBox 进程在运行，将被拒绝
- 本机解绑后再激活也计入当月解绑次数

### 授权信息查看

点击主窗口右上角【授权】按钮：
- 查看到期时间
- 复制激活码（一键复制到剪贴板）
- 解绑本机
- 联系客服 / 购买激活码

---

## 自动升级机制

### 升级流程

eBox 内置自动升级功能，**不强制更新**，仅在标题栏显示红点提示：

```
应用启动 → 8 秒后异步检查更新 → 有新版本则标题栏亮红点
                                          ↓
                                  用户点击红点入口
                                          ↓
                            弹窗显示版本号/changelog/大小
                                          ↓
                            用户点【立即更新】→ 二次确认
                                          ↓
                            后台下载到 %TEMP% → SHA-256 校验
                                          ↓
                            生成 updater.bat → 主进程退出
                                          ↓
                            bat 等待退出 → 覆盖 exe → 重启新版本
```

### 时效保证

| 场景 | 时效 |
|---|---|
| 启动后自动检查 | 8 秒内显示红点 |
| 常驻运行复检 | 每 6 小时一次 |
| 用户手动点击 | 立即检查（追加时间戳破除 CDN 缓存） |
| 发布新版本后 purge CDN | 5 分钟内全员可见 |

> 客户端拉取 manifest 时会自动追加 `?t=YYYYMMDDHH` 时间戳查询参数（精确到小时），破除 jsDelivr CDN 缓存，确保新版本及时触达。

### 数据保留

升级**完全不会丢失任何数据**：

| 数据 | 存储位置 | 升级后状态 |
|---|---|---|
| 激活码 | 注册表 `HKCU\Software\eBox\License` | ✅ 保留 |
| 解绑次数 | 注册表 `UnbindCount/Month` | ✅ 保留 |
| 环境列表 | 注册表 `Env\*` | ✅ 保留 |
| 环境数据目录 | `exeDir\Env` 或 `C:\eBoxData` | ✅ 保留（自动沿用） |

> 兼容老版本：从旧版 `2Box` 升级到 `eBox` 时，会自动回退读取 `HKCU\Software\2Box` 下的激活码/解绑次数/环境顺序，并沿用 `C:\2BoxData` 旧数据目录，**老用户平滑升级、数据不丢**。

### 回滚机制

- 升级前自动备份 `eBox.exe` → `eBox.exe.bak`
- 新版本启动成功后自动删除备份
- 若新版本启动失败，可手动重命名 `eBox.exe.bak` 为 `eBox.exe` 回滚

---

## 数据存储与隐私

### 数据存储位置

| 数据类别 | 位置 |
|---|---|
| **激活信息** | 注册表 `HKEY_CURRENT_USER\Software\eBox` |
| **环境元数据** | 注册表 `HKEY_CURRENT_USER\Software\eBox\Env` |
| **环境数据目录** | 默认 `C:\eBoxData\Env\`（自动选择剩余空间最大的盘） |
| **升级状态** | 注册表 `LastUpdateCheck` / `IgnoredVersionCode` 等 |

### 环境数据目录选择策略

1. 注册表已有记录且目录可写 → 直接使用（保证多次启动一致）
2. **升级兼容**：exe 目录下已存在 Env 数据 → 沿用原目录（避免老数据"丢失"）
3. **兼容老版本**：旧目录 `C:\2BoxData` / `D:\2BoxData` 已有 Env 数据则直接沿用
4. 全新部署：默认 `C:\eBoxData`（C 盘不可用则 `D:\eBoxData`）
5. 兜底：exe 目录

### 隐私说明

- eBox **不收集任何用户数据**
- 激活码体系**完全离线**，不与服务器通信
- 升级检查仅请求 manifest 文件（1KB JSON），不含任何用户标识
- 机器指纹仅用于绑定激活码，不上传任何服务器

---

## 版本发布与分发

本仓库同时承担**版本分发**职责：客户端启动时通过 jsDelivr CDN 拉取本仓库的 `dist/update.json` 检查更新。

### 分发链路

```
GitHub 仓库 dist/update.json
        ↓
jsDelivr CDN（https://cdn.jsdelivr.net/gh/shushuhao01/ebox@main/dist/update.json）
        ↓
客户端拉取（追加 ?t=时间戳 破缓存）
        ↓
版本比较 → 红点提示 → 下载升级包 → SHA-256 校验 → 自动安装
```

### 发布新版本流程

1. 修改 [eBox/app/MainApp.ixx](eBox/app/MainApp.ixx) 中的版本号常量（`kVerMajor`/`kVerMinor`/`kVerPatch`/`kVerCode`）
2. 同步更新 [eBox/res/eBox.rc](eBox/res/eBox.rc) 中的 `FILEVERSION`/`PRODUCTVERSION`
3. 构建 Release|x64，得到 `bin/x64/eBox.exe`
4. 计算新 exe 的 SHA-256：
   ```powershell
   Get-FileHash "bin\x64\eBox.exe" -Algorithm SHA256
   ```
5. 更新 `dist/update.json`（字段含义见 [dist/update.json](dist/update.json) 模板）
6. 将新版 `eBox.exe` 上传到下载地址（`downloadUrl` 指向的位置，可用 GitHub Releases / 对象存储等）
7. 提交并推送 `dist/update.json` 与源码改动到 GitHub
8. 访问以下 URL 手动刷新 jsDelivr CDN 缓存（5 分钟内全员生效）：
   ```
   https://purge.jsdelivr.net/gh/shushuhao01/ebox@main/dist/update.json
   ```

### update.json 字段说明

| 字段 | 类型 | 说明 |
|---|---|---|
| `latestVersion` | string | 版本号字符串，如 `"v2.7.0"` |
| `latestVersionCode` | int | 单调递增数字，用于版本比较（如 `20700`） |
| `releaseDate` | string | 发布日期，如 `"2026/8/7"` |
| `downloadUrl` | string | 升级包下载地址（HTTPS 推荐） |
| `downloadSha256` | string | 升级包 SHA-256（小写十六进制），客户端据此校验完整性 |
| `downloadSize` | int | 升级包字节数，用于界面展示 |
| `changelog` | string[] | 更新日志，每项一行 |
| `minSkipVersionCode` | int | 允许"跳过此版本"的最小版本码；低于此值强制升级（一般留 0） |
| `forceUpdate` | bool | 是否强制升级（true 时隐藏"跳过"按钮） |

---

## 开发者文档

### 版本号管理

版本号在 [eBox/app/MainApp.ixx](eBox/app/MainApp.ixx) 中集中定义：

```cpp
static constexpr int kVerMajor = 2;     // 主版本号
static constexpr int kVerMinor = 7;     // 次版本号
static constexpr int kVerPatch = 0;     // 修订号
static constexpr int kVerCode  = 20700; // 单调递增数字，用于版本比较
```

发布新版本时需同步更新此四处定义与 `.rc` 中的 `FILEVERSION`/`PRODUCTVERSION`。

### 模块依赖关系

```
MainApp (顶层)
    ↓
biz.Update ← biz.WinHttp ← common.coroutine
    ↓
biz.License ← bcrypt
    ↓
biz.Env ← biz.Env-Reg
    ↓
UI.MainWindow ← UI.Core ← common
    ↓
MemoryDll (注入层，独立)
```

---

## 许可证

本项目基于 [GNU General Public License v3.0](LICENSE) 开源。

- 任何使用、修改、分发需遵守 GPL v3 条款
- 商业使用请联系作者获取授权

---

## 致谢

- [Microsoft Detours](https://github.com/microsoft/Detours) - API Hook 库
- [WinHTTP](https://learn.microsoft.com/windows/win32/winhttp) - Windows HTTP 服务
- [Direct2D / DirectWrite](https://learn.microsoft.com/windows/win32/direct2d/direct2d-portal) - 图形渲染
- [BCrypt](https://learn.microsoft.com/windows/win32/seccng/bcrypt-reference) - 加密原语
- [jsDelivr](https://www.jsdelivr.com/) - 开源 CDN，用于版本分发加速

---

## 反馈与支持

- **问题反馈**：[GitHub Issues](https://github.com/shushuhao01/ebox/issues)
