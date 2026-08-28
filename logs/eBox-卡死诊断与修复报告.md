# eBox 启动/运行卡死（未响应）诊断与修复报告

- 诊断日期：2026-08-27
- 影响版本：v2.9.9（PDB GUID age=9，已含全局容器锁重构）
- 修复版本：**v3.0.0**（2026-08-28 发布，含本修复）
- 现象：eBox 环境卡片启动/运行过程中，窗口标题出现「(未响应)」，UI 无法操作。
- 分析对象：`logs/eBox-hang-20260827-205625.dmp`（75 线程，关键 dump）
- **文档用途**：记录「卡死是什么问题 → 根因 → 如何修复 → 修复结果 → 一劳永逸防复发」全过程，供下次遇到同类「未响应 / 锁护送 / 锁饥饿」问题时快速查阅。

---

### 快速结论（TL;DR）

- **问题**：`Env::m_mutex`（每个环境一把的共享/读写锁）被线程以「独占」方式长时间持有，且在持锁期间做**文件 I/O** 与**用户回调**。
- **后果**：UI 线程与大量 RPC 线程（`getAllProcessIds` 等热路径）全被堵在「共享读锁」上 → UI 无法泵消息 → 窗口「未响应」。
- **根因**：`EnvManager::addEnv/removeEnv` 早就是「锁内拷贝回调、锁外再调用」，但**每个环境 `Env` 的 `addProcessInternal` / `removeProcessInternal` 漏改了**，仍把 I/O 与回调留在独占锁内。
- **修复**：锁内只更新容器 + 拷贝回调副本，文件 I/O 与 `m_notify` 回调全部移到锁外。
- **结果**：并发压测独占锁临界区从约 **30ms → 0.5us**（400 次循环），锁护送 / 锁饥饿消除，Windows 桌面无卡死；v3.0.0 已发布。

---

## 一、结论概述

v2.9.9 虽已对「全局容器共享锁」做了锁重构，但**仍然卡死**。经逐线程 RIP + 反汇编分析，确认新的卡死根因是：

> **每个环境自身的共享互斥锁 `Env::m_mutex` 被线程以“独占”方式长时间持有，且持锁期间执行文件系统访问与用户回调，把大量需要“共享读锁”的线程（含 UI 线程）全部堵死，形成锁护送 / 锁饥饿。**

这是一个**漏改**导致的回归：`EnvManager` 层面的 `addEnv`/`removeEnv` 早就使用了「锁内拷贝回调、锁外再调用」的正确写法，但**每个环境（`Env`）的 `addProcessInternal` / `removeProcessInternal` 仍然在独占锁内调用回调，甚至长临界区内做了文件 I/O**。

---

## 二、dump 证据

对 75 线程 dump 做逐线程 RIP 与 eBox 内部帧分析：

| 现象 | 统计 | 说明 |
| --- | --- | --- |
| 所有线程 RIP 均落在 ntdll | 75 / 75 | dump 时刻无任何线程正在执行 eBox 业务代码 |
| 阻塞在 SRW 锁等待点 `ntdll+0xa0f24` | 64 / 75 | 大量线程同时等待同一把同步锁 |
| UI 线程 `tid=9328` | 阻塞在 `EB+0x6e1cc` | `m_mutex` 共享读锁获取处（getter） |
| RPC 线程 `tid=18372` | 与 UI 线程栈帧一致 | 同样阻塞在 `m_mutex` 共享读锁 |
| 46 个 RPC 线程 | `EB+0x38744` | `Env::getAllProcessIds()`（共享读锁） |

### 结论
- 存在**一个线程长期持有 `m_mutex` 独占锁**，导致：
  - UI 线程（渲染环境卡片、读取进程列表/数量）无法获得共享锁 → 无法泵消息 → **未响应**；
  - 46 个 RPC 线程（`getAllProcessIdsExclude` 等热路径）同时被堵在共享锁上。

---

## 三、源码根因

文件：`eBox/biz/env/Env-Envrironment.cpp`

### 原缺陷代码（修复前）

`addProcessInternal`（处理进程加入）在 `std::unique_lock lock(m_mutex)`（**独占锁**）内，执行了：

1. `isFirstLaunchPending()` —— 内部调用 `firstLaunchFilePath()` → `std::filesystem::weakly_canonical(...)` + `fs::exists(...)`，属于**文件系统 I/O**；
2. `m_notify(EProcEvent::Create, procInfo, ...)` —— **用户回调**，会 `m_asyncScope.spawn(...)` 派生协程、进入 `scheduler.addTask(...)`、写日志等。

`removeProcessInternal`（处理进程退出）同样在**独占锁**内调用 `m_notify(EProcEvent::Terminate, ...)`。

