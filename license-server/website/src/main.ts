import { ViteSSG } from 'vite-ssg'
import App from './App.vue'
import { routes } from './router'
import { loadSiteData } from './composables/useSite'
import { track } from './api'
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
