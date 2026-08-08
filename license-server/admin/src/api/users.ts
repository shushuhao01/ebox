import { get, post, put } from './request'

export interface AdminUser {
  id: string
  username: string
  nickname: string | null
  role: number
  status: number
  lastLoginAt: string | null
  createdAt: string
}

/** 管理员列表（仅超管） */
export function getUsers() {
  return get<AdminUser[]>('/users')
}

/** 新建管理员（仅超管） */
export function createUser(data: { username: string; password: string; nickname?: string; role: 0 | 1 }) {
  return post<{ id: string; username: string }>('/users', data)
}

/** 修改密码 */
export function changePassword(data: { oldPassword: string; newPassword: string }) {
  return put<null>('/users/password', data)
}

/** 停用/启用管理员（仅超管） */
export function setUserStatus(id: string, status: 0 | 1) {
  return put<null>(`/users/${id}/status`, { status })
}
