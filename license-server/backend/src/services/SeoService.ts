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

export interface SeoConfig {
  enabled: boolean;
  origin: string;
  baiduToken: string;
  indexnowKey: string;
}

/** 单次推送结果 */
export interface PushReport {
  total: number;
  baidu: 'ok' | 'fail' | 'skipped';
  indexnow: 'ok' | 'fail' | 'skipped';
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

/** 百度主动推送（普通收录） */
async function pushToBaidu(origin: string, token: string, urls: string[]): Promise<'ok' | 'fail' | 'skipped'> {
  if (!token) return 'skipped';
  try {
    const host = new URL(origin).host;
    const endpoint = `http://data.zz.baidu.com/urls?site=${encodeURIComponent(host)}&token=${encodeURIComponent(token)}`;
    const res = await fetchWithTimeout(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: urls.join('\n'),
    });
    const text = await res.text().catch(() => '');
    if (!res.ok) {
      log.warn(`[SEO] 百度推送失败：HTTP ${res.status} ${text.slice(0, 300)}`);
      return 'fail';
    }
    log.info(`[SEO] 百度推送 ${urls.length} 条成功：${text.slice(0, 300)}`);
    return 'ok';
  } catch (e) {
    log.warn('[SEO] 百度推送异常', String(e));
    return 'fail';
  }
}

/** IndexNow 推送（覆盖 Bing / Yandex / Naver / Seznam 等） */
async function pushToIndexNow(origin: string, key: string, urls: string[]): Promise<'ok' | 'fail' | 'skipped'> {
  if (!key) return 'skipped';
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
      log.warn(`[SEO] IndexNow 推送失败：HTTP ${res.status} ${text.slice(0, 300)}`);
      return 'fail';
    }
    log.info(`[SEO] IndexNow 推送 ${urls.length} 条成功（HTTP ${res.status}）`);
    return 'ok';
  } catch (e) {
    log.warn('[SEO] IndexNow 推送异常', String(e));
    return 'fail';
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
  return { total: list.length, baidu, indexnow };
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
