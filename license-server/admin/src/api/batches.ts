import { get, post, del } from './request'
import type { PageResult } from './request'
import type { LicenseKeyItem } from './keys'

export interface KeyBatch {
  id: string
  name: string
  remark: string | null
  createdBy: string
  createdAt: string
  totalCount?: number
  usedCount?: number
}

/** 批次分页列表 */
export function getBatches(params: { page?: number; pageSize?: number }) {
  return get<PageResult<KeyBatch>>('/batches', params)
}

/** 新建批次 */
export function createBatch(data: { name: string; remark?: string }) {
  return post<KeyBatch>('/batches', data)
}

/** 批次详情（含全部激活码） */
export function getBatchDetail(id: string) {
  return get<{ batch: KeyBatch; keys: LicenseKeyItem[] }>(`/batches/${id}`)
}

/** 删除批次（批次下激活码保留并解除归属） */
export function deleteBatch(id: string) {
  return del<{ id: string }>(`/batches/${id}`)
}
