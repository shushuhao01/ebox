import { AppDataSource } from '../config/database';
import { SiteArticle } from '../entities/SiteArticle';
import { getSiteSettings } from './SiteService';
import { log } from '../config/logger';

/** 官网正式主域（与前台 canonical 保持一致） */
const DEFAULT_ORIGIN = 'https://www.abc222.cn';

/** 站点固定页面（非文章），写入 sitemap 的固定部分 */
const STATIC_ROUTES: { path: string; changefreq: string; priority: string }[] = [
  { path: '/', changefreq: 'weekly', priority: '1.0' },
  { path: '/features', changefreq: 'monthly', priority: '0.9' },
  { path: '/download', changefreq: 'weekly', priority: '0.9' },
  { path: '/cases', changefreq: 'weekly', priority: '0.8' },
  { path: '/articles', changefreq: 'weekly', priority: '0.8' },
  { path: '/docs', changefreq: 'monthly', priority: '0.7' },
  { path: '/contact', changefreq: 'monthly', priority: '0.6' },
  { path: '/agreement', changefreq: 'yearly', priority: '0.3' },
  { path: '/privacy', changefreq: 'yearly', priority: '0.3' },
];

const FETCH_TIMEOUT_MS = 8000;

/**
 * 百度普通收录单批最大 URL 数。
 * 实测单站每日额度约 10 条，且整批超出额度会 all-or-nothing 全批失败，
 * 因此分批推送并依据接口返回的 remain 动态调整批次大小。
 */
const BAIDU_MAX_BATCH = 10;

export interface SeoConfig {
  enabled: boolean;
  origin: string;
  baiduToken: string;
  indexnowKey: string;
}

/** 单个搜索引擎的推送状态 */
export type PushStatus = 'ok' | 'partial' | 'fail' | 'skipped';

/** 单次推送结果 */
export interface PushReport {
  total: number;
  baidu: PushStatus;
  indexnow: PushStatus;
  /** 百度结果说明（成功条数 / 剩余未推 / error+message），用于后台回显 */
  baiduMessage?: string;
  /** IndexNow 结果说明（HTTP 状态码 / 错误描述），用于后台回显失败原因 */
  indexnowMessage?: string;
}

/** 单个搜索引擎的推送结果（带说明） */
interface PushResult {
  status: PushStatus;
  message: string;
}

