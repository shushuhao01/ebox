import axios, { type AxiosRequestConfig, type AxiosResponse } from 'axios'
import { ElMessage } from 'element-plus'
import router from '@/router'

export interface ApiResponse<T = unknown> {
  code: number
  msg: string
  data: T
}

export interface PageResult<T = unknown> {
  total: number
  page: number
  pageSize: number
  list: T[]
}

const TOKEN_KEY = 'ebox_token'

const request = axios.create({
  baseURL: '/api/admin',
  timeout: 30000,
})

// 请求拦截：附加 token
request.interceptors.request.use((config) => {
  const token = localStorage.getItem(TOKEN_KEY)
  if (token) {
    config.headers.Authorization = `Bearer ${token}`
  }
  return config
})

function redirectLogin(msg?: string) {
  localStorage.removeItem(TOKEN_KEY)
  localStorage.removeItem('ebox_user')
  if (router.currentRoute.value.path !== '/login') {
    router.push({ path: '/login', query: { redirect: router.currentRoute.value.fullPath } })
  }
  ElMessage.error(msg || '登录已过期，请重新登录')
}

// 响应拦截：解包 {code, msg, data}；401 跳登录
request.interceptors.response.use(
  (response: AxiosResponse) => {
    // 文件流（CSV 导出）直接返回
    if (response.config.responseType === 'blob') {
      return response.data
    }
    const res = response.data as ApiResponse
    if (res.code === 0) {
      return res.data
    }
    if (res.code === 401) {
      redirectLogin(res.msg)
      return Promise.reject(new Error(res.msg))
    }
    ElMessage.error(res.msg || '请求失败')
    return Promise.reject(new Error(res.msg || '请求失败'))
  },
  (error) => {
    const status = error.response?.status
    if (status === 401 || error.response?.data?.code === 401) {
      redirectLogin(error.response?.data?.msg)
    } else {
      ElMessage.error(error.response?.data?.msg || error.message || '网络错误')
    }
    return Promise.reject(error)
  }
)

export function get<T = unknown>(url: string, params?: Record<string, unknown>): Promise<T> {
  return request.get(url, { params }) as Promise<T>
}

export function post<T = unknown>(url: string, data?: unknown, config?: AxiosRequestConfig): Promise<T> {
  return request.post(url, data, config) as Promise<T>
}

export function put<T = unknown>(url: string, data?: unknown): Promise<T> {
  return request.put(url, data) as Promise<T>
}

export function del<T = unknown>(url: string, params?: Record<string, unknown>): Promise<T> {
  return request.delete(url, { params }) as Promise<T>
}

export default request
