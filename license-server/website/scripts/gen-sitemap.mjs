// 构建期生成 sitemap.xml（固定页面 + 全部已发布文章）
// 在 `vite-ssg build` 之后运行，写入 dist/sitemap.xml（dist 不入库，避免污染工作区）。
// 依赖后端 /api/site/articles 接口；拉取失败则保留构建产物里 public/sitemap.xml 的静态兜底，
// 仅打印警告、退出码 0，不阻断部署。
import { writeFile, mkdir } from 'node:fs/promises'
import { fileURLToPath } from 'node:url'
import path from 'node:path'

// 站点主域（与前台 canonical / 后端 site_origin 默认值一致）
const ORIGIN = (process.env.SITE_ORIGIN || process.env.VITE_SITE_ORIGIN || 'https://www.abc222.cn').replace(/\/+$/, '')
// 构建期后端地址（与 website/src/api/index.ts 的 ssgApiBase 保持一致）
const API_BASE = (process.env.VITE_SSG_API_BASE || 'http://127.0.0.1:3008').replace(/\/+$/, '')
const TIMEOUT_MS = 8000

// 固定页面（与 backend/src/services/SeoService.ts 的 STATIC_ROUTES 保持一致）
const STATIC_ROUTES = [
  { path: '/', changefreq: 'weekly', priority: '1.0' },
  { path: '/features', changefreq: 'monthly', priority: '0.9' },
  { path: '/download', changefreq: 'weekly', priority: '0.9' },
  { path: '/cases', changefreq: 'weekly', priority: '0.8' },
  { path: '/articles', changefreq: 'weekly', priority: '0.8' },
  { path: '/docs', changefreq: 'monthly', priority: '0.7' },
  { path: '/contact', changefreq: 'monthly', priority: '0.6' },
  { path: '/agreement', changefreq: 'yearly', priority: '0.3' },
  { path: '/privacy', changefreq: 'yearly', priority: '0.3' },
]

function escapeXml(v) {
  return String(v)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&apos;')
}

function toDateStr(v) {
  if (!v) return ''
  const d = new Date(v)
  return Number.isNaN(d.getTime()) ? '' : d.toISOString().slice(0, 10)
}

async function fetchJson(url) {
  const res = await fetch(url, { signal: AbortSignal.timeout(TIMEOUT_MS) })
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  return res.json()
}

/** 分页拉取全部已发布文章（接口 size 上限 50） */
async function fetchArticles() {
  const size = 50
  const items = []
  for (let page = 1; page <= 200; page += 1) {
    const body = await fetchJson(`${API_BASE}/api/site/articles?page=${page}&size=${size}`)
    if (!body || body.code !== 0) throw new Error((body && body.msg) || '接口返回异常')
    const data = body.data || {}
    const list = data.list || []
    for (const it of list) if (it && it.slug) items.push(it)
    const total = data.total || 0
    if (page * size >= total || list.length === 0) break
  }
  return items
}

function buildXml(articles) {
  const out = []
  out.push('<?xml version="1.0" encoding="UTF-8"?>')
  out.push('<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">')
  for (const r of STATIC_ROUTES) {
    out.push('  <url>')
    out.push(`    <loc>${ORIGIN}${r.path}</loc>`)
    out.push(`    <changefreq>${r.changefreq}</changefreq>`)
    out.push(`    <priority>${r.priority}</priority>`)
    out.push('  </url>')
  }
  for (const a of articles) {
    out.push('  <url>')
    out.push(`    <loc>${ORIGIN}/articles/${escapeXml(a.slug)}</loc>`)
    const lastmod = toDateStr(a.publishAt || a.createdAt)
    if (lastmod) out.push(`    <lastmod>${lastmod}</lastmod>`)
    out.push('    <changefreq>monthly</changefreq>')
    out.push('    <priority>0.7</priority>')
    out.push('  </url>')
  }
  out.push('</urlset>')
  return `${out.join('\n')}\n`
}

async function main() {
  const outFile = path.resolve(fileURLToPath(new URL('../dist/sitemap.xml', import.meta.url)))
  let articles
  try {
    articles = await fetchArticles()
  } catch (e) {
    console.warn(`[sitemap] 拉取文章列表失败，保留 public/sitemap.xml 静态兜底：${(e && e.message) || e}`)
    return
  }
  await mkdir(path.dirname(outFile), { recursive: true })
  await writeFile(outFile, buildXml(articles), 'utf8')
  console.log(`[sitemap] 已生成 dist/sitemap.xml：固定页面 ${STATIC_ROUTES.length} 条 + 文章 ${articles.length} 条`)
}

main().catch((e) => {
  console.warn('[sitemap] 生成异常，保留静态兜底 sitemap.xml：', e)
})
