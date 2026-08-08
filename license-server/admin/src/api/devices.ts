import { get, post } from './request'
import type { PageResult } from './request'

export interface Device {
  id: string
  keyId: string
  machineFp: string
  status: number
  firstActivateAt: string | null
  lastOnlineAt: string | null
  lastHeartbeatAt: string | null
  lastIp: string | null
  osInfo: string | null
  appVersion: string | null
  unbindAt: string | null
}

export type DeviceMode = 'all' | 'online' | 'offline' | 'kicked' | 'unbound'

/** 设备分页列表 */
export function getDevices(params: {
  page?: number
  pageSize?: number
  mode?: DeviceMode
  search?: string
}) {
  return get<PageResult<Device>>('/devices', params)
}

/** 在线设备 */
export function getOnlineDevices() {
  return get<Device[]>('/devices/online')
}

/** 踢下线 */
export function kickDevice(id: string) {
  return post<{ id: string }>(`/devices/${id}/kick`)
}
