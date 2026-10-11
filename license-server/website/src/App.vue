<template>
  <div class="site-root">
    <SiteHeader />
    <AnnouncementBar />
    <main class="site-main">
      <RouterView />
    </main>
    <SiteFooter />
    <ServiceWidget />
  </div>
</template>

<script setup lang="ts">
import { onMounted, watch } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useHead } from '@unhead/vue'
import type { Script } from '@unhead/vue'
import SiteHeader from '@/components/SiteHeader.vue'
import SiteFooter from '@/components/SiteFooter.vue'
import AnnouncementBar from '@/components/AnnouncementBar.vue'
import ServiceWidget from '@/components/ServiceWidget.vue'
import { loadSiteData, useSite } from '@/composables/useSite'

useHead({ htmlAttrs: { lang: 'zh-CN' } })

const route = useRoute()
const router = useRouter()
const { settings } = useSite()

/** 解析后台「统计脚本」：支持完整 <script> 片段或纯 JS 代码 */
function parseStatisticsScripts(code: string): Script[] {
  const raw = (code || '').trim()
  if (!raw) return []
  const out: Script[] = []
  const re = /<script\b([^>]*)>([\s\S]*?)<\/script>/gi
  let m: RegExpExecArray | null
  while ((m = re.exec(raw)) !== null) {
    const attrs = m[1] || ''
    const body = (m[2] || '').trim()
    const src = /\bsrc\s*=\s*["']([^"']+)["']/i.exec(attrs)?.[1]
    const script: Script = { type: 'text/javascript', key: `statistics-${out.length}` }
    if (src) script.src = src
    if (/\basync\b/i.test(attrs)) script.async = true
    if (/\bdefer\b/i.test(attrs)) script.defer = true
    if (body) script.innerHTML = body
    out.push(script)
  }
  // 未包含 <script> 标签时，按纯 JS 代码处理
  if (!out.length) out.push({ type: 'text/javascript', key: 'statistics-0', innerHTML: raw })
  return out
}

// 将后台配置的第三方统计脚本（百度统计 / 51LA / GA 等）注入官网 head，请仅使用可信来源
useHead(() => {
  const scripts = parseStatisticsScripts(settings.value.statistics_code || '')
  return scripts.length ? { script: scripts } : {}
})

onMounted(() => {
  void loadSiteData()
})

// 维护模式：官网前台统一跳转到 /maintenance（不影响后台与客户端接口）
watch(
  () => settings.value.maintenance_mode,
  (mode) => {
    if (mode === '1' && route.path !== '/maintenance') {
      void router.replace('/maintenance')
    }
  },
)
</script>

<style scoped>
.site-root {
  display: flex;
  flex-direction: column;
  min-height: 100vh;
}

.site-main {
  flex: 1;
}
</style>
