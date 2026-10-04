// 官网文章种子数据（6 篇 SEO 长尾词内容）
// 正文以独立 Markdown 文件维护，便于审阅与修改；由 seed-articles.ts 读取并转为 HTML 入库。
// 字段含义与后台「文章管理」一致，详见 entities/SiteArticle.ts 与 docs/官网设计与实现方案.md

export interface SeedArticle {
  /** 正文 Markdown 文件名（相对本目录） */
  file: string;
  /** URL 标识，唯一 */
  slug: string;
  title: string;
  /** tutorial(教程) / science(科普) / update(更新) / notice(公告) */
  category: 'tutorial' | 'science' | 'update' | 'notice';
  summary: string;
  seoTitle: string;
  seoDesc: string;
  /** 是否置顶 */
  pinned: boolean;
}

export const SEED_ARTICLES: SeedArticle[] = [
  {
    file: '01-qiye-weixin-duokai.md',
    slug: 'qiye-weixin-duokai',
    title: '企业微信多开怎么实现？会被封号吗？一文讲清原理与风险',
    category: 'science',
    summary:
      '企业微信多开是指在同一台电脑上同时登录并运行多个企微账号。本文讲清它的常见实现方式、官方对多开的态度，以及如何尽量降低账号受限与封号风险。',
    seoTitle: '企业微信多开怎么实现_会封号吗 - 原理与风险解析',
    seoDesc:
      '企业微信多开怎么实现？为什么会被判定外挂？本文讲清强开、虚拟机、隔离式多开三种方式的原理与风险，并给出合规使用建议。',
    pinned: true,
  },
  {
    file: '02-weixin-diannaoban-duokai.md',
    slug: 'weixin-diannaoban-duokai',
    title: '微信电脑版怎么多开？多开器 / 虚拟机 / 隔离多开 3 种方法对比',
    category: 'tutorial',
    summary:
      '微信电脑版默认只能登录一个账号，想同时登录多个，常见有多开器、虚拟机、隔离式多开三种方法。本文逐一对比原理、体验与风险，帮你选对方案。',
    seoTitle: '微信电脑版怎么多开_微信多开器怎么用 - 3 种方法对比',
    seoDesc:
      '微信电脑版怎么多开？本文对比多开器、虚拟机、隔离式多开三种方式的原理、体验和封号风险，并给出多开微信电脑版的实用建议。',
    pinned: true,
  },
  {
    file: '03-duokaiqi-yuanli-fengxian.md',
    slug: 'duokaiqi-yuanli-fengxian',
    title: '多开器是什么？为什么容易封号？原理与风险一次说清',
    category: 'science',
    summary:
      '多开器是一类能让程序同时开多个实例的工具，但它往往通过改程序、注入、篡改内存实现，容易被风控识别。本文讲清多开器的原理、为什么会封号，以及更稳妥的替代思路。',
    seoTitle: '多开器是什么_万能多开器为什么封号 - 原理与风险',
    seoDesc:
      '多开器、万能多开是什么原理？为什么用了容易封号？本文讲清多开器的常见实现方式、被风控识别的机制，以及更安全的隔离式多开替代思路。',
    pinned: false,
  },
  {
    file: '04-xuniji-duokai-duibi.md',
    slug: 'xuniji-duokai-duibi',
    title: '虚拟机多开 vs 隔离式多开，长期挂账号选哪种更稳？',
    category: 'science',
    summary:
      '想在一台电脑长期挂着多个账号，虚拟机多开和隔离式多开都是常见选择。本文从资源占用、隔离效果、体验、风控特征四个维度对比，帮长期运营者选对方案。',
    seoTitle: '虚拟机多开和隔离式多开哪个好 - 长期挂账号对比',
    seoDesc:
      '虚拟机多开软件好用吗？和隔离式多开比谁更稳？本文从资源占用、隔离程度、使用体验、风控特征四方面对比两种方案，给出长期挂账号的选择建议。',
    pinned: false,
  },
  {
    file: '05-ebox-shiyong-jiaocheng.md',
    slug: 'ebox-shiyong-jiaocheng',
    title: 'eBox 多开工具怎么用？从下载到多开的完整图文教程',
    category: 'tutorial',
    summary:
      'eBox 是一款轻量级 Windows 多实例运行工具，绿色单文件免安装。本文从下载、激活、新建环境，到多开应用、环境管理、解绑换机，一步步讲清 eBox 怎么用。',
    seoTitle: 'eBox 多开工具怎么用_2box 使用教程 - 下载到多开全过程',
    seoDesc:
      'eBox（2box）多开工具怎么用？本文从下载、激活、新建环境、启动应用到环境管理与常见问题，给出完整图文教程，新手也能一次上手。',
    pinned: true,
  },
  {
    file: '06-yitai-diannao-duokai-yingyong.md',
    slug: 'yitai-diannao-duokai-yingyong',
    title: '一台电脑怎么多开多个应用？微商 / 企微销售实战方案',
    category: 'tutorial',
    summary:
      '微商、私域运营、企微销售常需要在一台电脑上同时多开微信、企业微信等多个应用。本文给出从设备准备、账号分组到日常运营的完整实战方案，并提醒合规边界。',
    seoTitle: '一台电脑怎么多开多个应用_电脑多开应用实战方案',
    seoDesc:
      '一台电脑怎么多开多个应用？本文以微商和企微销售场景为例，讲清多开方案选择、设备与账号规划、日常运营要点，以及必须注意的合规风险。',
    pinned: false,
  },
];
