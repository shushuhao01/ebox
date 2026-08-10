# DESIGN.md — eBox 沙箱多开工具使用指南

## 画布与母版（A/B/C 三区）

- 画布：1280 × 720 px；页面 padding：上下 20px、左右 64px。
- **A · 标题块**：0–120px。左侧 6px 主色竖条 + 主标题 34px bold（#1E293B）+ 副标题 16px（#64748B）。标题块右侧不塞装饰小图。
- **B · 内容区**：120–660px（可用 540px）。一切正文/图/卡片。
- **C · 页脚条**：660–720px。左侧 "eBox 使用指南" 14px #94A3B8 + 右侧页码 `NN / 19` 14px #94A3B8。封面与结束页省略 C 区。

## 颜色系统（4 hex）

| 角色 | hex | 用途 |
|---|---|---|
| 主色 | `#2563EB` | 标题竖条、按钮、编号标记、强调文字、流程箭头 |
| 辅色 | `#06B6D4` | 次级标记、图表第二系列、渐变收尾色 |
| 强调色 | `#F59E0B` | 警示条、关键提醒、Hero 页焦点（≤10%，Hero 可到 15%） |
| 文本主色 | `#1E293B` | 标题与正文（辅助灰 #64748B / #94A3B8 为其同族中性色） |

- 背景：#FFFFFF；卡片底：#F8FAFC、rgba(37,99,235,0.06)；深色页（封面/结束/P9）用 `linear-gradient(135deg, #1E3A8A 0%, #2563EB 60%, #06B6D4 100%)`。
- 渐变方案：135deg 主色→辅色，用于深色 Hero 页背景与编号标记；半透明：浅色卡片用 rgba(37,99,235,0.06~0.10)，警示条用 rgba(245,158,11,0.12)。
- 色彩面积：常规页 主色≤25% / 辅色≤15% / 强调色≤5%；深色 Hero 页（P1/P9/P19）主色（蓝系）可达 60%。

## 字体系统

- 中文标题/正文统一 `Microsoft YaHei`（雅黑），层级靠字重与字号拉开：
  - 封面主标题 64px bold / 深色页大字 56–72px bold
  - 巨型锚点数字 96px 900（与正文字重强对比）
  - 页面主标题 34px bold / 卡片小标题 22px 600
  - 正文 16–18px regular，行高 1.55 / 脚注页码 14px
  - 西文/数字搭配 Arial

## 配图清单（全部为材料图，已 Read 核对）

| 文件 | 来源 | 内容 | 尺寸 | 使用页 | 核对 |
|---|---|---|---|---|---|
| icon_256.png | 用户提供 | eBox 应用图标（深蓝底双屏） | 256×256 | P1/P19 | ✓ |
| ebox_env.png | 用户截图 | 主界面-环境信息视图 | 1024×759 | P4 | ✓ |
| ebox_license.png | 用户截图 | 授权信息弹窗 | 1024×759 | P7 | ✓ |
| ebox_envinfo.png | 从 ebox_env.png 裁剪 | 环境信息面板特写 | 730×315 | P11 | ✓ |
| ebox_process.png | 用户截图 | 进程记录视图 | 1024×759 | P13 | ✓ |
| ebox_log.png | 用户截图 | 日志面板视图 | 1024×759 | P13 | ✓ |

- 截图标注规范：截图置于 `position:relative` 容器内，编号圆点（28px 圆形、主色渐变底、白字 bold、boxShadow）用 `position:absolute` 按截图坐标等比换算定位；图例列表与编号一一对应。
- 截图统一样式：`borderRadius: 10px` + `border: 1px solid #E2E8F0` + `boxShadow: 0 8px 24px rgba(15,23,42,0.10)`。

## 页面映射表

| # | 文件 | 类型 | 角色 | 版式 | L1 | 字数 | 留白 | 色彩分配 | 关键约束 |
|---|------|------|------|------|----|------|------|----------|----------|
| 01 | slide_01_cover.jsx | cover | hero | 全幅视觉+骑线文字 | icon_256.png | 30 | 40% | 蓝系渐变60%+强调5% | 深色底白字，省略 C 区 |
| 02 | slide_02_intro.jsx | content | supporting | 非对称双栏 60:40 | FAIcon | 220 | 28% | 主色20%+辅色10% | 左宽右窄 |
| 03 | slide_03_flow.jsx | content | supporting | SVG 流程图 | Diagram | 150 | 30% | 主色25% | 6 步横向箭头流 |
| 04 | slide_04_ui_overview.jsx | content | hero | 左大图+右侧文字 | ebox_env.png | 180 | 22% | 主色15% | ①-⑥ 标记+图例 |
| 05 | slide_05_download.jsx | content | supporting | 非对称双栏 55:45 | FAIcon | 240 | 25% | 主色18%+强调5% | 含数据目录警告条 |
| 06 | slide_06_activate.jsx | content | supporting | 左标题+右内容 | Diagram | 230 | 26% | 主色22% | 激活 3 步+码说明 |
| 07 | slide_07_license.jsx | content | supporting | 左大图+右侧文字 | ebox_license.png | 200 | 24% | 主色15% | ①-④ 标记，图例排版与 P4 差异化 |
| 08 | slide_08_launch.jsx | content | supporting | 左标题+右内容 | FAIcon | 240 | 25% | 主色20% | 两方法分区明确 |
| 09 | slide_09_isolation.jsx | content | hero | 巨型数字+洞察 | SVG 架构图 | 120 | 38% | 蓝系55%+强调8% | 深色底，96px+ 锚点 |
| 10 | slide_10_rename.jsx | content | supporting | 非对称双栏 58:42 | FAIcon | 220 | 26% | 主色18% | 改名/再启动双卡 |
| 11 | slide_11_envinfo.jsx | content | supporting | 上大图+下方卡片 | ebox_envinfo.png | 180 | 24% | 主色15% | 特写截图+字段卡 |
| 12 | slide_12_clean.jsx | content | supporting | N卡片横排(3) | FAIcon | 260 | 25% | 主色18%+强调5% | 尾部提示条 marginTop:auto |
| 13 | slide_13_process_log.jsx | content | supporting | 上下分栏双截图 | 2×L2 | 160 | 20% | 主色12% | 双图各带标记 |
| 14 | slide_14_stop_delete.jsx | content | supporting | 非对称双栏对比 | FAIcon | 240 | 24% | 主色15%+强调10% | 删除侧警示色块 |
| 15 | slide_15_renew.jsx | content | supporting | 横向4步流程 | Diagram | 200 | 27% | 主色20% | 到期规则说明 |
| 16 | slide_16_unbind.jsx | content | supporting | 左标题+右内容 | FAIcon | 240 | 25% | 主色18%+强调5% | 次数上限突出 |
| 17 | slide_17_update.jsx | content | supporting | 非对称双栏 55:45 | FAIcon | 230 | 25% | 主色18% | 更新/关闭托盘分区 |
| 18 | slide_18_faq.jsx | content | supporting | 左标题+右内容 | 列表 | 320 | 22% | 主色15% | 6 条 Q&A 编号 |
| 19 | slide_19_end.jsx | ending | hero | 居中金句+全幅渐变 | icon_256.png | 60 | 42% | 蓝系60% | 省略 C 区 |

## 密度与填充门禁

- 常规内容页正文 ≥180 字，留白 ≤35%；容器填充率 ≥85%；卡片尾部元素 `marginTop: 'auto'` 锚底。
- 每页 ≥1 视觉锚点（≥44px 元素或 ≥40% B 区图）；相邻页版式不重复。
