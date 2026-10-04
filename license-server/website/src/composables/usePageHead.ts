import { computed } from 'vue'
import { useHead } from '@unhead/vue'
import { useSite } from './useSite'

/** 统一设置页面 title 与 description（自动拼接站点名） */
export function usePageHead(title?: string, description?: string) {
  const { siteName } = useSite()

  useHead({
    title: computed(() => (title ? `${title} - ${siteName.value}` : siteName.value)),
    meta: [
      {
        name: 'description',
        content: computed(() => description || ''),
      },
    ],
  })
}
