import { Repository, In } from 'typeorm';
import { AppDataSource } from '../config/database';
import { SiteVisitLog } from '../entities/SiteVisitLog';
import { SiteStatsDaily } from '../entities/SiteStatsDaily';
import { SiteOnline } from '../entities/SiteOnline';
import { bumpStat } from './SiteService';
import { defaultDbFile, loadContentFromFile, newWithBuffer, isValidIp } from 'ip2region-ts';

// ==================== 通用类型 ====================

export interface NameValue {
  name: string;
  value: number;
}

const repo = (): Repository<SiteVisitLog> => AppDataSource.getRepository(SiteVisitLog);
const statsRepo = (): Repository<SiteStatsDaily> => AppDataSource.getRepository(SiteStatsDaily);
const onlineRepo = (): Repository<SiteOnline> => AppDataSource.getRepository(SiteOnline);

// ==================== 时间工具（本地时区，与 SiteService.today 保持一致） ====================

function fmtDate(d: Date): string {
  const y = d.getFullYear();
  const m = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return `${y}-${m}-${day}`;
}

function addDays(base: Date, n: number): Date {
  const d = new Date(base.getTime());
  d.setDate(d.getDate() + n);
  return d;
}

// ==================== IP 归属地（离线 ip2region，懒加载） ====================

export interface IpLocation {
  country: string;
  province: string;
  city: string;
  isp: string;
}

const IPV4 = /^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}$/;

let searcher: { search(ip: string): Promise<{ region: string | null }> } | null = null;
let searcherReady = false;

/** 懒加载离线 IP 库（数据文件常驻内存，仅初始化一次；加载失败自动降级为空归属地） */
function getSearcher(): { search(ip: string): Promise<{ region: string | null }> } | null {
  if (searcherReady) return searcher;
  searcherReady = true;
  try {
    const dbPath = process.env.IP2REGION_XDB || defaultDbFile;
    const buffer = loadContentFromFile(dbPath);
    searcher = newWithBuffer(buffer) as unknown as { search(ip: string): Promise<{ region: string | null }> };
  } catch {
    searcher = null;
  }
  return searcher;
}

function cleanPart(v: string | undefined): string {
  const s = (v || '').trim();
  return s === '0' ? '' : s;
}

/** 解析 IP 归属地：region 格式 `国家|区域|省份|城市|ISP`（`0` 表示空） */
export async function resolveLocation(ip: string): Promise<IpLocation> {
  const empty: IpLocation = { country: '', province: '', city: '', isp: '' };
  if (!ip || !IPV4.test(ip) || !isValidIp(ip)) return empty;
  const s = getSearcher();
  if (!s) return empty;
  try {
    const { region } = await s.search(ip);
    if (!region) return empty;
    const parts = region.split('|');
    const isp = cleanPart(parts[4]);
    if (isp === '内网IP') return { country: '', province: '局域网', city: '', isp };
    return {
      country: cleanPart(parts[0]),
      province: cleanPart(parts[2]),
      city: cleanPart(parts[3]),
      isp,
    };
  } catch {
    return empty;
  }
}

// ==================== User-Agent 解析（轻量自研） ====================

export interface UaInfo {
  device: string;
  os: string;
  browser: string;
}

const BOT_RE = /bot|spider|crawler|slurp|bingpreview|python-requests|curl\/|wget|headless|facebookexternalhit|node-fetch|axios|okhttp/i;

function detectDevice(ua: string): string {
  if (BOT_RE.test(ua)) return 'bot';
  if (/ipad|tablet|playbook|silk|kindle/i.test(ua)) return 'tablet';
  if (/android/i.test(ua) && !/mobile/i.test(ua)) return 'tablet';
  if (/mobile|iphone|ipod|android|windows phone|blackberry|iemobile|opera mini/i.test(ua)) return 'mobile';
  if (/windows|macintosh|mac os x|x11|linux|cros/i.test(ua)) return 'desktop';
  return 'unknown';
}

