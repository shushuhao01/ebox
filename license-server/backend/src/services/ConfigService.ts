import { AppDataSource } from '../config/database';
import { SystemConfig } from '../entities/SystemConfig';
import { log } from '../config/logger';
import { broadcastNotice, type NoticeData, type NoticeEvent } from './NoticeBroadcaster';

const DEFAULTS: Record<string, string> = {
  heartbeat_interval_hours: '6',
  offline_grace_days: '7',
  force_online_activate: '0',
  // 兼容字段：老客户端只认纯文本公告（由结构化公告派生写入）
  notice: '',
  // 公告草稿（管理端编辑态，保存不影响已发布内容）
  notice_title: '',
  notice_content: '',
  notice_strong: '0',
  notice_link: '',
  // 公告发布态（JSON）与待定时发布任务（JSON）
  notice_payload: '',
  notice_pending: '',
  online_threshold_minutes: '30',
  // 操作日志自动清理：保留天数（默认 1 天=24 小时），每日清理时间 HH:mm
  log_retention_days: '1',
  log_clean_time: '03:00',
};

const repo = () => AppDataSource.getRepository(SystemConfig);

export async function getConfigValue(key: string): Promise<string> {
  const row = await repo().findOneBy({ cfgKey: key });
  return row?.cfgValue ?? DEFAULTS[key] ?? '';
}

/** 仅落库，不触发公告广播（公告发布/定时逻辑显式控制推送时机）。 */
async function saveConfig(key: string, value: string): Promise<void> {
  let row = await repo().findOneBy({ cfgKey: key });
  if (!row) {
    row = repo().create({ cfgKey: key, cfgValue: value });
  } else {
    row.cfgValue = value;
  }
  await repo().save(row);
}

/**
 * 在线判定窗口（分钟）= 心跳间隔 + 宽限阈值。
 * 客户端按 heartbeat_interval_hours 周期上报心跳，若判定窗口小于一个心跳周期，
 * 心跳间隙内的正常在线设备会被误判为离线，导致"在线设备"数量长期偏低。
 * 因此窗口必须不小于一个心跳周期，online_threshold_minutes 作为漏报一次的宽限。
 */
export async function getOnlineWindowMinutes(): Promise<number> {
  const intervalHours = Number(await getConfigValue('heartbeat_interval_hours')) || 6;
  const graceMin = parseInt(await getConfigValue('online_threshold_minutes'), 10) || 30;
  return intervalHours * 60 + graceMin;
}

export async function getConfigAll(): Promise<Record<string, string>> {
  const rows = await repo().find();
  const map: Record<string, string> = { ...DEFAULTS };
  for (const r of rows) map[r.cfgKey] = r.cfgValue;
  return map;
}

export async function setConfigValue(key: string, value: string): Promise<void> {
  await saveConfig(key, value);
  // 兼容旧版管理端：直接写 notice 视为“立即发布纯标题公告”
  if (key === 'notice') {
    const title = value.trim();
    if (!title) {
      await saveConfig('notice_payload', '');
      await saveConfig('notice_pending', '');
      broadcastNotice(toNoticeEvent(null));
      return;
    }
    const payload: NoticePayload = { title, content: '', strong: false, link: '', publishedAt: Date.now() };
    await saveConfig('notice_payload', JSON.stringify(payload));
    await saveConfig('notice_pending', '');
    broadcastNotice(toNoticeEvent(payload));
  }
}

// ===========================================================================
// 系统公告：草稿 / 立即发送 / 定时发送 / 撤销
// ===========================================================================

/** 公告内容（草稿与发布共用） */
export interface NoticeDraft extends NoticeData {}

/** 已发布公告（带发布时间戳，秒级） */
export interface NoticePayload extends NoticeData {
  publishedAt: number;
}

/** 待定时发布任务 */
export interface PendingNotice {
  payload: NoticePayload;
  publishAt: number;
}

function parseJson<T>(raw: string): T | null {
  if (!raw) return null;
  try {
    const v = JSON.parse(raw);
    return v && typeof v === 'object' ? (v as T) : null;
  } catch {
    return null;
  }
}

/** 取正文首个非空行（作为标题兜底 / 老客户端兼容串） */
function firstLine(text: string, maxLen = 80): string {
  const line = (String(text || '').split(/\r?\n/).find((s) => s.trim()) || '').trim();
  return line.length > maxLen ? line.slice(0, maxLen) : line;
}

function normalizeData(raw: Partial<NoticeData> | null | undefined): NoticeData | null {
  if (!raw) return null;
  const data: NoticeData = {
    title: String(raw.title || ''),
    content: String(raw.content || ''),
    strong: !!raw.strong,
    link: String(raw.link || ''),
  };
  if (!data.title && !data.content && !data.link) return null;
  return data;
}

function toNoticeEvent(payload: NoticePayload | null): NoticeEvent {
  const data = normalizeData(payload);
  if (!data) return { notice: '', noticeData: null };
  // 老客户端只有一行文本：优先标题，其次正文首行
  return { notice: data.title || firstLine(data.content, 500), noticeData: data };
}