/** 宽松解析 JSON，失败返回 null（推送接口异常时 body 可能不是 JSON） */
function safeJson(text: string): Record<string, unknown> | null {
  try {
    const v = JSON.parse(text);
    return v && typeof v === 'object' ? (v as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

/** 读取站点主域（去掉末尾斜杠） */
export async function getSiteOrigin(): Promise<string> {
  const settings = await getSiteSettings();
  return (settings.site_origin || DEFAULT_ORIGIN).replace(/\/+$/, '');
}

/** 读取主动推送配置 */
export async function getSeoConfig(): Promise<SeoConfig> {
  const s = await getSiteSettings();
  return {
    enabled: (s.seo_push_enabled ?? '1') !== '0',
    origin: (s.site_origin || DEFAULT_ORIGIN).replace(/\/+$/, ''),
    baiduToken: (s.baidu_push_token || '').trim(),
    indexnowKey: (s.indexnow_key || '').trim(),
  };
}

function escapeXml(v: string): string {
  return v
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&apos;');
}

function toDateStr(v: Date | null | undefined): string {
  if (!v) return '';
  const d = new Date(v);
  if (Number.isNaN(d.getTime())) return '';
  return d.toISOString().slice(0, 10);
}

/** 文章是否对搜索引擎可见（已发布、已到发布时间、未过期） */
function isArticleVisible(a: SiteArticle): boolean {
  if (a.status !== 'published') return false;
  const now = Date.now();
  if (a.publishAt && new Date(a.publishAt).getTime() > now) return false;
  if (a.expireAt && new Date(a.expireAt).getTime() <= now) return false;
  return true;
}

/** 查询全部可见文章 */
async function findVisibleArticles(): Promise<SiteArticle[]> {
  return AppDataSource.getRepository(SiteArticle)
    .createQueryBuilder('a')
    .where('a.status = :status', { status: 'published' })
    .andWhere('(a.publish_at IS NULL OR a.publish_at <= NOW())')
    .andWhere('(a.expire_at IS NULL OR a.expire_at > NOW())')
    .orderBy('a.publish_at', 'DESC')
    .addOrderBy('a.id', 'DESC')
    .getMany();
}

/** 动态生成 sitemap.xml（固定页面 + 全部可见文章） */
export async function buildSitemapXml(): Promise<string> {
  const origin = await getSiteOrigin();
  const articles = await findVisibleArticles();

  const out: string[] = [];
  out.push('<?xml version="1.0" encoding="UTF-8"?>');
  out.push('<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">');

  for (const r of STATIC_ROUTES) {
    out.push('  <url>');
    out.push(`    <loc>${origin}${r.path}</loc>`);
    out.push(`    <changefreq>${r.changefreq}</changefreq>`);
    out.push(`    <priority>${r.priority}</priority>`);
    out.push('  </url>');
  }

  for (const a of articles) {
    const lastmod = toDateStr(a.updatedAt || a.publishAt || a.createdAt);
    out.push('  <url>');
    out.push(`    <loc>${origin}/articles/${escapeXml(a.slug)}</loc>`);
    if (lastmod) out.push(`    <lastmod>${lastmod}</lastmod>`);
    out.push('    <changefreq>monthly</changefreq>');
    out.push('    <priority>0.7</priority>');
    out.push('  </url>');
  }

  out.push('</urlset>');
  return out.join('\n');
}

/** 带超时的 fetch（避免推送接口无响应时挂起） */
async function fetchWithTimeout(url: string, init: RequestInit): Promise<Response> {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), FETCH_TIMEOUT_MS);
  try {
    return await fetch(url, { ...init, signal: controller.signal });
  } finally {
    clearTimeout(timer);
  }
}

/** 单次百度推送响应解析 */
interface BaiduBatchResult {
  ok: boolean;
  success: number;
  remain?: number;
  overQuota: boolean;
  message: string;
}

/** 发送一批 URL 到百度（不做额度处理，只解析响应） */
async function sendBaiduBatch(endpoint: string, urls: string[]): Promise<BaiduBatchResult> {
  const res = await fetchWithTimeout(endpoint, {
    method: 'POST',
    headers: { 'Content-Type': 'text/plain' },
    body: urls.join('\n'),
  });
  const text = await res.text().catch(() => '');
  const parsed = safeJson(text);
  const remain = typeof parsed?.remain === 'number' ? parsed.remain : undefined;
  const errCode = String(parsed?.error ?? '');
  const errMsg = String(parsed?.message ?? '');
  const overQuota = errCode === 'over quota' || errMsg.includes('over quota');
  if (!res.ok) {
    const detail = errMsg ? `${parsed?.error ?? res.status} ${errMsg}` : `HTTP ${res.status}`;
    return { ok: false, success: 0, remain, overQuota, message: detail };
  }
  const success = typeof parsed?.success === 'number' ? parsed.success : urls.length;
  return { ok: true, success, remain, overQuota: false, message: '' };
}

/**
 * 百度主动推送（普通收录）。
 * 按 BAIDU_MAX_BATCH 分批，超出额度导致整批失败时对半缩小批次重试（额度耗尽前不会消耗额度），
 * 尽量用满当日额度，并如实报告成功 / 未推条数与剩余额度。
 */
async function pushToBaidu(origin: string, token: string, urls: string[]): Promise<PushResult> {
  if (!token) return { status: 'skipped', message: '未配置 token' };
  try {
    const host = new URL(origin).host;
    const endpoint = `http://data.zz.baidu.com/urls?site=${encodeURIComponent(host)}&token=${encodeURIComponent(token)}`;

    let remaining = [...urls];
    let successCount = 0;
    let lastRemain: number | undefined;
    let lastError = '';
    let batchSize = Math.min(remaining.length, BAIDU_MAX_BATCH);

    while (remaining.length) {
      const size = Math.min(remaining.length, batchSize);
      const r = await sendBaiduBatch(endpoint, remaining.slice(0, size));
      if (r.ok) {
        successCount += r.success;
        if (typeof r.remain === 'number') {
          lastRemain = r.remain;
          batchSize = Math.max(1, Math.min(BAIDU_MAX_BATCH, r.remain));
        }
        remaining = remaining.slice(size);
      } else if (r.overQuota) {
        if (size === 1) {
          // 单条都推不动 → 当日额度已耗尽
          lastRemain = r.remain ?? 0;
          break;
        }
        // 整批超出额度：缩小批次重试（未成功则不消耗额度）
        batchSize = Math.max(1, Math.floor(size / 2));
      } else {
        // 其他错误（token 失效 / 站点不匹配等）：无法继续，停止并如实报告
        lastError = r.message;
        break;
      }
    }

    const failedCount = remaining.length;
    if (successCount === 0 && lastError) {
      log.warn(`[SEO] 百度推送失败：${lastError}`);
      return { status: 'fail', message: lastError };
    }

    const parts = [`已推送 ${successCount} 条`];
    if (failedCount > 0) parts.push(`剩余 ${failedCount} 条未推`);
    if (typeof lastRemain === 'number') parts.push(`剩余额度 ${lastRemain}`);
    const detail = parts.join('，');
    log.info(`[SEO] 百度推送 ${urls.length} 条：${detail}`);
    const status: PushStatus = failedCount > 0 ? (successCount > 0 ? 'partial' : 'fail') : 'ok';
    return { status, message: detail };
  } catch (e) {
    log.warn('[SEO] 百度推送异常', String(e));
    return { status: 'fail', message: `请求异常：${String(e)}` };
  }
}

/** IndexNow 推送（覆盖 Bing / Yandex / Naver / Seznam 等） */
async function pushToIndexNow(origin: string, key: string, urls: string[]): Promise<PushResult> {
  if (!key) return { status: 'skipped', message: '未配置 Key' };
  try {
    const host = new URL(origin).host;
    const res = await fetchWithTimeout('https://api.indexnow.org/indexnow', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json; charset=utf-8' },
      body: JSON.stringify({
        host,
        key,
        keyLocation: `${origin}/${key}.txt`,
        urlList: urls,
      }),
    });
    if (!res.ok) {
      const text = await res.text().catch(() => '');
      const parsed = safeJson(text);
      const detail = parsed?.message
        ? `HTTP ${res.status} ${parsed.message}`
        : `HTTP ${res.status}${text ? ` ${text.slice(0, 160)}` : ''}`;
      log.warn(`[SEO] IndexNow 推送失败：${detail}`);
      return { status: 'fail', message: detail };
    }
    log.info(`[SEO] IndexNow 推送 ${urls.length} 条成功（HTTP ${res.status}）`);
    return { status: 'ok', message: `HTTP ${res.status}` };
  } catch (e) {
    log.warn('[SEO] IndexNow 推送异常', String(e));
    return { status: 'fail', message: `请求异常：${String(e)}` };
  }
}

