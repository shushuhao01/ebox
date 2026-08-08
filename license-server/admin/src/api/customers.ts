import { del, get, post, put } from './request'
import type { PageResult } from './request'
import type { LicenseKeyItem } from './keys'

export interface Customer {
  id: string
  name: string
  phone: string | null
  wechat: string | null
  qq: string | null
  source: string | null
  remark: string | null
  status: number
  createdAt: string
  keyCount?: number
  deviceCount?: number
}

export interface CustomerForm {
  name: string
  phone?: string
  wechat?: string
  qq?: string
  source?: string
  remark?: string
}

/** 客户分页列表 */
export function getCustomers(params: {
  page?: number
  pageSize?: number
  search?: string
  status?: number | string
}) {
  return get<PageResult<Customer>>('/customers', params)
}

/** 新建客户 */
export function createCustomer(data: CustomerForm) {
  return post<Customer>('/customers', data)
}

/** 编辑客户 */
export function updateCustomer(id: string, data: CustomerForm & { status?: number }) {
  return put<Customer>(`/customers/${id}`, data)
}

/** 停用客户 */
export function deleteCustomer(id: string) {
  return del<{ id: string }>(`/customers/${id}`)
}

/** 客户名下激活码 */
export function getCustomerKeys(id: string) {
  return get<LicenseKeyItem[]>(`/customers/${id}/keys`)
}
