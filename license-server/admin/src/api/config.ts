import { get, post, put } from './request'

export interface SystemConfig {
  heartbeat_interval_hours: string
  offline_grace_days: string
  force_online_activate: string
  notice: string
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

/** 更新系统设置 */
export function updateConfig(data: Partial<SystemConfig>) {
  return put<SystemConfig>('/config', data)
}

/** 立即清理过期操作日志 */
export function cleanLogs() {
  return post<{ deleted: number }>('/config/clean-logs')
}
