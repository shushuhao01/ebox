<template>
  <div class="maintenance">
    <div class="container maintenance-inner">
      <div class="maintenance-icon">🛠️</div>
      <h1 class="maintenance-title">{{ siteName }} 正在维护中</h1>
      <p class="maintenance-desc">
        网站正在进行升级维护，暂时无法访问。给您带来的不便敬请谅解，请稍后再试。
      </p>
      <div class="maintenance-actions">
        <button type="button" class="btn btn-primary" @click="reload">刷新页面</button>
        <RouterLink class="btn btn-ghost" to="/contact">联系我们</RouterLink>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { useSite } from '@/composables/useSite'
import { usePageHead } from '@/composables/usePageHead'

const { siteName } = useSite()
usePageHead('维护中')

function reload() {
  if (typeof window !== 'undefined') window.location.reload()
}
</script>

<style scoped>
.maintenance {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 56vh;
}

.maintenance-inner {
  text-align: center;
  max-width: 560px;
  padding: 60px 20px;
}

.maintenance-icon {
  font-size: 56px;
}

.maintenance-title {
  margin-top: 20px;
  font-size: 30px;
}

.maintenance-desc {
  margin-top: 16px;
  color: var(--text-2);
  font-size: 16px;
}

.maintenance-actions {
  margin-top: 30px;
  display: flex;
  gap: 14px;
  justify-content: center;
  flex-wrap: wrap;
}

@media (max-width: 768px) {
  .maintenance-title {
    font-size: 24px;
  }
}
</style>