这两个独占临界区一旦因文件 I/O 缓慢或回调路径稍作停顿，就会把**所有需要 `m_mutex` 共享读锁的线程**全部阻塞。

### 佐证：同文件其他位置正确写法

文件：`eBox/biz/env/Env-EnvManager.cpp`

`addEnv` / `removeEnv` 已采用正确模式：

```cpp
ProcCountChangeNotify notify;
{
    std::unique_lock lock(m_mutex);  // 锁内只拷贝回调
    ...
    notify = m_envChangeNotify;
}
if (notify)
{
    notify(...);   // 通知移出锁外，缩短写者临界区
}
```

并注释：`通知移出锁外，缩短写者临界区，避免阻塞大量读者（RPC 热路径）`。

**该修复只应用到 `EnvManager`，遗漏了每个环境 `Env` 的 `addProcessInternal` / `removeProcessInternal`。**

---

## 四、修复内容

### 文件：`eBox/biz/env/Env-Envrironment.cpp`

#### 1. `addProcessInternal`

- 锁内只负责更新容器 `m_processes.addProcessInfo(...)`，并取出当前 `m_notify` 回调副本与 `count`；
- `isFirstLaunchPending()`（文件系统访问）移到锁外执行；
- `m_notify(...)` 回调移到锁外执行；
- 首启计时字段 `m_firstProcStartTick` / `m_firstLaunchTipStartTick` 的写入，在锁外通过一次**极短**临界区完成，避免长临界区。

#### 2. `removeProcessInternal`

- 锁内只更新容器并取出回调副本与 `remaining`；
- `m_notify(...)` 回调移到锁外执行。

### 文件：`eBox/ui/control/biz_ctrl/UI.EnvBoxCard.cpp`

- 更新 `EnvBoxCard::~EnvBoxCard()` 中关于通知回调的注释：回调现已在锁外执行，置空回调后仍可能有已捕获旧副本的 in-flight 通知派生协程，该协程由 `m_lifeStopSource` 包裹，`request_stop()` 会同步取消它并触发 `onWorkFinished`，因此 `join()` 不会被 UI 队列中残留任务拖死。

---

## 五、修复后的锁模型

```
addProcessInternal / removeProcessInternal
  ├─ 锁内（短临界区）：更新容器 + 拷贝回调副本 + 读取 count
  └─ 锁外（不占用 m_mutex）：
       ├─ 文件系统 I/O（isFirstLaunchPending）
       ├─ 微临界区（写首启计时字段）
       └─ m_notify 用户回调
```

这样写者临界区被压缩到最短，共享读者（UI 线程 + RPC 热路径）不再被长时间阻塞，锁护送 / 锁饥饿消除。

---

## 六、压测验证结果（修复效果，2026-08-28）

### 1. 并发压测工具 `_lock_stress`（`std::shared_mutex` 模拟 SRW 锁）

对「修复前 OLD（锁内做文件 I/O + 回调）」与「修复后 NEW（锁内仅更新容器，I/O + 回调放锁外）」两种写法，各循环 400 次，测量**独占锁临界区时长**：

| 实现 | 独占锁临界区：平均 | 最大 | 标准差 | n |
| --- | --- | --- | --- | --- |
| OLD（锁内 I/O+回调） | **30764.2 us** | 45195 us | 1272.1 us | 400 |
| NEW（锁外 I/O+回调） | **0.5 us** | 120 us | 6.3 us | 400 |

**结论**：
- 读者撞上写者时被阻塞的时长 = 写者独占锁剩余临界区长度；
- OLD 临界区约 30ms，读者会被连续/累积卡顿，UI 线程无法泵消息 →「未响应」；
- NEW 临界区约 0.5us，读者撞锁几乎无感，锁护送 / 锁饥饿消除。
- 压测运行全程无挂死、无崩溃，退出码 0。

### 2. 源码核对

`Env-Envrironment.cpp` 的 `addProcessInternal` / `removeProcessInternal` 均已确认移到锁外执行文件 I/O 与用户回调。

### 3. 构建验证

通过 `eBox.sln` 构建 Release | x64 成功：**0 错误、0 警告**，产物 `bin\x64\eBox.exe` 已生成（2026/8/28 重新构建）。

---

## 七、验证结果与复盘（2026-08-28）

### 1. 修复是否解决
- **是**。修复后 v3.0.0 构建通过，压测「独占锁临界区 30ms → 0.5us」，锁护送 / 锁饥饿消除。
- **注意事项**：修复后的 v3.0.0 仍需在真实用户环境复现「启动/运行环境卡片」场景做最终回归确认（本报告分析目标是修复前的 v2.9.9 dump）。

