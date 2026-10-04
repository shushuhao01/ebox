<template>
  <div class="not-found">
    <div class="container not-found-inner">
      <div class="code">404</div>
      <h1 class="title">页面不存在</h1>
      <p class="desc">您访问的页面可能已被删除或移动，请检查地址是否正确。</p>
      <div class="actions">
        <RouterLink class="btn btn-primary" to="/">返回首页</RouterLink>
        <RouterLink class="btn btn-ghost" to="/download">前往下载</RouterLink>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { usePageHead } from '@/composables/usePageHead'

usePageHead('页面不存在')
</script>

<style scoped>
.not-found {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 56vh;
}

.not-found-inner {
  text-align: center;
  max-width: 520px;
  padding: 60px 20px;
}

.code {
  font-size: 84px;
  font-weight: 800;
  color: var(--primary-color);
  letter-spacing: 4px;
  line-height: 1;
}

.title {
  margin-top: 18px;
  font-size: 28px;
}

.desc {
  margin-top: 14px;
  color: var(--text-2);
  font-size: 16px;
}

.actions {
  margin-top: 30px;
  display: flex;
  gap: 14px;
  justify-content: center;
  flex-wrap: wrap;
}

@media (max-width: 768px) {
  .code {
    font-size: 60px;
  }
  .title {
    font-size: 22px;
  }
}
</style>
