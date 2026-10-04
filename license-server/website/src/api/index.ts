import axios, { type AxiosResponse } from 'axios'

export interface ApiResponse<T = unknown> {
  code: number
  msg: string
  data: T
}

/** 站点设置（后台 KV，值均为字符串） */
export type SiteSettings = Record<string, string>

export interface SiteNav {
  id: string
  position: string
  label: string
  url: string
  target: string
  sort: number
  visible: number
}

export interface ArticleBrief {
  id: string
  slug: string
  title: string
  category: string
  cover: string | null
  summary: string | null
  pinned: number
  publishAt: string | null
  createdAt: string
  views: number
}

export interface SiteArticle extends ArticleBrief {
  content: string | null
  contentType: string
  seoTitle: string | null
  seoDesc: string | null
}

export interface RelatedArticle {
  id: string
  slug: string
  title: string
  cover: string | null
}

export interface SiteCase {
  id: string
  industry: string
  title: string
  summary: string | null
  content: string | null
  avatar: string | null
  metrics: string | null
  sort: number
  status: string
}

export interface SiteContact {
  id: string
  type: string
  name: string
  value: string | null
  qrcode: string | null
  sort: number
  enabled: number
}

export interface DownloadMirror {
  id: string
  name: string
  url: string
  type: string
  password: string | null
  extractCode: string | null
}

export interface LatestRelease {
  latestVersion: string
  latestVersionCode: number
  releaseDate: string
  downloadUrl: string
  downloadSha256: string
  downloadSize: number
  changelog: string[]
  mirrors: DownloadMirror[]
}

const http = axios.create({
  baseURL: '',
  timeout: 15000,
})

// 响应拦截：解包统一响应体 { code, msg, data }
http.interceptors.response.use(
  (response: AxiosResponse) => {
    const body = response.data as ApiResponse
    if (body && typeof body === 'object' && 'code' in body) {
      if (body.code === 0) return body.data as never
      return Promise.reject(new Error(body.msg || '请求失败'))
    }
    return response.data as never
  },
  (error) => Promise.reject(error),
)

export function getSiteSettings(): Promise<SiteSettings> {
  return http.get('/api/site/settings') as unknown as Promise<SiteSettings>
}

export function getNavList(position?: string): Promise<SiteNav[]> {
  return http.get('/api/site/nav', { params: position ? { position } : {} }) as unknown as Promise<SiteNav[]>
}

export function getArticles(params: { category?: string; page?: number; size?: number } = {}): Promise<{
  total: number
  page: number
  size: number
  list: ArticleBrief[]
}> {
  return http.get('/api/site/articles', { params }) as unknown as Promise<{
    total: number
    page: number
    size: number
    list: ArticleBrief[]
  }>
}

export function getArticle(slug: string): Promise<{ article: SiteArticle; related: RelatedArticle[] }> {
  return http.get(`/api/site/articles/${encodeURIComponent(slug)}`) as unknown as Promise<{
    article: SiteArticle
    related: RelatedArticle[]
  }>
}

export function getCaseList(industry?: string): Promise<SiteCase[]> {
  return http.get('/api/site/cases', { params: industry ? { industry } : {} }) as unknown as Promise<SiteCase[]>
}

export function getCaseDetail(id: string): Promise<SiteCase> {
  return http.get(`/api/site/cases/${encodeURIComponent(id)}`) as unknown as Promise<SiteCase>
}

export function getContacts(): Promise<SiteContact[]> {
  return http.get('/api/site/contacts') as unknown as Promise<SiteContact[]>
}

export function getLatestRelease(): Promise<LatestRelease> {
  return http.get('/api/site/latest-release') as unknown as Promise<LatestRelease>
}

/** 埋点：pv / uv / download / buy（失败不影响前台） */
export function track(type: 'pv' | 'uv' | 'download' | 'buy'): void {
  http.post('/api/site/track', { type }).catch(() => undefined)
}