function detectOs(ua: string): string {
  if (/harmonyos|openharmony/i.test(ua)) return 'HarmonyOS';
  if (/windows nt 10\.0/i.test(ua)) return 'Windows 10/11';
  if (/windows nt 6\.3/i.test(ua)) return 'Windows 8.1';
  if (/windows nt 6\.2/i.test(ua)) return 'Windows 8';
  if (/windows nt 6\.1/i.test(ua)) return 'Windows 7';
  if (/windows nt 6\.0/i.test(ua)) return 'Windows Vista';
  if (/windows nt 5\.1/i.test(ua)) return 'Windows XP';
  if (/windows/i.test(ua)) return 'Windows';
  if (/iphone|ipad|ipod/i.test(ua)) return 'iOS';
  if (/mac os x|macintosh/i.test(ua)) return 'macOS';
  if (/android/i.test(ua)) return 'Android';
  if (/cros/i.test(ua)) return 'ChromeOS';
  if (/linux|x11/i.test(ua)) return 'Linux';
  return '';
}

function detectBrowser(ua: string): string {
  if (/micromessenger/i.test(ua)) return '微信';
  if (/qqbrowser/i.test(ua)) return 'QQ浏览器';
  if (/ucbrowser|ucweb/i.test(ua)) return 'UC浏览器';
  if (/360se|360ee|qihoobrowser/i.test(ua)) return '360浏览器';
  if (/edg[ae]?\//i.test(ua)) return 'Edge';
  if (/opr\/|opera/i.test(ua)) return 'Opera';
  if (/chrome|crios/i.test(ua)) return 'Chrome';
  if (/firefox|fxios/i.test(ua)) return 'Firefox';
  if (/msie|trident/i.test(ua)) return 'IE';
  if (/safari/i.test(ua)) return 'Safari';
  return '其他';
}

export function parseUserAgent(uaRaw: string): UaInfo {
  const ua = uaRaw || '';
  return { device: detectDevice(ua), os: detectOs(ua), browser: detectBrowser(ua) };
}

// ==================== 来源分类 ====================

/** 依据 referer 归类来源渠道（返回中文标签） */
export function classifySource(referer: string, host: string): string {
  if (!referer) return '直接访问';
  let refHost = '';
  try {
    refHost = new URL(referer).hostname.toLowerCase();
  } catch {
    refHost = referer.toLowerCase();
  }
  if (!refHost) return '直接访问';
  const h = (host || '').toLowerCase();
  if (h && (refHost === h || refHost.endsWith('.' + h))) return '站内';
  if (refHost.includes('baidu.')) return '百度';
  if (refHost.includes('google.')) return '谷歌';
  if (refHost.includes('bing.')) return '必应';
  if (refHost.includes('sogou.')) return '搜狗';
  if (refHost.includes('so.com') || refHost.includes('360.cn') || refHost.includes('360.com')) return '360搜索';
  if (refHost.includes('sm.cn')) return '神马';
  if (refHost.includes('weixin') || refHost.includes('wechat')) return '微信';
  if (refHost.includes('weibo.')) return '微博';
  if (refHost.includes('zhihu.')) return '知乎';
  if (refHost.includes('github.')) return 'GitHub';
  if (refHost.includes('douyin.') || refHost.includes('tiktok.')) return '抖音';
  if (refHost.includes('xiaohongshu.')) return '小红书';
  if (refHost.includes('qq.com')) return 'QQ';
  return '外部链接';
}

// ==================== 访问记录写入 ====================

export interface RecordVisitInput {
  ip: string;
  ua: string;
  referer: string;
  path: string;
  visitorId: string;
  host: string;
}

/** 当日是否该访客首次访问（UV 去重；visitorId 为空时按 IP 兜底） */
async function isFirstVisitToday(date: string, visitorId: string, ip: string): Promise<boolean> {
  if (visitorId) {
    return (await repo().count({ where: { visitDate: date, visitorId } })) === 0;
  }
  if (ip) {
    return (await repo().count({ where: { visitDate: date, ip } })) === 0;
  }
  return false;
}

/** 记录一次页面访问：写明细 + 累加 PV，当日首访额外累加 UV */
export async function recordVisit(input: RecordVisitInput): Promise<void> {
  const now = new Date();
  const visitDate = fmtDate(now);
  // 先判断是否当日首访（此时当前记录尚未写入）
  const isNew = await isFirstVisitToday(visitDate, input.visitorId, input.ip).catch(() => false);

  const [loc, ua] = [await resolveLocation(input.ip), parseUserAgent(input.ua)];
  const row = repo().create({
    visitDate,
    visitHour: now.getHours(),
    ip: (input.ip || '').slice(0, 64),
    country: loc.country,
    province: loc.province,
    city: loc.city,
    isp: loc.isp,
    device: ua.device,
    os: ua.os,
    browser: ua.browser,
    source: classifySource(input.referer, input.host),
    referer: (input.referer || '').slice(0, 512),
    path: (input.path || '/').slice(0, 255),
    visitorId: (input.visitorId || '').slice(0, 64),
    userAgent: (input.ua || '').slice(0, 255),
  });
  await repo().save(row);

  await bumpStat('pv', 1);
  if (isNew) await bumpStat('uv', 1);
}

// ==================== 聚合分析 ====================

export interface AnalyticsSummary {
  pv: number;
  uv: number;
  ips: number;
  downloads: number;
  buyClicks: number;
  todayPv: number;
  todayUv: number;
  avgPv: number;
  peakHour: number;
  peakHourPv: number;
}

export interface AnalyticsTrend {
  dates: string[];
  pv: number[];
  uv: number[];
}

export interface AnalyticsPage {
  path: string;
  pv: number;
  uv: number;
}

export interface AnalyticsIp {
  ip: string;
  province: string;
  city: string;
  isp: string;
  pv: number;
  lastPath: string;
  lastTime: string;
}

export interface AnalyticsDaily {
  date: string;
  pv: number;
  uv: number;
  downloads: number;
  buyClicks: number;
}

export interface SiteAnalytics {
  range: { days: number; start: string; end: string };
  summary: AnalyticsSummary;
  trend: AnalyticsTrend;
  hours: { labels: string[]; pv: number[] };
  sources: NameValue[];
  devices: NameValue[];
  os: NameValue[];
  browsers: NameValue[];
  regions: NameValue[];
  isps: NameValue[];
  pages: AnalyticsPage[];
  ips: AnalyticsIp[];
  daily: AnalyticsDaily[];
}

const UV_EXPR = "COUNT(DISTINCT CASE WHEN v.visitor_id = '' THEN v.ip ELSE v.visitor_id END)";

function toNum(v: unknown): number {
  return Number(v) || 0;
}

/** 官网数据分析（流量 / 来源 / 地域 / 设备 / 时间等多维度聚合） */
export async function getSiteAnalytics(days = 30): Promise<SiteAnalytics> {
  const range = Math.min(90, Math.max(1, Math.floor(days) || 30));
  const now = new Date();
  const start = fmtDate(addDays(now, -(range - 1)));
  const end = fmtDate(now);
  const today = end;

  const r = repo();

  const [sumRow, trendRows, hourRows, sourceRows, deviceRows, osRows, browserRows, regionRows, ispRows, pageRows, ipRows, todayRow] =
    await Promise.all([
      r.createQueryBuilder('v')
        .select('COUNT(*)', 'pv')
        .addSelect(UV_EXPR, 'uv')
        .addSelect('COUNT(DISTINCT v.ip)', 'ips')
        .where('v.visit_date >= :start', { start })
        .getRawOne<{ pv: string; uv: string; ips: string }>(),
      r.createQueryBuilder('v')
        .select("DATE_FORMAT(v.visit_date, '%Y-%m-%d')", 'date')
        .addSelect('COUNT(*)', 'pv')
        .addSelect(UV_EXPR, 'uv')
        .where('v.visit_date >= :start', { start })
        .groupBy('v.visit_date')
        .orderBy('v.visit_date', 'ASC')
        .getRawMany<{ date: string; pv: string; uv: string }>(),
      r.createQueryBuilder('v')
        .select('v.visit_hour', 'hour')
        .addSelect('COUNT(*)', 'pv')
        .where('v.visit_date >= :start', { start })
        .groupBy('v.visit_hour')
        .orderBy('v.visit_hour', 'ASC')
        .getRawMany<{ hour: number; pv: string }>(),
      r.createQueryBuilder('v')
        .select('v.source', 'name')
        .addSelect('COUNT(*)', 'value')
        .where('v.visit_date >= :start', { start })
        .groupBy('v.source')
        .orderBy('value', 'DESC')
        .limit(10)
        .getRawMany<{ name: string; value: string }>(),
      r.createQueryBuilder('v')
        .select('v.device', 'name')
        .addSelect('COUNT(*)', 'value')
        .where('v.visit_date >= :start', { start })
        .groupBy('v.device')
        .orderBy('value', 'DESC')
        .limit(10)
        .getRawMany<{ name: string; value: string }>(),
      r.createQueryBuilder('v')
        .select('v.os', 'name')
        .addSelect('COUNT(*)', 'value')
        .where('v.visit_date >= :start', { start })
        .groupBy('v.os')
        .orderBy('value', 'DESC')
        .limit(10)
        .getRawMany<{ name: string; value: string }>(),
      r.createQueryBuilder('v')
        .select('v.browser', 'name')
        .addSelect('COUNT(*)', 'value')
        .where('v.visit_date >= :start', { start })
        .groupBy('v.browser')
        .orderBy('value', 'DESC')
        .limit(10)
        .getRawMany<{ name: string; value: string }>(),
      r.createQueryBuilder('v')
        .select('v.province', 'name')
        .addSelect('COUNT(*)', 'value')
        .where('v.visit_date >= :start', { start })
        .andWhere("v.province != ''")
        .groupBy('v.province')
        .orderBy('value', 'DESC')
        .limit(10)
        .getRawMany<{ name: string; value: string }>(),
      r.createQueryBuilder('v')
        .select('v.isp', 'name')
        .addSelect('COUNT(*)', 'value')
        .where('v.visit_date >= :start', { start })
        .andWhere("v.isp != ''")
        .groupBy('v.isp')
        .orderBy('value', 'DESC')
        .limit(10)
        .getRawMany<{ name: string; value: string }>(),
      r.createQueryBuilder('v')
        .select('v.path', 'path')
        .addSelect('COUNT(*)', 'pv')
        .addSelect(UV_EXPR, 'uv')
        .where('v.visit_date >= :start', { start })
        .groupBy('v.path')
        .orderBy('pv', 'DESC')
        .limit(10)
        .getRawMany<{ path: string; pv: string; uv: string }>(),
      r.createQueryBuilder('v')
        .select('v.ip', 'ip')
        .addSelect('COUNT(*)', 'pv')
        .addSelect('MAX(v.id)', 'lastId')
        .where('v.visit_date >= :start', { start })
        .andWhere("v.ip != ''")
        .groupBy('v.ip')
        .orderBy('pv', 'DESC')
        .limit(50)
        .getRawMany<{ ip: string; pv: string; lastId: string }>(),
      r.createQueryBuilder('v')
        .select('COUNT(*)', 'pv')
        .addSelect(UV_EXPR, 'uv')
        .where('v.visit_date = :today', { today })
        .getRawOne<{ pv: string; uv: string }>(),
    ]);

  // 访问明细中 Top IP 的最近一次访问信息
  const detailMap = new Map<string, SiteVisitLog>();
  const ids = ipRows.map((x) => x.lastId).filter(Boolean);
  if (ids.length) {
    const details = await r.find({ where: { id: In(ids) } });
    for (const d of details) detailMap.set(String(d.id), d);
  }

  // 下载 / 购买点击来自日聚合表
  const statRows = await statsRepo()
    .createQueryBuilder('s')
    .where('s.stat_date >= :start', { start })
    .getMany();
  const statMap = new Map<string, SiteStatsDaily>();
  for (const s of statRows) statMap.set(s.statDate, s);
  const downloads = statRows.reduce((a, s) => a + (s.downloads || 0), 0);
  const buyClicks = statRows.reduce((a, s) => a + (s.buyClicks || 0), 0);

  // 补齐日期序列
  const dates: string[] = [];
  for (let i = 0; i < range; i += 1) dates.push(fmtDate(addDays(now, -(range - 1 - i))));
  const trendPvMap = new Map<string, number>();
  const trendUvMap = new Map<string, number>();
  for (const row of trendRows) {
    trendPvMap.set(String(row.date), toNum(row.pv));
    trendUvMap.set(String(row.date), toNum(row.uv));
  }
  const trend: AnalyticsTrend = {
    dates,
    pv: dates.map((d) => trendPvMap.get(d) || 0),
    uv: dates.map((d) => trendUvMap.get(d) || 0),
  };

  // 补齐 0-23 小时
  const hourMap = new Map<number, number>();
  for (const row of hourRows) hourMap.set(Number(row.hour), toNum(row.pv));
  const hours = {
    labels: Array.from({ length: 24 }, (_, i) => `${String(i).padStart(2, '0')}:00`),
    pv: Array.from({ length: 24 }, (_, i) => hourMap.get(i) || 0),
  };
  let peakHour = 0;
  let peakHourPv = 0;
  hours.pv.forEach((v, i) => {
    if (v > peakHourPv) {
      peakHourPv = v;
      peakHour = i;
    }
  });

  const pv = toNum(sumRow?.pv);
  const uv = toNum(sumRow?.uv);

  const daily: AnalyticsDaily[] = dates.map((d) => {
    const s = statMap.get(d);
    return {
      date: d,
      pv: trendPvMap.get(d) || 0,
      uv: trendUvMap.get(d) || 0,
      downloads: s?.downloads || 0,
      buyClicks: s?.buyClicks || 0,
    };
  });

  return {
    range: { days: range, start, end },
    summary: {
      pv,
      uv,
      ips: toNum(sumRow?.ips),
      downloads,
      buyClicks,
      todayPv: toNum(todayRow?.pv),
      todayUv: toNum(todayRow?.uv),
      avgPv: range > 0 ? Math.round(pv / range) : 0,
      peakHour,
      peakHourPv,
    },
    trend,
    hours,
    sources: sourceRows.map((x) => ({ name: x.name || '未知', value: toNum(x.value) })),
    devices: deviceRows.map((x) => ({ name: x.name || '未知', value: toNum(x.value) })),
    os: osRows.map((x) => ({ name: x.name || '未知', value: toNum(x.value) })),
    browsers: browserRows.map((x) => ({ name: x.name || '未知', value: toNum(x.value) })),
    regions: regionRows.map((x) => ({ name: x.name || '未知', value: toNum(x.value) })),
    isps: ispRows.map((x) => ({ name: x.name || '未知', value: toNum(x.value) })),
    pages: pageRows.map((x) => ({ path: x.path || '/', pv: toNum(x.pv), uv: toNum(x.uv) })),
    ips: ipRows.map((x) => {
      const d = detailMap.get(String(x.lastId));
      return {
        ip: x.ip,
        province: d?.province || '',
        city: d?.city || '',
        isp: d?.isp || '',
        pv: toNum(x.pv),
        lastPath: d?.path || '',
        lastTime: d?.createdAt ? new Date(d.createdAt).toISOString() : '',
      };
    }),
    daily,
  };
}

// ==================== 实时在线 ====================

const ONLINE_WINDOW_MIN = 5;

/** 刷新某访客的在线状态（visitor_id 优先，回退 ip），用于页面访问与心跳上报 */
export async function touchOnline(visitorId: string, ip: string): Promise<void> {
  const key = (visitorId || '').trim() || `ip:${(ip || '').trim()}`;
  if (key === 'ip:') return; // 既无访客标识也无 IP，忽略
  await onlineRepo()
    .createQueryBuilder()
    .insert()
    .into(SiteOnline)
    .values({ visitorId: key.slice(0, 64), ip: (ip || '').slice(0, 64), lastSeen: new Date() })
    .orUpdate(['ip', 'last_seen'], ['visitor_id'])
    .execute();
}

/** 实时在线访客数：最近 N 分钟内有过活跃上报的在线记录数 */
export async function getSiteOnlineCount(minutes = ONLINE_WINDOW_MIN): Promise<number> {
  const win = Math.min(1440, Math.max(1, Math.floor(minutes) || ONLINE_WINDOW_MIN));
  const n = await onlineRepo()
    .createQueryBuilder('o')
    .where('o.last_seen >= DATE_SUB(NOW(), INTERVAL :min MINUTE)', { min: win })
    .getCount();
  return n;
}

/** 清理在线表中过期记录（仅保留最近 hours 小时的在线状态） */
export async function cleanSiteOnline(hours = 24): Promise<number> {
  const h = Math.max(1, Math.floor(hours) || 24);
  const res = await onlineRepo()
    .createQueryBuilder()
    .delete()
    .where('last_seen < DATE_SUB(NOW(), INTERVAL :h HOUR)', { h })
    .execute();
  return res.affected || 0;
}

// ==================== 访问明细查询 ====================

export interface VisitQuery {
  page: number;
  pageSize: number;
  ip?: string;
  path?: string;
  device?: string;
  source?: string;
  keyword?: string;
  start?: string;
  end?: string;
}

export interface VisitListResult {
  total: number;
  page: number;
  pageSize: number;
  list: SiteVisitLog[];
}

/** 分页查询访问明细（支持 IP / 路径 / 设备 / 来源 / 关键字 / 时间范围过滤） */
export async function listSiteVisits(q: VisitQuery): Promise<VisitListResult> {
  const page = Math.max(1, q.page || 1);
  const pageSize = Math.min(100, Math.max(1, q.pageSize || 20));
  const qb = repo().createQueryBuilder('v');

  if (q.ip) qb.andWhere('v.ip LIKE :ip', { ip: `%${q.ip}%` });
  if (q.path) qb.andWhere('v.path LIKE :path', { path: `%${q.path}%` });
  if (q.device) qb.andWhere('v.device = :device', { device: q.device });
  if (q.source) qb.andWhere('v.source = :source', { source: q.source });
  if (q.keyword) {
    qb.andWhere(
      '(v.ip LIKE :kw OR v.path LIKE :kw OR v.referer LIKE :kw OR v.user_agent LIKE :kw OR v.province LIKE :kw OR v.city LIKE :kw)',
      { kw: `%${q.keyword}%` }
    );
  }
  if (q.start) qb.andWhere('v.visit_date >= :start', { start: q.start });
  if (q.end) qb.andWhere('v.visit_date <= :end', { end: q.end });

  const total = await qb.getCount();
  const list = await qb
    .orderBy('v.id', 'DESC')
    .skip((page - 1) * pageSize)
    .take(pageSize)
    .getMany();

  return { total, page, pageSize, list };
}

// ==================== 明细清理（保留期，默认 90 天） ====================

/** 删除超过保留期的访问明细 */
export async function cleanSiteVisitLogs(retentionDays = 90): Promise<number> {
  const days = Math.max(1, Math.floor(retentionDays) || 90);
  const cutoff = fmtDate(addDays(new Date(), -days));
  const res = await repo()
    .createQueryBuilder()
    .delete()
    .where('visit_date < :cutoff', { cutoff })
    .execute();
  return res.affected || 0;
}
