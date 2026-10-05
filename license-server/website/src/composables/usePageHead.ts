import { useRoute } from 'vue-router'
import { useHead } from '@unhead/vue'
import { useSite } from './useSite'

/** keywords 默认值（SSG 构建时后端不可用，settings 为空，需内置兜底） */
const DEFAULT_KEYWORDS = 'eBox,2box,多开工具,电脑多开,应用多开,多开器,企业微信多开,微信多开,万能多开,虚拟机多开,环境隔离'

/** 官网正式主域（带 www）：非主域会 301 跳转到此，全站 canonical 也统一指向它 */
const SITE_ORIGIN = 'https://www.abc222.cn'

/** 生成指向主域的 canonical 绝对地址 */
export function canonicalUrl(path: string): string {
  return `${SITE_ORIGIN}${path || '/'}`
}

interface PageHeadOptions {
  /** 是否输出 canonical，默认 true；404、维护页等无收录价值页面应显式设为 false */
  canonical?: boolean
}

/** 统一设置页面 title、description、keywords 与 canonical（自动拼接站点名） */
export function usePageHead(title?: string, description?: string, options: PageHeadOptions = {}) {
  const { siteName, settings } = useSite()
  const route = useRoute()

  useHead(() => ({
    title: title ? `${title} - ${siteName.value}` : siteName.value,
    meta: [
      { name: 'description', content: description || '' },
      { name: 'keywords', content: settings.value.site_keywords || DEFAULT_KEYWORDS },
    ],
    link: options.canonical === false ? [] : [{ rel: 'canonical', href: canonicalUrl(route.path) }],
  }))
}
