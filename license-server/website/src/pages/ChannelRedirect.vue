<template>
  <div class="channel-redirect">
    <div class="container channel-redirect-inner">
      <div class="channel-redirect-spinner" />
      <p class="channel-redirect-text">{{ tip }}</p>
      <RouterLink v-if="failed" class="btn btn-primary channel-redirect-home" to="/">返回首页</RouterLink>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { resolveChannel, setChannelCookie, normalizeChannel } from '@/api'
import { usePageHead } from '@/composables/usePageHead'

const route = useRoute()
const router = useRouter()
const tip = ref('正在跳转…')
const failed = ref(false)

usePageHead('跳转中', undefined, { canonical: false })

onMounted(async () => {
  const code = normalizeChannel(route.params.code)
  if (!code) {
    failed.value = true
    tip.value = '链接无效或已停用'
    return
  }
  // 先落地渠道来源，确保跳转后的页面埋点能带上渠道码
  setChannelCookie(code)
  try {
    const data = await resolveChannel(code)
    const target = data?.targetPath || '/'
    const sep = target.includes('?') ? '&' : '?'
    router.replace(`${target}${sep}ch=${encodeURIComponent(code)}`)
  } catch {
    failed.value = true
    tip.value = '链接无效或已停用'
  }
})
</script>

<style scoped>
.channel-redirect {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 56vh;
}

.channel-redirect-inner {
  text-align: center;
  max-width: 480px;
  padding: 60px 20px;
}

.channel-redirect-spinner {
  width: 42px;
  height: 42px;
  margin: 0 auto;
  border: 3px solid var(--border, #e5e7eb);
  border-top-color: var(--primary, #3b82f6);
  border-radius: 50%;
  animation: channel-spin 0.8s linear infinite;
}

.channel-redirect-text {
  margin-top: 20px;
  color: var(--text-2);
  font-size: 16px;
}

.channel-redirect-home {
  margin-top: 24px;
}

@keyframes channel-spin {
  to {
    transform: rotate(360deg);
  }
}
</style>
