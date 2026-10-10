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
  linkUrl: string | null
}

export interface RelatedArticle {
  id: string
  slug: string
  title: string
  cover: string | null
}

/** 最新公告（公告管理里最新一条已发布公告） */
export interface LatestNotice {
  id: string
  slug: string
  title: string
  summary: string | null
  linkUrl: string | null
  pinned: number
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

// SSG 构建期在 Node 环境执行，相对路径无法发请求，需指向后端绝对地址；
// 浏览器端保持同源相对路径（走 nginx /api 反代）。
const ssgApiBase = import.meta.env.VITE_SSG_API_BASE || 'http://127.0.0.1:3008'

const http = axios.create({
  baseURL: import.meta.env.SSR ? ssgApiBase : '',
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

/** 最新公告（顶部公告条）：发布后需即时生效，加时间戳绕过缓存 */
export function getLatestNotice(): Promise<LatestNotice | null> {
  return http.get('/api/site/notice', {
    params: { _t: Date.now() },
  }) as unknown as Promise<LatestNotice | null>
}

/** SSG 构建期使用：分页拉取全部已发布文章 slug（接口 size 上限 50，需循环） */
export async function getAllArticleSlugs(): Promise<string[]> {
  const size = 50
  const slugs: string[] = []
  let page = 1
  // 兜底上限，避免后端异常导致死循环
  for (let guard = 0; guard < 200; guard += 1) {
    const res = await getArticles({ page, size })
    for (const item of res.list || []) {
      if (item.slug) slugs.push(item.slug)
    }
    const total = res.total || 0
    if (page * size >= total || !res.list || res.list.length === 0) break
    page += 1
  }
  return slugs
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

/** 渠道短链解析结果 */
export interface ChannelResolveResult {
  code: string
  name: string
  channel: string
  targetPath: string
}

/** 渠道编码归一化（与后端保持一致：去空格 + 转小写 + 限长 32） */
export function normalizeChannel(v: unknown): string {
  return String(v ?? '').trim().toLowerCase().slice(0, 32)
}

const CH_COOKIE = 'ch_ref'
const CH_TTL_DAYS = 30

/** 写入渠道来源 Cookie（30 天归因窗口，path=/ 全站可用） */
export function setChannelCookie(code: string): void {
  if (typeof document === 'undefined') return
  const c = normalizeChannel(code)
  if (!c) return
  const maxAge = CH_TTL_DAYS * 24 * 60 * 60
  document.cookie = `${CH_COOKIE}=${encodeURIComponent(c)}; path=/; max-age=${maxAge}; SameSite=Lax`
}

/** 读取渠道来源 Cookie */
export function getChannelCode(): string {
  if (typeof document === 'undefined') return ''
  const m = document.cookie.match(new RegExp(`(?:^|;\\s*)${CH_COOKIE}=([^;]+)`))
  return m ? normalizeChannel(decodeURIComponent(m[1])) : ''
}

/** 解析当前渠道码：URL ?ch= 优先（并写入 Cookie），其次读取 Cookie */
export function resolveChannelCode(): string {
  if (typeof window === 'undefined') return ''
  try {
    const fromUrl = normalizeChannel(new URLSearchParams(window.location.search).get('ch'))
    if (fromUrl) {
      setChannelCookie(fromUrl)
      return fromUrl
    }
  } catch {
    // 忽略 URL 解析异常
  }
  return getChannelCode()
}

/** 解析渠道短链：命中返回跳转信息，否则抛错 */
export function resolveChannel(code: string): Promise<ChannelResolveResult> {
  return http.get(`/api/site/channel/resolve/${encodeURIComponent(normalizeChannel(code))}`) as unknown as Promise<ChannelResolveResult>
}

/** 埋点：pv / uv / download / buy（失败不影响前台） */
export function track(type: 'pv' | 'uv' | 'download' | 'buy'): void {
  http.post('/api/site/track', { type, channelCode: resolveChannelCode() }).catch(() => undefined)
}

/** 生成 / 读取本地访客标识（用于 UV 去重，仅存浏览器本地） */
function getVisitorId(): string {
  if (typeof localStorage === 'undefined') return ''
  try {
    let id = localStorage.getItem('ebox_vid')
    if (!id) {
      id = `${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`
      localStorage.setItem('ebox_vid', id)
    }
    return id
  } catch {
    return ''
  }
}

/** 页面访问埋点：携带路径 / 来源 / 访客标识 / 渠道码，供后台流量分析（失败不影响前台） */
export function trackPageview(path: string): void {
  if (typeof window === 'undefined') return
  http
    .post('/api/site/track', {
      type: 'pv',
      path: path || window.location.pathname + window.location.search,
      referer: document.referrer || '',
      visitorId: getVisitorId(),
      channelCode: resolveChannelCode(),
    })
    .catch(() => undefined)
}

/** 在线心跳：停留页面期间定期上报，供后台「实时在线」统计（不计入 PV，失败不影响前台） */
export function sendHeartbeat(): void {
  if (typeof window === 'undefined') return
  http
    .post('/api/site/track', { type: 'hb', visitorId: getVisitorId() })
    .catch(() => undefined)
}
