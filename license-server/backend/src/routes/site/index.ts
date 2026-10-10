import { Router, Request, Response, NextFunction } from 'express';
import { AppDataSource } from '../../config/database';
import { SiteArticle } from '../../entities/SiteArticle';
import { SiteCase } from '../../entities/SiteCase';
import { SiteNav } from '../../entities/SiteNav';
import { SiteContact } from '../../entities/SiteContact';
import { ok, fail, clientIp } from '../../middleware/helpers';
import {
  getPublicSettings,
  getAccessRules,
  getLatestRelease,
  bumpStat,
  bumpChannelStat,
  getSiteSetting,
} from '../../services/SiteService';
import { recordVisit, touchOnline } from '../../services/SiteAnalyticsService';
import { resolveChannel } from '../../services/ChannelService';

const router = Router();

// ==================== 访问守卫（只作用于 /api/site/*，绝不影响 /api/v1/* 与 /api/admin/*） ====================

/** 匹配单条 IP 规则：支持精确 IP 与 IPv4 CIDR */
function matchIpRule(ip: string, pattern: string): boolean {
  if (!ip || !pattern) return false;
  if (!pattern.includes('/')) return ip === pattern.trim();
  const [net, bitsStr] = pattern.trim().split('/');
  const bits = parseInt(bitsStr, 10);
  const toLong = (s: string): number | null => {
    const parts = s.split('.').map((n) => parseInt(n, 10));
    if (parts.length !== 4 || parts.some((n) => Number.isNaN(n) || n < 0 || n > 255)) return null;
    return ((parts[0] << 24) >>> 0) + (parts[1] << 16) + (parts[2] << 8) + parts[3];
  };
  const ipLong = toLong(ip);
  const netLong = toLong(net);
  if (ipLong === null || netLong === null || Number.isNaN(bits) || bits < 0 || bits > 32) return false;
  const mask = bits === 0 ? 0 : (0xffffffff << (32 - bits)) >>> 0;
  return (ipLong & mask) === (netLong & mask);
}

async function siteAccessGuard(req: Request, res: Response, next: NextFunction) {
  try {
    const ip = clientIp(req);
    const rules = await getAccessRules();

    const black = rules.filter((r) => r.type === 'ip_black');
    const white = rules.filter((r) => r.type === 'ip_white');
    const uaRules = rules.filter((r) => r.type === 'ua');

    // IP 黑名单：命中直接拒绝
    if (black.some((r) => matchIpRule(ip, r.pattern))) {
      return fail(res, '拒绝访问', 403, 403);
    }
    // IP 白名单：启用后仅白名单可访问
    if (white.length && !white.some((r) => matchIpRule(ip, r.pattern))) {
      return fail(res, '拒绝访问', 403, 403);
    }
    // UA 拦截（关键字或正则）
    const ua = String(req.headers['user-agent'] || '');
    for (const r of uaRules) {
      try {
        if (ua.includes(r.pattern) || new RegExp(r.pattern, 'i').test(ua)) {
          return fail(res, '拒绝访问', 403, 403);
        }
      } catch {
        if (ua.includes(r.pattern)) return fail(res, '拒绝访问', 403, 403);
      }
    }

    // 维护模式：仅 /settings 放行（前台据此跳转维护页），其余接口 503
    const maintenance = await getSiteSetting('maintenance_mode');
    if (maintenance === '1' && req.path !== '/settings') {
      return res.status(503).json({ code: 503, msg: '网站维护中', data: null });
    }

    next();
  } catch {
    // 守卫异常不阻断公开只读接口（避免官网整体不可用）
    next();
  }
}

router.use(siteAccessGuard);

// ==================== 公开只读接口 ====================

/** 站点基本信息 */
router.get('/settings', async (_req, res) => {
  const settings = await getPublicSettings();
  res.set('Cache-Control', 'public, max-age=30');
  ok(res, settings);
});