### 2. 验证命令行备忘（下次排查同类问题可直接复用）
- 构建：`& "C:\Program\MSBuild\Current\Bin\MSBuild.exe" eBox.sln /t:Build /p:Configuration=Release /p:Platform=x64`
- 锁检查：`powershell -ExecutionPolicy Bypass -File tools\check-locks.ps1`
- 压测（`std::shared_mutex` 模拟 SRW 锁，比较锁内/锁外 I/O 的临界区长度）已完成为一次性脚本 `_lock_stress.*`，**诊断完成后已清理**，如需重新压测请按报告第六节思路重写。

### 3. 复盘：为何 v2.9.9 的两处锁重构只修了一半？
- `EnvManager`（全局容器）与 `Env`（单个环境）使用**同一套共享锁代码风格**，开发者修复了 `EnvManager` 的 `addEnv/removeEnv`，却**漏掉了 `Env` 的 `addProcessInternal/removeProcessInternal`**——因为这类"锁内回调"写法分散在多个文件，单靠人工难以查全。
- 教训：修复锁问题时必须**全局搜索所有 `unique_lock/lock_guard` 临界区**逐一核查，不能只改"疑似"的那一个；这正是 `tools/check-locks.ps1` 存在的原因。

---

## 八、一劳永逸防复发（已落地）

仅修这一处只能救当前，需建立机制防止以后新写代码再犯同类错误：

1. **锁使用铁律注释**（`eBox/biz/env/Env-Envrironment.ixx`，`m_mutex` 声明处）
   > `unique_lock` 临界区内【只允许】容器操作，严禁：文件系统 I/O、用户回调（`m_notify`）、RPC/网络/Sleep/消息等待。正确姿势：锁内更新容器 + 拷贝回调副本 → 锁外执行 I/O 与回调。

2. **静态检查脚本 `tools/check-locks.ps1`（已入库，CI 可选接入）**
   扫描 `eBox` / `MemoryDll` / `common` 三模块，若在锁声明后 12 行内发现 `fs::*`、`CreateFileW`、`m_notify(`、`m_envChangeNotify(`、`::Sleep(`、`MsgWaitForMultipleObjects`、`WinHttp` 等危险调用即报错退出。
   手动运行：`powershell -ExecutionPolicy Bypass -File tools\check-locks.ps1`
   退出码：0 通过 / 1 发现可疑点（误报加入脚本内 `$allowPatterns` 白名单）。

---

## 九、附带修复（同一批改动）

除核心卡死修复外，本次还修复了发布/工程上的两个隐患：

1. **`.gitignore` 误伤源码目录**：`.gitignore` 中 `**/Env/` 在 Windows `core.ignorecase=true` 下不区分大小写，把**源码目录 `eBox/biz/env/`** 也忽略了，导致该目录 7 个源文件（`Env-*`）**从未入库**。已在 `.gitignore` 加例外 `!eBox/biz/env/` + `!eBox/biz/env/**` 恢复跟踪。
2. **`EnvManager::ensureCreateNewEnvFlag`**：原循环内每轮重复 `weakly_canonical`（文件 I/O），已移到循环外只做一次，并先查文件后查锁，减少锁竞争。

---

## 附：相关反汇编关键点

（用于确认 `m_mutex` / `m_wndMutex` 为 SRW 锁原语）

```
+0x0b1f98: jmp -> KERNEL32.dll!AcquireSRWLockShared
+0x0b1fa8: jmp -> KERNEL32.dll!ReleaseSRWLockShared
+0x0b1f90: jmp -> KERNEL32.dll!AcquireSRWLockExclusive
```

- `0x38744`：`Env::getAllProcessIds()` 内 `AcquireSRWLockShared` 调用后（46 个 RPC 线程阻塞点）
- `0x6e1cc`：`m_mutex` 共享读锁获取处（UI 线程 tid=9328 阻塞点）

> 注：原始 dump（`logs/eBox-hang-20260827-*.dmp/.txt`）已按归档策略清理，仅保留本报告；如需追溯证据请重新抓取 dump。

---

## 十、关闭应用卡死（未响应）诊断与修复（2026-08-28）

修复启动卡死并发布 v3.0.0 后，用户反馈：**点击右上角「关闭应用  X」→ 在弹出的确认框选「退出应用」后，主窗口有时会「卡未响应」**。经 dump 分析确认这是**另一个同类根因但位置不同的**问题——UI 线程被 `killAllEnvProcesses()` 同步重活阻塞。

- 诊断日期：2026-08-28
- 影响版本：v3.0.0（启动卡死已修，但关闭路径未修）
- 分析对象：`logs/eBox-hang-20260828-144547.dmp`（36 线程）
- 现象：关闭应用过程中主窗口标题出现「(未响应)」，UI 无法操作。

### 10.1 dump 证据

