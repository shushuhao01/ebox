import { computed, reactive } from 'vue'
import { getNavList, getSiteSettings, type SiteNav, type SiteSettings } from '@/api'

interface SiteState {
  settings: SiteSettings
  nav: SiteNav[]
  loaded: boolean
}

const state = reactive<SiteState>({
  settings: {},
  nav: [],
  loaded: false,
})

let loading: Promise<void> | null = null

/** 加载站点公共数据（设置 + 导航），全局仅加载一次 */
export function loadSiteData(force = false): Promise<void> {
  if (state.loaded && !force) return Promise.resolve()
  if (loading) return loading
  loading = (async () => {
    try {
      const [settings, nav] = await Promise.all([getSiteSettings(), getNavList()])
      state.settings = settings || {}
      state.nav = (nav || []).filter((n) => n.visible !== 0)
      state.loaded = true
      applyTheme()
    } catch {
      // 接口不可用时保持默认文案，不阻断页面渲染
    } finally {
      loading = null
    }
  })()
  return loading
}

/** 将后台配置的主色写入 CSS 变量并同步标题 */
function applyTheme() {
  if (typeof document === 'undefined') return
  const color = state.settings.primary_color
  if (color) document.documentElement.style.setProperty('--primary-color', color)
}

export function useSite() {
  const settings = computed(() => state.settings)
  const nav = computed(() => state.nav)

  const siteName = computed(() => state.settings.site_name || 'eBox')
  const logo = computed(() => state.settings.site_logo || '/logo.png')
  const purchaseUrl = computed(() => state.settings.purchase_url || '')
  const docUrl = computed(() => state.settings.doc_url || '')
  const docTitle = computed(() => state.settings.doc_title || '使用手册')
  const githubUrl = computed(() => state.settings.github_url || '')
  const copyright = computed(() => state.settings.copyright || '')
  const announcement = computed(() => ({
    enabled: state.settings.announcement_enabled === '1',
    text: state.settings.announcement_text || '',
    url: state.settings.announcement_url || '',
  }))

  const topNav = computed(() => state.nav.filter((n) => n.position === 'top'))
  const footerNav = computed(() => state.nav.filter((n) => n.position === 'footer'))

  return {
    settings,
    nav,
    siteName,
    logo,
    purchaseUrl,
    docUrl,
    docTitle,
    githubUrl,
    copyright,
    announcement,
    topNav,
    footerNav,
  }
}
