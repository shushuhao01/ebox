# 2Box

> 轻量级 Windows 软件**多实例运行**工具，尽可能不影响软件原有功能的前提下实现多开隔离。

许多软件会阻止自身多实例运行（如企业微信、钉钉、微信等），这通常是为了简化逻辑或因为业务上并无必要。但在某些场景下，我们仍希望同时启动多个实例以满足特定需求（多账号同时登录、测试环境隔离等）。

2Box 通过 **PE 内存加载 + API Hook + 路径重定向** 的方式，让每个实例拥有独立的配置、缓存与聊天数据目录，实现真正的多开互不干扰。

---

## 交流社区

https://kook.vip/8z8C9U

---

## 目录

- [核心功能](#核心功能)
- [技术架构](#技术架构)
- [项目结构](#项目结构)
- [构建指南](#构建指南)
- [使用说明](#使用说明)
- [2Box-cli 命令行工具](#2box-cli-命令行工具)
- [授权激活机制](#授权激活机制)
- [自动升级机制](#自动升级机制)
- [数据存储与隐私](#数据存储与隐私)
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
| **多账号支持** | 同时登录多个企业微信等客户端，方便多账号管理 |

---

## 技术架构

### 模块组成

```
┌─────────────────────────────────────────────────────┐
│                   2Box.exe (主程序)                  │
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
| MainApp | [2Box/app/MainApp.ixx](2Box/app/MainApp.ixx) | 应用入口、全局配置、版本号单一可信源 |
| UI.MainWindow | [2Box/ui/UI.MainWindow.cpp](2Box/ui/UI.MainWindow.cpp) | 主窗口、标题栏自绘、拖拽启动 |
| biz.License | [2Box/biz/license/License.cpp](2Box/biz/license/License.cpp) | 离线激活码签发与验证、解绑次数统计 |
| biz.Update | [2Box/biz/update/Update.cpp](2Box/biz/update/Update.cpp) | 在线升级（manifest 检查/下载/SHA-256 校验/一键安装） |
| biz.Env | [2Box/biz/env/Env-EnvManager.cpp](2Box/biz/env/Env-EnvManager.cpp) | 环境管理、进程-环境映射 |
| biz.WinHttp | [2Box/biz/http/WinHttp.ixx](2Box/biz/http/WinHttp.ixx) | 异步 HTTP 客户端 |
| MemoryDll | [MemoryDll/](MemoryDll/) | 注入到目标进程的 DLL，Hook 各类 API |
| common | [common/](common/) | 协程、调度器、PE 加载器等基础设施 |

---

## 项目结构

```
2Box-master/
├── 2Box/                      # 主程序源码
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
│   └── 2Box.vcxproj
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
├── DevTools/2Box-cli/          # 命令行工具
├── tools/KeyGen/               # 激活码生成工具（含私钥，仅内部使用）
├── docs/                       # 文档
├── 2Box.sln                    # Visual Studio 解决方案
└── README.md
```

---

## 构建指南

### 环境要求

- **Visual Studio 2022**（17.0+，需支持 C++20 Modules）
- **Windows SDK 10.0** 或更高
- **MSBuild**（随 VS 安装）
- 工作负载：**使用 C++ 的桌面开发**

### 构建步骤

1. 用 Visual Studio 2022 打开 `2Box.sln`
2. 选择配置（推荐 `Release | x64`）
3. 生成解决方案（`Ctrl+Shift+B`）

或使用命令行：

```powershell
# 定位 MSBuild
$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -products * -requires Microsoft.Component.MSBuild `
    -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1

# 构建 Release|x64
& $msbuild "d:\Projects\2Box-master\2Box.sln" `
    /t:2Box /p:Configuration=Release /p:Platform=x64 /m /v:m
```

构建产物位于 `bin/x64/2Box.exe`。

### 配置说明

| 配置 | 平台 | 用途 |
|---|---|---|
| Debug | x64 | 开发调试 |
| Debug | x86 | 32 位调试 |
| **Release** | **x64** | **推荐发布配置** |
| Release | x86 | 32 位发布 |

---

## 使用说明

### 图形界面

1. **启动**：双击 `2Box.exe`
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
- 本软件支持多开同一程序互不干扰（如同时登录多个企业微信）
- 每个环境拥有独立的配置、缓存与聊天数据
- 本软件只能简单地在环境之间隔离，不会阻止环境内进程访问环境外资源

---

## 2Box-cli 命令行工具

### 简介

提供 CLI 工具主要为了帮助开发者使用命令行方式使用 2Box。注意，其**依赖 2Box，不能单独使用**，且必须与 `2Box.exe` 在同一目录。CLI 在必要的时候会自动隐式启动 2Box。

### 使用方法

```bash
2Box-cli.exe <target_program> [program_arguments...]
```

### 参数说明

- **`<target_program>`**（必需）- 要启动的目标程序路径（如果路径带空格，请用双引号包含完整路径）
- **`[program_arguments...]`**（可选）- 透传给目标程序的参数（如果某个参数带空格，也必须用双引号包含）

### 示例

```bash
# 启动 test.exe，不传递额外参数
2Box-cli.exe test.exe

# 启动 test.exe 并传递参数
2Box-cli.exe test.exe --config config.json --verbose

# 启动路径带空格，或参数带空格
2Box-cli.exe "C:\Program Files\My App\app.exe" --input "file name with spaces.txt"
```

### 退出码说明

#### 成功代码
- **`0`** - 成功：目标程序正常启动

#### 错误代码
- **`-1`** - 启动程序错误：无法启动指定的目标程序
- **`-2`** - 启动 2box.exe 错误：特定的 2box.exe 启动失败
- **`-3`** - 参数错误：未提供必需的参数或参数格式不正确
- **`-4`** - 参数解析错误：命令行参数解析失败

#### Windows API 错误代码
- **`正数`** - Windows API 调用失败时返回的系统错误码，具体含义请参考 [Microsoft 官方文档](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes)

---

## 授权激活机制

### 激活码体系

2Box 采用**纯离线激活码体系**，无需联网即可激活：

- **算法**：ECDSA P-256 数字签名（不可伪造、不可篡改）
- **格式**：`2BOX-XXXXX-XXXXX-...`（Base32 编码）
- **载荷**：版本号 + 到期时间 + 指纹标志 + 机器指纹
- **验证**：客户端内置公钥验签，私钥仅在发码工具中

### 两种激活模式

| 模式 | 说明 | 解绑次数限制 |
|---|---|---|
| **绑定码** | 绑定单台机器，激活码可解绑后转移 | 每自然月可解绑 N 次（N 由发码工具配置） |
| **通用码** | 不绑定机器，可在任意机器激活 | 无需解绑 |

### 解绑规则

- 解绑按**自然月**计数，跨月自动重置
- 解绑次数存储于注册表 `HKCU\Software\2Box\UnbindCount`
- 解绑时若有其他 2Box 进程在运行，将被拒绝
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

2Box 内置自动升级功能，**不强制更新**，仅在标题栏显示红点提示：

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
| 用户手动点击 | 立即检查（绕过缓存） |
| 发布新版本后 purge CDN | 5 分钟内全员可见 |

### 数据保留

升级**完全不会丢失任何数据**：

| 数据 | 存储位置 | 升级后状态 |
|---|---|---|
| 激活码 | 注册表 `HKCU\Software\2Box\License` | ✅ 保留 |
| 解绑次数 | 注册表 `UnbindCount/Month` | ✅ 保留 |
| 环境列表 | 注册表 `Env\*` | ✅ 保留 |
| 环境数据目录 | `exeDir\Env` 或 `C:\2BoxData` | ✅ 保留（自动沿用） |

### 回滚机制

- 升级前自动备份 `2Box.exe` → `2Box.exe.bak`
- 新版本启动成功后自动删除备份
- 若新版本启动失败，可手动重命名 `2Box.exe.bak` 为 `2Box.exe` 回滚

---

## 数据存储与隐私

### 数据存储位置

| 数据类别 | 位置 |
|---|---|
| **激活信息** | 注册表 `HKEY_CURRENT_USER\Software\2Box` |
| **环境元数据** | 注册表 `HKEY_CURRENT_USER\Software\2Box\Env` |
| **环境数据目录** | 默认 `C:\2BoxData\Env\`（自动选择剩余空间最大的盘） |
| **升级状态** | 注册表 `LastUpdateCheck` / `IgnoredVersionCode` 等 |

### 环境数据目录选择策略

1. 注册表已有记录且目录可写 → 直接使用（保证多次启动一致）
2. **升级兼容**：exe 目录下已存在 Env 数据 → 沿用原目录（避免老数据"丢失"）
3. 全新部署：默认 `C:\2BoxData`（C 盘不可用则 `D:\2BoxData`）
4. 兜底：exe 目录

### 隐私说明

- 2Box **不收集任何用户数据**
- 激活码体系**完全离线**，不与服务器通信
- 升级检查仅请求 manifest 文件（1KB JSON），不含任何用户标识
- 机器指纹仅用于绑定激活码，不上传任何服务器

---

## 开发者文档

### 版本号管理

版本号在 [2Box/app/MainApp.ixx](2Box/app/MainApp.ixx) 中集中定义：

```cpp
static constexpr int kVerMajor = 2;     // 主版本号
static constexpr int kVerMinor = 7;     // 次版本号
static constexpr int kVerPatch = 0;     // 修订号
static constexpr int kVerCode  = 20700; // 单调递增数字，用于版本比较
```

发布新版本时需同步更新此四处定义。

### 发布新版本流程

1. 修改 `MainApp.ixx` 中的版本号常量
2. 构建 Release|x64
3. 计算新 exe 的 SHA-256
4. 更新分发仓库的 `update.json`
5. 推送分发仓库到 GitHub
6. 访问 `https://purge.jsdelivr.net/gh/.../update.json` 刷新 CDN 缓存

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

---

## 反馈与支持

- **问题反馈**：[GitHub Issues](https://github.com/shushuhao01/ebox/issues)
- **交流社区**：https://kook.vip/8z8C9U
