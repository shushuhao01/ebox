import { get } from './request'
import type { Heartbeat } from './logs'

export interface Overview {
  total: number
  used: number
  revoked: number
  switched: number
  onlineDevices: number
  todayActivate: number
}

export interface Trend {
  dates: string[]
  activates: number[]
  heartbeats: number[]
}

export interface Distribution {
  durationMap: Record<string, number>
  statusMap: Record<string, number>
  unbindCount: number
}

/** 总览统计 */
export function getOverview() {
  return get<Overview>('/stats/overview')
}

/** 激活/心跳趋势 */
export function getTrend(days: number) {
  return get<Trend>('/stats/trend', { days })
}

/** 分布统计 */
export function getDistribution() {
  return get<Distribution>('/stats/distribution')
}

/** 最近心跳流水 */
export function getRecentHeartbeats(limit = 10) {
  return get<Heartbeat[]>('/stats/recent-heartbeats', { limit })
}