/** 批量推送 URL 到百度与 IndexNow（不抛错，结果写日志） */
export async function pushUrls(urls: string[]): Promise<PushReport> {
  const list = Array.from(new Set(urls.map((u) => String(u || '').trim()).filter(Boolean)));
  if (!list.length) return { total: 0, baidu: 'skipped', indexnow: 'skipped' };

  let cfg: SeoConfig;
  try {
    cfg = await getSeoConfig();
  } catch (e) {
    log.warn('[SEO] 读取推送配置失败', String(e));
    return { total: list.length, baidu: 'skipped', indexnow: 'skipped' };
  }
  if (!cfg.enabled) {
    log.debug('[SEO] 主动推送已关闭，跳过');
    return { total: list.length, baidu: 'skipped', indexnow: 'skipped' };
  }

  const [baidu, indexnow] = await Promise.all([
    pushToBaidu(cfg.origin, cfg.baiduToken, list),
    pushToIndexNow(cfg.origin, cfg.indexnowKey, list),
  ]);
  return {
    total: list.length,
    baidu: baidu.status,
    indexnow: indexnow.status,
    baiduMessage: baidu.message,
    indexnowMessage: indexnow.message,
  };
}

/** 推送单篇文章（会重新校验可见性，不可见则跳过） */
export async function pushArticleBySlug(slug: string): Promise<PushReport | null> {
  if (!slug) return null;
  try {
    const article = await AppDataSource.getRepository(SiteArticle).findOneBy({ slug });
    if (!article || !isArticleVisible(article)) return null;
    const origin = await getSiteOrigin();
    return await pushUrls([`${origin}/articles/${encodeURIComponent(article.slug)}`]);
  } catch (e) {
    log.warn('[SEO] 文章推送异常', String(e));
    return null;
  }
}

/** 一键推送全站（固定页面 + 全部可见文章） */
export async function pushAllUrls(): Promise<PushReport> {
  const origin = await getSiteOrigin();
  const articles = await findVisibleArticles();
  const urls = [
    ...STATIC_ROUTES.map((r) => `${origin}${r.path}`),
    ...articles.map((a) => `${origin}/articles/${encodeURIComponent(a.slug)}`),
  ];
  return pushUrls(urls);
}
