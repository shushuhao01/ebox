import { get, put } from './request'

export interface SystemConfig {
  heartbeat_interval_hours: string
  offline_grace_days: string
  force_online_activate: string
  notice: string
  online_threshold_minutes: string
}

/** 读取系统设置（字符串值） */
export function getConfig() {
  return get<SystemConfig>('/config')
}

/** 更新系统设置 */
export function updateConfig(data: Partial<SystemConfig>) {
  return put<SystemConfig>('/config', data)
}
