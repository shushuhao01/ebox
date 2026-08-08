import { get } from './request'
import type { PageResult } from './request'

export interface Heartbeat {
  id: string
  keyId: string
  deviceId: string | null
  action: number
  clientTime: string
  ip: string | null
  appVersion: string | null
  detail: string | null
  createdAt: string
}

export interface UnbindLog {
  id: string
  keyId: string
  deviceId: string
  month: string
  newKeyId: string | null
  createdAt: string
}

export interface RevokeLog {
  id: string
  keyId: string
  operatorId: string
  reason: string | null
  createdAt: string
}

export interface OperationLog {
  id: string
  userId: string
  action: string
  target: string | null
  detail: string | null
  ip: string | null
  createdAt: string
}

/** 心跳/流水日志（action: 1激活 2心跳 3解绑 4被踢） */
export function getHeartbeats(params: {
  page?: number
  pageSize?: number
  action?: number | string
  code?: string
}) {
  return get<PageResult<Heartbeat>>('/heartbeats', params)
}

/** 换机记录 */
export function getUnbindLogs(params: { page?: number; pageSize?: number; month?: string }) {
  return get<PageResult<UnbindLog>>('/unbind-logs', params)
}

/** 作废记录 */
export function getRevokeLogs(params: { page?: number; pageSize?: number }) {
  return get<PageResult<RevokeLog>>('/revoke-logs', params)
}

/** 操作日志 */
export function getOperationLogs(params: { page?: number; pageSize?: number; action?: string; userId?: string }) {
  return get<PageResult<OperationLog>>('/operation-logs', params)
}
