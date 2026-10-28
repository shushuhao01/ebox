import { get, post, put } from './request'

/** 结构化公告内容（客户端横幅 + 详情弹窗 + 链接跳转） */
export interface NoticeData {
  title: string
  content: string
  strong: boolean
  link: string
}

/** 已发布公告（publishedAt 为毫秒时间戳） */
export interface NoticePayload extends NoticeData {
  publishedAt: number
}

/** 待定时发布任务 */
export interface PendingNotice {
  payload: NoticePayload
  publishAt: number
}

/** 公告状态（发送/撤销等操作后的回显） */
export interface NoticeState {
  notice_published: NoticePayload | null
  notice_pending_task: PendingNotice | null
}

export interface SystemConfig extends NoticeState {
  heartbeat_interval_hours: string
  offline_grace_days: string
  force_online_activate: string
  notice: string
  /** 公告草稿：标题（≤80） */
  notice_title: string
  /** 公告草稿：正文（≤2000） */
  notice_content: string
  /** 公告草稿：是否强提醒（'1' / '0'） */
  notice_strong: string
  /** 公告草稿：跳转链接 */
  notice_link: string
  online_threshold_minutes: string
  /** 操作日志保留天数 */
  log_retention_days: string
  /** 每日日志自动清理时间 HH:mm */
  log_clean_time: string
}

/** 读取系统设置（字符串值） */
export function getConfig() {
  return get<SystemConfig>('/config')
}

/** 更新系统设置（仅保存，不推送公告） */
export function updateConfig(data: Partial<SystemConfig>) {
  return put<SystemConfig>('/config', data)
}

/** 立即发送公告（先保存草稿，再实时推送到在线客户端） */
export function sendNotice(data: NoticeData) {
  return post<NoticeState>('/config/notice/send', data)
}

/** 定时发送公告（sendAt 为 ISO 时间字符串） */
export function scheduleNotice(data: NoticeData & { sendAt: string }) {
  return post<NoticeState>('/config/notice/schedule', data)
}

/** 取消定时发送 */
export function cancelScheduledNotice() {
  return post<NoticeState>('/config/notice/cancel')
}

/** 撤销公告（客户端立即隐藏公告栏） */
export function withdrawNotice() {
  return post<NoticeState>('/config/notice/withdraw')
}

/** 立即清理过期日志（操作日志 + 心跳日志） */
export function cleanLogs() {
  return post<{ operationLogs: number; heartbeats: number }>('/config/clean-logs')
}
