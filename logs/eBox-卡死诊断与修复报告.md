# eBox 启动/运行卡死（未响应）诊断与修复报告

- 日期：2026-08-27
- 涉及版本：v2.9.9（PDB GUID age=9，含锁重构）
- 现象：eBox 环境卡片启动/运行过程中，窗口标题出现「(未响应)」，UI 无法操作。
- 分析对象：`logs/eBox-hang-20260827-205625.dmp`（75 线程，关键 dump）

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

## 六、压测验证结果（2026-08-28，已有数据）

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

## 七、待验证 / 后续步骤

1. 重新编译 Release | x64，将版本号升到下一版（建议 v3.0.0）。
2. 推送仓库并发布新版本。
3. 用新版本复现「启动/运行环境卡片」场景，确认不再出现「未响应」。
4. 建议保留 `logs/` 下的 dump 与本报告，作为后续回归对照。

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