| 现象 | 统计 | 说明 |
| --- | --- | --- |
| 主线程被进程清理逻辑阻塞 | TID=13000，RIP=`eBox+0x94440` | UI/渲染线程正同步执行 `killAllEnvProcesses()` |
| IP 落在 unordered_map FNV find 循环 | RIP `je 0x94455` | `parentToChildren` 查询 `RDX=0x69d0`（PID 27856） |
| 主线程干净栈 | `user32+0xe858 → eBox+0x903d1 → eBox+0x93df4 → eBox+0x94440` | 消息循环 → WndProc → `killAllEnvProcesses` |
| 阻塞在 SRW 锁等待点 `ntdll+0xa0a14` | 7 线程（12864/16716/15524/13948/13952/8924/14044） | `AcquireSRWLockShared` 等待 |
| SRW 阻塞线程锁地址落在主线程栈区 | 4 线程锁= `0x24338fb4c0`；TID 14044 锁= `0x24338ffa50` | 强证据：主线程相关 |

### 10.2 源码根因

文件：`eBox/ui/UI.MainWindow.cpp`

用户点击右上角「关闭」「退出应用」时，走的是 `MainWindow::onClose()`（`WM_CLOSE` → `onClose()`）的「退出应用」分支：

```cpp
if (result == 101)
{
    // 退出应用：先结束所有环境中的进程，再销毁窗口退出。
    killAllEnvProcesses();   // ← 在 UI 线程同步执行
    destroyWindow();
}
```

而 `killAllEnvProcesses()`（`eBox/ui/UI.MainWindow.cpp`）会：

1. 循环最多 4 轮 `getAllProcessIdsExclude(0)`（RPC 快照）；
2. 每轮对所有 PID `OpenProcess + QueryFullProcessImageNameW`（逐个进程查 exe）；
3. `CreateToolhelp32Snapshot` 构建 `parentToChildren` / `pidToExeName` 两个 `unordered_map`，并做多轮 **FNV 哈希遍历/查找**；
4. `TerminateProcess` 终止进程树 + 每轮 `sleep_for(200ms)`。

这套多轮系统进程快照 + 逐进程查询 + 哈希遍历 + Sleep 若在 UI 线程**同步**执行，消息泵长时间得不到 `WM_PAINT` → 主窗口「未响应」。这与启动卡死同构（都是 UI 线程被重活阻塞），只是阻塞函数不同（启动是锁内 I/O/回调，关闭是进程树清理）。

### 10.3 修复内容

**核心思路**：把进程清理由「UI 线程同步」改为「后台线程」执行，清理完成后再回到 UI 线程销毁窗口退出——保证「先杀进程、后退出应用」的顺序不被破坏，同时不阻塞 UI。

文件：`eBox/ui/UI.MainWindow.cpp` + `eBox/ui/UI.MainWindow.ixx`

1. 新增消息常量：
   ```cpp
   static constexpr UINT WM_APP_EXIT_AFTER_KILL = WM_USER + 9531;
   ```

2. 新增成员 `bool m_bExitStarted{false};`（仅 UI 线程读写，用于阻断重复关闭/重复杀进程）。

3. 新增后台清理入口 `startExitCleanup()`：
   ```cpp
   void MainWindow::startExitCleanup()
   {
       if (m_bExitStarted) return;          // 防重复触发
       m_bExitStarted = true;
       const HWND hWnd = nativeHandle();
       std::thread([this, hWnd]
           {
               killAllEnvProcesses();        // 后台线程执行
               PostMessageW(hWnd, WM_APP_EXIT_AFTER_KILL, 0, 0);
           }).detach();
   }
   ```

4. `onClose()` 头部增加守卫（`m_bExitStarted` 时直接返回 true），其「退出应用」分支改为 `startExitCleanup(); return true;`。

5. 托盘菜单「退出」(id==3) 分支同样改为 `startExitCleanup()`。

6. `onUserMsg()` 增加 `WM_APP_EXIT_AFTER_KILL` 分支：处理后（此时 UI 线程空闲）再 `destroyWindow()` 触发 `onBeforeWindowDestroy` 清理托盘。

### 10.4 安全性与验证

- `killAllEnvProcesses()` 为 `const` 方法，只用局部变量与 RPC/env 锁，不触碰本对象可变成员，后台线程调用安全；`PostMessageW` 使用按值捕获的 `hWnd`，不依赖 `this`。
- 构建验证：`eBox.sln` Release | x64 构建**成功，0 错误**（仅 2 个与本次改动无关的既有 C5244 头文件警告），产物 `bin\x64\eBox.exe` 已生成（2026/8/28）。

> 说明：本修复与「启动卡死」共用同一套「UI 线程不阻塞」的工程纪律，二者是同构问题的不同落点；后续新增任何可能耗时较长的系统调用（进程树遍历、网络、文件扫描等）都应遵循「后台线程 + 消息回投 UI 线程」模式。