/** 导航菜单（兼容 position 过滤） */
router.get('/nav', async (req, res) => {
  const position = typeof req.query.position === 'string' ? req.query.position : '';
  const where: Record<string, unknown> = { visible: 1 };
  if (position) where.position = position;
  const rows = await AppDataSource.getRepository(SiteNav).find({
    where,
    order: { sort: 'ASC', id: 'ASC' },
  });
  res.set('Cache-Control', 'public, max-age=60');
  ok(res, rows);
});

/** 文章列表 */
router.get('/articles', async (req, res) => {
  const page = Math.max(1, parseInt(String(req.query.page || '1'), 10) || 1);
  const size = Math.min(50, Math.max(1, parseInt(String(req.query.size || '10'), 10) || 10));
  const category = typeof req.query.category === 'string' ? req.query.category : '';

  const repo = AppDataSource.getRepository(SiteArticle);
  const qb = repo
    .createQueryBuilder('a')
    .where('a.status = :status', { status: 'published' })
    .andWhere('(a.publish_at IS NULL OR a.publish_at <= NOW())')
    .andWhere('(a.expire_at IS NULL OR a.expire_at > NOW())');
  if (category) qb.andWhere('a.category = :category', { category });

  const total = await qb.getCount();
  const list = await qb
    .orderBy('a.pinned', 'DESC')
    .addOrderBy('a.publish_at', 'DESC')
    .addOrderBy('a.id', 'DESC')
    .skip((page - 1) * size)
    .take(size)
    .getMany();

  // 列表不返回正文，减小体积
  const brief = list.map((a) => ({
    id: a.id, slug: a.slug, title: a.title, category: a.category,
    cover: a.cover, summary: a.summary, pinned: a.pinned,
    publishAt: a.publishAt, createdAt: a.createdAt, views: a.views,
  }));
  ok(res, { total, page, size, list: brief });
});

/** 文章详情（views 自增） */
router.get('/articles/:slug', async (req, res) => {
  const repo = AppDataSource.getRepository(SiteArticle);
  const article = await repo.findOneBy({ slug: req.params.slug, status: 'published' });
  if (!article) return fail(res, '文章不存在', 1002);
  // 定时发布未到 / 已过期 均视为不可见
  const now = Date.now();
  if (article.publishAt && new Date(article.publishAt).getTime() > now) return fail(res, '文章不存在', 1002);
  if (article.expireAt && new Date(article.expireAt).getTime() <= now) return fail(res, '文章不存在', 1002);
  await repo.increment({ id: article.id }, 'views', 1);
  article.views += 1;

  // 相关推荐（同分类，排除自身）
  const related = await repo
    .createQueryBuilder('a')
    .where('a.status = :status', { status: 'published' })
    .andWhere('a.category = :category', { category: article.category })
    .andWhere('a.id != :id', { id: article.id })
    .andWhere('(a.publish_at IS NULL OR a.publish_at <= NOW())')
    .andWhere('(a.expire_at IS NULL OR a.expire_at > NOW())')
    .orderBy('a.id', 'DESC')
    .take(5)
    .getMany();

  ok(res, {
    article,
    related: related.map((a) => ({ id: a.id, slug: a.slug, title: a.title, cover: a.cover })),
  });
});

/** 最新公告（官网顶部公告条使用）：取最新一条已发布且未过期的公告，无则返回 null */
router.get('/notice', async (_req, res) => {
  const row = await AppDataSource.getRepository(SiteArticle)
    .createQueryBuilder('a')
    .where('a.category = :category', { category: 'notice' })
    .andWhere('a.status = :status', { status: 'published' })
    .andWhere('(a.publish_at IS NULL OR a.publish_at <= NOW())')
    .andWhere('(a.expire_at IS NULL OR a.expire_at > NOW())')
    .orderBy('a.pinned', 'DESC')
    .addOrderBy('a.publish_at', 'DESC')
    .addOrderBy('a.id', 'DESC')
    .getOne();
  // 公告条需“发布即生效”，禁用浏览器/中间层缓存
  res.set('Cache-Control', 'no-cache, no-store, must-revalidate');
  ok(
    res,
    row
      ? { id: row.id, slug: row.slug, title: row.title, summary: row.summary, linkUrl: row.linkUrl, pinned: row.pinned }
      : null
  );
});

