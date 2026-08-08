import { get, post, del } from './request'
import type { PageResult } from './request'

export interface LicenseKeyItem {
  id: string
  code: string
  codeFp?: string
  type: number
  durationSec: string
  expireAt: string | null
  bound: number
  unbindMax: number
  status: number
  batchId: string | null
  customerId: string | null
  remark: string | null
  createdAt: string
  usedAt: string | null
  revokedAt: string | null
  revokedBy: string | null
  revokedReason: string | null
  convertFromKeyId: string | null
}

export interface KeyDevice {
  id: string
  keyId: string
  machineFp: string
  status: number
  firstActivateAt: string | null
  lastOnlineAt: string | null
  lastIp: string | null
  osInfo: string | null
  appVersion: string | null
}

export interface GenerateParams {
  durationSec: number
  bound: boolean
  unbindMax: number
  count: number
  customerId?: string | null
  batchId?: string | null
  batchName?: string | null
  remark?: string | null
}

export interface KeyListParams {
  page?: number
  pageSize?: number
  status?: number | string
  type?: number | string
  bound?: number | string
  batchId?: string
  customerId?: string
  start?: string
  end?: string
  search?: string
}

/** 批量生成激活码 */
export function generateKeys(data: GenerateParams) {
  return post<{ codes: string[]; batchId: string | null; count: number }>('/keys/generate', data)
}

/** 激活码分页列表 */
export function getKeys(params: KeyListParams) {
  return get<PageResult<LicenseKeyItem>>('/keys', params)
}

/** 作废激活码 */
export function revokeKey(id: string, reason: string) {
  return post<{ id: string; code: string; status: number }>(`/keys/${id}/revoke`, { reason })
}

/** 恢复激活码 */
export function restoreKey(id: string) {
  return post<{ id: string; code: string; status: number }>(`/keys/${id}/restore`)
}

/** 批量作废激活码 */
export function batchRevokeKeys(ids: string[], reason: string) {
  return post<{ okCount: number; failCount: number }>('/keys/batch-revoke', { ids, reason })
}

/** 批量删除激活码（仅已作废，软删除） */
export function batchDeleteKeys(ids: string[]) {
  return post<{ deleted: number }>('/keys/batch-delete', { ids })
}

/** 删除单个激活码（仅已作废，软删除） */
export function deleteKey(id: string) {
  return del<{ id: string; code: string; status: number }>(`/keys/${id}`)
}

/** 回收站批量恢复（已删除 → 作废） */
export function batchRestoreKeys(ids: string[]) {
  return post<{ restored: number }>('/keys/batch-restore', { ids })
}

/** 签发换机码 */
export function convertKey(id: string) {
  return post<{ id: string; code: string; expireAt: string | null }>(`/keys/${id}/convert`)
}

/** 校验激活码 */
export function verifyKey(code: string) {
  return post('/keys/verify', { code })
}

/** 导出激活码 CSV（返回 Blob） */
export function exportKeys(params?: Record<string, unknown>) {
  return request.get('/keys/export', { params, responseType: 'blob' }) as Promise<Blob>
}

/** 激活码详情（含设备列表） */
export function getKeyDetail(id: string) {
  return get<{ key: LicenseKeyItem; devices: KeyDevice[]; unbindCount: number }>(`/keys/${id}`)
}

/**
 * 批量解析 keyId -> code（设备/日志等页面展示激活码用）
 * 后端 join 表不返回 code 字段，需逐条取详情，采用并发分片避免请求过多
 */
export async function getKeyCodeMap(ids: Array<string | null | undefined>): Promise<Record<string, string>> {
  const uniq = Array.from(new Set(ids.filter((v): v is string => !!v)))
  const map: Record<string, string> = {}
  for (let i = 0; i < uniq.length; i += 10) {
    const chunk = uniq.slice(i, i + 10)
    const results = await Promise.allSettled(chunk.map((id) => getKeyDetail(id)))
    results.forEach((r, idx) => {
      if (r.status === 'fulfilled') {
        map[r.value.key.id] = r.value.key.code
      }
    })
  }
  return map
}