/** 读取当前草稿（管理端编辑区回显） */
export async function getNoticeDraft(): Promise<NoticeDraft> {
  const [title, content, strong, link] = await Promise.all([
    getConfigValue('notice_title'),
    getConfigValue('notice_content'),
    getConfigValue('notice_strong'),
    getConfigValue('notice_link'),
  ]);
  return { title, content, strong: strong === '1', link };
}

/** 保存草稿（不推送、不影响已发布公告） */
export async function saveNoticeDraft(draft: NoticeDraft): Promise<void> {
  await Promise.all([
    saveConfig('notice_title', draft.title || ''),
    saveConfig('notice_content', draft.content || ''),
    saveConfig('notice_strong', draft.strong ? '1' : '0'),
    saveConfig('notice_link', draft.link || ''),
  ]);
}

/** 读取已发布公告；老库只有 notice 字符串时自动兜底为纯标题公告 */
export async function getNoticePayload(): Promise<NoticePayload | null> {
  const parsed = parseJson<NoticePayload>(await getConfigValue('notice_payload'));
  const data = normalizeData(parsed);
  if (data) {
    return { ...data, publishedAt: Number(parsed?.publishedAt) || 0 };
  }
  const legacy = (await getConfigValue('notice')).trim();
  if (!legacy) return null;
  return { title: legacy, content: '', strong: false, link: '', publishedAt: 0 };
}

/** 读取待定时发布任务 */
export async function getPendingNotice(): Promise<PendingNotice | null> {
  const parsed = parseJson<PendingNotice>(await getConfigValue('notice_pending'));
  if (!parsed) return null;
  const data = normalizeData(parsed.payload);
  const publishAt = Number(parsed.publishAt) || 0;
  if (!data || publishAt <= 0) return null;
  return { payload: { ...data, publishedAt: publishAt }, publishAt };
}

/** 心跳/SSE 统一下发内容（notice 兼容串 + noticeData 结构化数据） */
export async function getNoticeEvent(): Promise<NoticeEvent> {
  return toNoticeEvent(await getNoticePayload());
}

function buildPayload(draft: NoticeDraft, publishedAt: number): NoticePayload | null {
  const data = normalizeData(draft);
  if (!data) return null;
  return {
    title: data.title || firstLine(data.content, 80),
    content: data.content,
    strong: data.strong,
    link: data.link,
    publishedAt,
  };
}

/** 公告发布结果：event 为下发内容，delivered 为实际送达的在线连接数 */
export interface NoticePublishResult {
  event: NoticeEvent;
  delivered: number;
}

/** 立即发送：把当前草稿发布为线上公告并实时推送（草稿为空等价于撤销） */
export async function publishNotice(): Promise<NoticePublishResult> {
  const draft = await getNoticeDraft();
  const payload = buildPayload(draft, Date.now());
  if (!payload) return withdrawNotice();
  await saveConfig('notice_payload', JSON.stringify(payload));
  await saveConfig('notice', payload.title);
  await saveConfig('notice_pending', '');
  const event = toNoticeEvent(payload);
  const delivered = broadcastNotice(event);
  log.info(`系统公告已发布：${payload.title || '(无标题)'}（送达 ${delivered} 个在线连接）`);
  return { event, delivered };
}

/** 定时发送：保存草稿并登记待发布任务，由每分钟的定时任务到点推送 */
export async function scheduleNotice(publishAt: number): Promise<PendingNotice | null> {
  const draft = await getNoticeDraft();
  const payload = buildPayload(draft, publishAt);
  if (!payload) {
    await saveConfig('notice_pending', '');
    return null;
  }
  const pending: PendingNotice = { payload, publishAt };
  await saveConfig('notice_pending', JSON.stringify(pending));
  log.info(`系统公告已登记定时发送：${new Date(publishAt).toISOString()}`);
  return pending;
}

/** 取消定时发送（不影响已发布公告） */
export async function cancelScheduledNotice(): Promise<void> {
  await saveConfig('notice_pending', '');
}

/** 撤销公告：清空已发布内容并实时通知客户端隐藏横幅 */
export async function withdrawNotice(): Promise<NoticePublishResult> {
  await saveConfig('notice_payload', '');
  await saveConfig('notice', '');
  await saveConfig('notice_pending', '');
  const event = toNoticeEvent(null);
  const delivered = broadcastNotice(event);
  log.info(`系统公告已撤销（送达 ${delivered} 个在线连接）`);
  return { event, delivered };
}

/** 定时任务入口：待发布任务到点即发布（每分钟调用一次） */
export async function processScheduledNotice(): Promise<void> {
  const pending = await getPendingNotice();
  if (!pending) return;
  if (pending.publishAt > Date.now()) return;
  const payload = pending.payload;
  await saveConfig('notice_payload', JSON.stringify(payload));
  await saveConfig('notice', payload.title);
  await saveConfig('notice_pending', '');
  broadcastNotice(toNoticeEvent(payload));
  log.info(`定时公告已自动发布：${payload.title || '(无标题)'}`);
}
