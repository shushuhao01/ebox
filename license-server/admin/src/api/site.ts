import request, { get, post, put, del, type PageResult } from './request'

// ==================== 类型定义 ====================

export interface SiteArticle {
  id: string
  slug: string
  title: string
  category: string
  cover: string | null
  summary: string | null
  content: string | null
  contentType: string
  status: string
  pinned: number
  publishAt: string | null
  seoTitle: string | null
  seoDesc: string | null
  views: number
  createdAt: string
  updatedAt: string | null
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
  createdAt: string
  updatedAt: string | null
}

export interface SiteNav {
  id: string
  position: string
  label: string
  url: string
  target: string
  sort: number
  visible: number
  createdAt: string
}

export interface SiteContact {
  id: string
  type: string
  name: string
  value: string | null
  qrcode: string | null
  sort: number
  enabled: number
  createdAt: string
}

export interface SiteMirror {
  id: string
  name: string
  url: string
  type: string
  password: string | null
  extractCode: string | null
  sort: number
  enabled: number
  createdAt: string
}

export interface ReleaseConfig {
  release_version: string
  release_date: string
  release_changelog: string
  release_file_url: string
  release_file_name: string
  release_file_size: string
  release_file_sha256: string
}

export interface SiteAccessRule {
  id: string
  type: string
  pattern: string
  note: string | null
  enabled: number
  createdAt: string
}

export interface SiteMedia {
  id: string
  filename: string
  path: string
  size: number
  mime: string | null
  width: number | null
  height: number | null
  uploadedBy: string | null
  createdAt: string
}

export interface SiteStatsDaily {
  id: string
  statDate: string
  pv: number
  uv: number
  downloads: number
  buyClicks: number
}

// ==================== 站点设置 ====================

export function getSiteSettings() {
  return get<Record<string, string>>('/site/settings')
}

export function updateSiteSettings(patch: Record<string, string>) {
  return put<Record<string, string>>('/site/settings', patch)
}

export function getSiteSecurity() {
  return get<{ maintenance_mode: string; access_password: string }>('/site/security')
}

export function updateSiteSecurity(data: { maintenance_mode: string; access_password: string }) {
  return put<{ maintenance_mode: string; access_password: string }>('/site/security', data)
}

// ==================== 文章 ====================

export function getArticles(params: { page?: number; pageSize?: number; category?: string; status?: string }) {
  return get<PageResult<SiteArticle>>('/site/articles', params)
}

export function getArticle(id: string) {
  return get<SiteArticle>(`/site/articles/${id}`)
}

export function createArticle(data: Partial<SiteArticle>) {
  return post<SiteArticle>('/site/articles', data)
}

export function updateArticle(id: string, data: Partial<SiteArticle>) {
  return put<SiteArticle>(`/site/articles/${id}`, data)
}

export function deleteArticle(id: string) {
  return del<{ id: string }>(`/site/articles/${id}`)
}

// ==================== 案例 ====================

export function getCases(params: { page?: number; pageSize?: number }) {
  return get<PageResult<SiteCase>>('/site/cases', params)
}

export function createCase(data: Partial<SiteCase>) {
  return post<SiteCase>('/site/cases', data)
}

export function updateCase(id: string, data: Partial<SiteCase>) {
  return put<SiteCase>(`/site/cases/${id}`, data)
}

export function deleteCase(id: string) {
  return del<{ id: string }>(`/site/cases/${id}`)
}

// ==================== 导航 ====================

export function getNavList() {
  return get<SiteNav[]>('/site/nav')
}

export function createNav(data: Partial<SiteNav>) {
  return post<SiteNav>('/site/nav', data)
}

export function updateNav(id: string, data: Partial<SiteNav>) {
  return put<SiteNav>(`/site/nav/${id}`, data)
}

export function deleteNav(id: string) {
  return del<{ id: string }>(`/site/nav/${id}`)
}

// ==================== 联系方式 ====================

export function getContactList() {
  return get<SiteContact[]>('/site/contacts')
}

export function createContact(data: Partial<SiteContact>) {
  return post<SiteContact>('/site/contacts', data)
}

export function updateContact(id: string, data: Partial<SiteContact>) {
  return put<SiteContact>(`/site/contacts/${id}`, data)
}

export function deleteContact(id: string) {
  return del<{ id: string }>(`/site/contacts/${id}`)
}

// ==================== 下载地址 ====================

export function getMirrorList() {
  return get<SiteMirror[]>('/site/mirrors')
}

export function createMirror(data: Partial<SiteMirror>) {
  return post<SiteMirror>('/site/mirrors', data)
}

export function updateMirror(id: string, data: Partial<SiteMirror>) {
  return put<SiteMirror>(`/site/mirrors/${id}`, data)
}

export function deleteMirror(id: string) {
  return del<{ id: string }>(`/site/mirrors/${id}`)
}

// ==================== 访问规则 ====================

export function getAccessRules() {
  return get<SiteAccessRule[]>('/site/access-rules')
}

export function createAccessRule(data: Partial<SiteAccessRule>) {
  return post<SiteAccessRule>('/site/access-rules', data)
}

export function updateAccessRule(id: string, data: Partial<SiteAccessRule>) {
  return put<SiteAccessRule>(`/site/access-rules/${id}`, data)
}

export function deleteAccessRule(id: string) {
  return del<{ id: string }>(`/site/access-rules/${id}`)
}

// ==================== 媒体库 ====================

export function getMediaList(params: { page?: number; pageSize?: number }) {
  return get<PageResult<SiteMedia>>('/site/media', params)
}

export function deleteMedia(id: string) {
  return del<{ id: string }>(`/site/media/${id}`)
}

/** 上传图片：以原始二进制提交，文件名通过 query 传递 */
export function uploadSiteImage(file: File) {
  const url = `/site/media/upload?filename=${encodeURIComponent(file.name)}`
  return request.post(url, file, {
    headers: { 'Content-Type': file.type || 'application/octet-stream' },
    timeout: 60000,
  }) as unknown as Promise<{ id: string; url: string; filename: string }>
}

// ==================== 版本 / 统计 ====================

export function syncRelease() {
  return post<{ synced: boolean }>('/site/release/sync')
}

/** 读取官网直下配置（版本号 / 更新日志 / 安装包信息） */
export function getReleaseConfig() {
  return get<ReleaseConfig>('/site/release')
}

/** 保存官网直下配置（不含文件本身） */
export function saveReleaseConfig(data: Partial<ReleaseConfig>) {
  return put<ReleaseConfig>('/site/release', data)
}

/** 上传安装包（exe / zip）：以原始二进制提交，文件名通过 query 传递 */
export function uploadReleaseFile(file: File) {
  const url = `/site/release/upload?filename=${encodeURIComponent(file.name)}`
  return request.post(url, file, {
    headers: { 'Content-Type': 'application/octet-stream' },
    timeout: 0,
  }) as unknown as Promise<ReleaseConfig>
}

export function getSiteStats(days = 30) {
  return get<{ list: SiteStatsDaily[]; totals: { pv: number; uv: number; downloads: number; buyClicks: number } }>(
    '/site/stats',
    { days },
  )
}