/** 案例列表 */
router.get('/cases', async (req, res) => {
  const industry = typeof req.query.industry === 'string' ? req.query.industry : '';
  const where: Record<string, unknown> = { status: 'published' };
  if (industry) where.industry = industry;
  const rows = await AppDataSource.getRepository(SiteCase).find({
    where,
    order: { sort: 'ASC', id: 'DESC' },
  });
  ok(res, rows);
});

/** 案例详情 */
router.get('/cases/:id', async (req, res) => {
  const row = await AppDataSource.getRepository(SiteCase).findOneBy({ id: req.params.id });
  if (!row || row.status !== 'published') return fail(res, '案例不存在', 1002);
  ok(res, row);
});

/** 联系方式 */
router.get('/contacts', async (_req, res) => {
  const rows = await AppDataSource.getRepository(SiteContact).find({
    where: { enabled: 1 },
    order: { sort: 'ASC', id: 'ASC' },
  });
  ok(res, rows);
});

/** 最新版本 + 下载地址（含网盘 / 镜像） */
router.get('/latest-release', async (_req, res) => {
  const data = await getLatestRelease();
  if (!data) return fail(res, '版本信息暂不可用', 1003);
  res.set('Cache-Control', 'public, max-age=60');
  ok(res, data);
});

/** 短链解析：/c/{code} 命中启用渠道时记录点击并返回跳转目标 */
router.get('/channel/resolve/:code', async (req, res) => {
  const code = String(req.params.code || '');
  const result = await resolveChannel(
    code,
    clientIp(req),
    String(req.headers['user-agent'] || ''),
    String(req.headers.referer || '')
  ).catch(() => null);
  if (!result) return fail(res, '渠道不存在或已停用', 1004);
  ok(res, result);
});

/** 埋点：页面访问（PV/UV+明细）/ 下载点击 / 购买点击 */
router.post('/track', async (req, res) => {
  const type = String(req.body?.type || '');
  const channelCode = String(req.body?.channelCode || '');

  // 页面访问：写入访问明细（IP / 来源 / 设备 / 地域 / 时间），并累加 PV、当日首访累加 UV
  if (type === 'pv') {
    const ip = clientIp(req);
    const visitorId = String(req.body?.visitorId || '');
    try {
      await recordVisit({
        ip,
        ua: String(req.headers['user-agent'] || ''),
        referer: String(req.headers.referer || req.body?.referer || ''),
        path: String(req.body?.path || '/'),
        visitorId,
        host: String(req.headers.host || ''),
        channelCode,
      });
    } catch {
      // 统计失败不影响前台
    }
    try {
      // 访问同时刷新实时在线状态
      await touchOnline(visitorId, ip);
    } catch {
      // 在线统计失败不影响前台
    }
    return ok(res, null);
  }

  // 在线心跳：仅刷新「实时在线」在线状态，不计入 PV / UV
  if (type === 'hb') {
    try {
      await touchOnline(String(req.body?.visitorId || ''), clientIp(req));
    } catch {
      // 在线统计失败不影响前台
    }
    return ok(res, null);
  }

  const map: Record<string, 'uv' | 'downloads' | 'buyClicks'> = {
    uv: 'uv', download: 'downloads', buy: 'buyClicks',
  };
  const field = map[type];
  if (!field) return fail(res, '参数错误', 400);
  try {
    await bumpStat(field, 1);
    // 带渠道码的下载 / 购买点击同时累加渠道维度聚合
    if (channelCode && (type === 'download' || type === 'buy')) {
      await bumpChannelStat(channelCode, type === 'download' ? 'downloads' : 'buyClicks', 1);
    }
  } catch {
    // 统计失败不影响前台
  }
  ok(res, null);
});

export default router;
