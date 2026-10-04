import { computed } from 'vue'
import { useHead } from '@unhead/vue'
import { useSite } from './useSite'

/** keywords 默认值（SSG 构建时后端不可用，settings 为空，需内置兜底） */
const DEFAULT_KEYWORDS = 'eBox,2box,多开工具,电脑多开,应用多开,多开器,企业微信多开,微信多开,万能多开,虚拟机多开,环境隔离'

/** 统一设置页面 title、description 与 keywords（自动拼接站点名） */
export function usePageHead(title?: string, description?: string) {
  const { siteName, settings } = useSite()

  useHead({
    title: computed(() => (title ? `${title} - ${siteName.value}` : siteName.value)),
    meta: [
      {
        name: 'description',
        content: computed(() => description || ''),
      },
      {
        name: 'keywords',
        content: computed(() => settings.value.site_keywords || DEFAULT_KEYWORDS),
      },
    ],
  })
}
