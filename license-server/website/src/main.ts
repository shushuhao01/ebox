import { ViteSSG } from 'vite-ssg'
import type { RouteRecordRaw } from 'vue-router'
import App from './App.vue'
import { routes } from './router'
import { loadSiteData } from './composables/useSite'
import { getAllArticleSlugs, track } from './api'
import './styles/main.scss'

export const createApp = ViteSSG(
  App,
  { routes, base: import.meta.env.BASE_URL },
  ({ router, isClient }) => {
    if (!isClient) return

    // 每次导航触发一次站点公共数据加载（内部已做单次缓存）
    router.beforeEach(() => {
      void loadSiteData()
      return true
    })

    // 页面访问埋点
    router.afterEach(() => {
      track('pv')
    })
  },
)

/**
 * SSG 构建期注入需要预渲染的路由。
 * 默认实现会过滤动态路由，这里在静态路由基础上补充每篇已发布文章的详情页，
 * 使文章正文进入构建产物 HTML（利于 SEO 抓取）。构建期后端不可用时降级为静态路由。
 */
export async function includedRoutes(paths: string[], _routes: readonly RouteRecordRaw[]): Promise<string[]> {
  const result = paths.filter((p) => !p.includes(':') && !p.includes('*'))
  try {
    const slugs = await getAllArticleSlugs()
    for (const slug of slugs) {
      if (!slug) continue
      const path = `/articles/${encodeURIComponent(slug)}`
      if (!result.includes(path)) result.push(path)
    }
  } catch (e) {
    console.warn('[vite-ssg] 拉取文章列表失败，跳过文章详情预渲染：', e)
  }
  return result
}
