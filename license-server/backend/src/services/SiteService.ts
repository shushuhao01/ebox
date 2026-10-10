import fs from 'fs';
import path from 'path';
import { AppDataSource } from '../config/database';
import { SiteSetting } from '../entities/SiteSetting';
import { SiteDownloadMirror } from '../entities/SiteDownloadMirror';
import { SiteAccessRule } from '../entities/SiteAccessRule';
import { SiteStatsDaily } from '../entities/SiteStatsDaily';
import { SiteChannelStatsDaily } from '../entities/SiteChannelStatsDaily';

/** 官网站点设置默认值（键值均为字符串，复杂结构用 JSON 字符串） */
export const SITE_DEFAULTS: Record<string, string> = {
  // 基本信息
  site_name: 'eBox',
  site_logo: '/logo.png',
  site_favicon: '/favicon.ico',
  primary_color: '#3A7AFE',
  site_description: 'eBox 电脑多开工具，支持微信、企业微信等任意应用多开，环境独立隔离、免扫码自动登录，一台电脑稳定多开不卡顿。',
  site_keywords: 'eBox,2box,多开工具,电脑多开,应用多开,多开器,企业微信多开,微信多开,万能多开,虚拟机多开,环境隔离',
  // 页脚合规
  icp: '',
  police_icp: '',
  copyright: '© eBox 保留所有权利',
  // 购买 / 文档 / 仓库
  purchase_url: 'https://noepay.cn/',
  doc_url: '',
  doc_title: '使用手册',
  doc_target: '_blank',
  github_url: '',
  // 渠道链接：短链域名（为空时回退请求 Host 拼接待用）
  site_domain: '',
  // 首页文案
  home_hero_title: '一台电脑，多开任意应用',
  home_hero_subtitle: '环境独立隔离 · 免扫码自动登录 · 体积小不卡顿',
  home_hero_images: '[]',
  home_stats: '[]',
  // SEO
  seo_title: 'eBox - 电脑多开工具',
  seo_desc: '',
  // 统计代码
  statistics_code: '',
  // 访问安全（仅后台可见）
  maintenance_mode: '0',
  access_password: '',
  // 官网直下：后台上传的安装包（仅后台可见，非空时优先于 dist/update.json）
  release_version: '',
  release_date: '',
  release_changelog: '[]',
  release_file_url: '',
  release_file_name: '',
  release_file_size: '0',
  release_file_sha256: '',
};

/** 公开给官网前台的设置键（不包含敏感项） */
const PUBLIC_KEYS = [
  'site_name', 'site_logo', 'site_favicon', 'primary_color',
  'site_description', 'site_keywords', 'icp', 'police_icp', 'copyright',
  'purchase_url', 'doc_url', 'doc_title', 'doc_target', 'github_url',
  'site_domain',
  'home_hero_title', 'home_hero_subtitle', 'home_hero_images', 'home_stats',
  'seo_title', 'seo_desc', 'statistics_code', 'maintenance_mode',
];

const settingsRepo = () => AppDataSource.getRepository(SiteSetting);

/** 读取全部设置（DB 覆盖默认值） */
export async function getSiteSettings(): Promise<Record<string, string>> {
  const rows = await settingsRepo().find();
  const map: Record<string, string> = { ...SITE_DEFAULTS };
  for (const r of rows) map[r.cfgKey] = r.cfgValue ?? '';
  return map;
}

/** 读取单个设置 */
export async function getSiteSetting(key: string): Promise<string> {
  const row = await settingsRepo().findOneBy({ cfgKey: key });
  return row?.cfgValue ?? SITE_DEFAULTS[key] ?? '';
}

/** 写入单个设置（存在则更新） */
export async function setSiteSetting(key: string, value: string, group = 'general'): Promise<void> {
  let row = await settingsRepo().findOneBy({ cfgKey: key });
  if (!row) {
    row = settingsRepo().create({ cfgKey: key, cfgValue: value, cfgGroup: group });
  } else {
    row.cfgValue = value;
  }
  await settingsRepo().save(row);
}

/** 批量写入设置 */
export async function setSiteSettings(patch: Record<string, string>): Promise<void> {
  for (const [k, v] of Object.entries(patch)) {
    await setSiteSetting(k, v === undefined || v === null ? '' : String(v));
  }
}

/** 公开设置（官网前台使用） */
export async function getPublicSettings(): Promise<Record<string, string>> {
  const all = await getSiteSettings();
  const out: Record<string, string> = {};
  for (const k of PUBLIC_KEYS) out[k] = all[k] ?? '';
  return out;
}

// ==================== 最新版本（只读 dist/update.json） ====================

export interface LatestRelease {
  latestVersion: string;
  latestVersionCode: number;
  releaseDate: string;
  downloadUrl: string;
  downloadSha256: string;
  downloadSize: number;
  changelog: string[];
  mirrors: {
    id: string;
    name: string;
    url: string;
    type: string;
    password: string | null;
    extractCode: string | null;
  }[];
}

let releaseCache: { at: number; data: LatestRelease } | null = null;
const RELEASE_CACHE_MS = 60 * 1000;

/** 解析 update.json 候选路径（仓库根目录 dist/update.json） */
function resolveUpdateJsonPath(): string | null {
  const candidates = [
    process.env.SITE_UPDATE_JSON || '',
    path.resolve(process.cwd(), '../../dist/update.json'),
    path.resolve(__dirname, '../../../../dist/update.json'),
    path.resolve(process.cwd(), 'dist/update.json'),
  ].filter(Boolean);
  for (const p of candidates) {
    try {
      if (fs.existsSync(p)) return p;
    } catch {
      // 忽略
    }
  }
  return null;
}

