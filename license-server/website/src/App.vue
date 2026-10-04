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
import SiteHeader from '@/components/SiteHeader.vue'
import SiteFooter from '@/components/SiteFooter.vue'
import AnnouncementBar from '@/components/AnnouncementBar.vue'
import ServiceWidget from '@/components/ServiceWidget.vue'
import { loadSiteData, useSite } from '@/composables/useSite'

useHead({ htmlAttrs: { lang: 'zh-CN' } })

const route = useRoute()
const router = useRouter()
const { settings } = useSite()

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