/** 解析后台填写的更新日志（JSON 数组字符串，兼容按行拆分） */
function parseChangelog(value: string): string[] {
  if (!value) return [];
  try {
    const parsed = JSON.parse(value);
    if (Array.isArray(parsed)) return parsed.map((v) => String(v)).filter(Boolean);
  } catch {
    // 非 JSON，按行拆分
  }
  return value.split('\n').map((s) => s.trim()).filter(Boolean);
}

/** 读取最新版本信息（后台直下安装包优先，否则回退 dist/update.json + 后台配置的下载地址） */
export async function getLatestRelease(): Promise<LatestRelease | null> {
  const now = Date.now();
  if (releaseCache && now - releaseCache.at < RELEASE_CACHE_MS) return releaseCache.data;

  const mirrors = await AppDataSource.getRepository(SiteDownloadMirror).find({
    where: { enabled: 1 },
    order: { sort: 'ASC', id: 'ASC' },
  });
  const mirrorList = mirrors.map((m) => ({
    id: m.id,
    name: m.name,
    url: m.url,
    type: m.type,
    password: m.password || null,
    extractCode: m.extractCode || null,
  }));

  const settings = await getSiteSettings();
  const fileUrl = settings.release_file_url || '';

  // 官网直下：后台上传了安装包时，优先使用后台配置的版本号 / 更新日志 / 直链
  if (fileUrl) {
    const data: LatestRelease = {
      latestVersion: settings.release_version || '',
      latestVersionCode: 0,
      releaseDate: settings.release_date || '',
      downloadUrl: fileUrl,
      downloadSha256: settings.release_file_sha256 || '',
      downloadSize: Number(settings.release_file_size || 0) || 0,
      changelog: parseChangelog(settings.release_changelog || ''),
      mirrors: mirrorList,
    };
    releaseCache = { at: now, data };
    return data;
  }

  // 回退：GitHub Release 自动生成的 dist/update.json
  const p = resolveUpdateJsonPath();
  if (!p) return null;
  let raw: Record<string, unknown>;
  try {
    raw = JSON.parse(fs.readFileSync(p, 'utf8')) as Record<string, unknown>;
  } catch {
    return null;
  }

  const data: LatestRelease = {
    latestVersion: String(raw.latestVersion ?? ''),
    latestVersionCode: Number(raw.latestVersionCode ?? 0),
    releaseDate: String(raw.releaseDate ?? ''),
    downloadUrl: String(raw.downloadUrl ?? ''),
    downloadSha256: String(raw.downloadSha256 ?? ''),
    downloadSize: Number(raw.downloadSize ?? 0),
    changelog: Array.isArray(raw.changelog) ? (raw.changelog as string[]) : [],
    mirrors: mirrorList,
  };
  releaseCache = { at: now, data };
  return data;
}

/** 清空版本缓存（后台手动同步时调用） */
export function clearReleaseCache(): void {
  releaseCache = null;
}

// ==================== 访问控制规则（带缓存） ====================

export interface AccessRule {
  type: string;
  pattern: string;
}

let ruleCache: { at: number; data: AccessRule[] } | null = null;
const RULE_CACHE_MS = 30 * 1000;

export async function getAccessRules(): Promise<AccessRule[]> {
  const now = Date.now();
  if (ruleCache && now - ruleCache.at < RULE_CACHE_MS) return ruleCache.data;
  const rows = await AppDataSource.getRepository(SiteAccessRule).find({ where: { enabled: 1 } });
  const data = rows.map((r) => ({ type: r.type, pattern: r.pattern }));
  ruleCache = { at: now, data };
  return data;
}

export function clearAccessRuleCache(): void {
  ruleCache = null;
}

// ==================== 访问统计 ====================

function today(): string {
  const d = new Date();
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return `${y}-${m}-${day}`;
}

/** 累加今日统计字段（pv/uv/downloads/buyClicks） */
export async function bumpStat(field: 'pv' | 'uv' | 'downloads' | 'buyClicks', delta = 1): Promise<void> {
  const repo = AppDataSource.getRepository(SiteStatsDaily);
  const date = today();
  let row = await repo.findOneBy({ statDate: date });
  if (!row) {
    row = repo.create({ statDate: date, pv: 0, uv: 0, downloads: 0, buyClicks: 0 });
  }
  row[field] = (row[field] || 0) + delta;
  await repo.save(row);
}

/** 读取最近 N 天统计 */
export async function getRecentStats(days = 30): Promise<SiteStatsDaily[]> {
  const repo = AppDataSource.getRepository(SiteStatsDaily);
  const rows = await repo.find({ order: { statDate: 'DESC' }, take: days });
  return rows.reverse();
}

/** 累加今日渠道统计字段（downloads/buyClicks），按 渠道 + 日期 聚合 */
export async function bumpChannelStat(
  channelCode: string,
  field: 'downloads' | 'buyClicks',
  delta = 1
): Promise<void> {
  const code = String(channelCode || '').trim().toLowerCase().slice(0, 32);
  if (!code) return;
  const repo = AppDataSource.getRepository(SiteChannelStatsDaily);
  const date = today();
  let row = await repo.findOneBy({ statDate: date, channelCode: code });
  if (!row) {
    row = repo.create({ statDate: date, channelCode: code, downloads: 0, buyClicks: 0 });
  }
  row[field] = (row[field] || 0) + delta;
  await repo.save(row);
}
