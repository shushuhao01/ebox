<template>
  <footer class="site-footer">
    <div class="container footer-main">
      <div class="footer-brand">
        <div class="brand">
          <img :src="logo" :alt="siteName" class="brand-logo" />
          <span class="brand-name">{{ siteName }}</span>
        </div>
        <p class="footer-desc">{{ settings.site_description || '一台电脑，多开任意应用。' }}</p>
      </div>

      <div class="footer-links">
        <div class="footer-col">
          <h4>产品</h4>
          <RouterLink to="/features">功能介绍</RouterLink>
          <RouterLink to="/download">下载中心</RouterLink>
          <RouterLink to="/cases">使用案例</RouterLink>
        </div>
        <div class="footer-col">
          <h4>支持</h4>
          <RouterLink to="/docs">文档中心</RouterLink>
          <RouterLink to="/articles">文章中心</RouterLink>
          <RouterLink to="/contact">联系我们</RouterLink>
        </div>
        <div class="footer-col">
          <h4>协议</h4>
          <RouterLink to="/agreement">用户协议</RouterLink>
          <RouterLink to="/privacy">隐私政策</RouterLink>
          <template v-for="item in footerNav" :key="item.id">
            <a v-if="isExternal(item.url)" :href="item.url" target="_blank" rel="noopener">{{ item.label }}</a>
            <RouterLink v-else :to="item.url">{{ item.label }}</RouterLink>
          </template>
        </div>
      </div>
    </div>

    <div class="footer-bottom">
      <div class="container footer-bottom-inner">
        <span>{{ copyright }}</span>
        <span class="footer-meta">
          <a v-if="settings.icp" href="https://beian.miit.gov.cn/" target="_blank" rel="noopener">{{ settings.icp }}</a>
          <span v-if="settings.police_icp">{{ settings.police_icp }}</span>
        </span>
      </div>
    </div>
  </footer>
</template>

<script setup lang="ts">
import { useSite } from '@/composables/useSite'

const { siteName, logo, settings, copyright, footerNav } = useSite()

function isExternal(url: string) {
  return /^https?:\/\//i.test(url)
}
</script>

<style scoped>
.site-footer {
  background: #0f1729;
  color: #c2cad8;
  margin-top: 60px;
}

.footer-main {
  display: flex;
  flex-wrap: wrap;
  gap: 40px;
  padding-top: 52px;
  padding-bottom: 40px;
}

.footer-brand {
  flex: 1;
  min-width: 240px;
}

.brand {
  display: flex;
  align-items: center;
  gap: 10px;
  color: #fff;
  font-weight: 700;
  font-size: 19px;
}

.brand-logo {
  width: 32px;
  height: 32px;
  border-radius: 8px;
}

.footer-desc {
  margin-top: 14px;
  max-width: 320px;
  font-size: 14px;
  color: #8a94a6;
}

.footer-links {
  display: flex;
  gap: 64px;
  flex-wrap: wrap;
}

.footer-col {
  display: flex;
  flex-direction: column;
  gap: 10px;
  min-width: 96px;
}

.footer-col h4 {
  color: #fff;
  font-size: 15px;
  margin-bottom: 4px;
}

.footer-col a {
  font-size: 14px;
  color: #8a94a6;
  transition: color 0.2s ease;
}

.footer-col a:hover {
  color: #fff;
}

.footer-bottom {
  border-top: 1px solid rgba(255, 255, 255, 0.08);
}

.footer-bottom-inner {
  display: flex;
  flex-wrap: wrap;
  gap: 8px 20px;
  align-items: center;
  justify-content: space-between;
  padding-top: 18px;
  padding-bottom: 18px;
  font-size: 13px;
  color: #8a94a6;
}

.footer-meta {
  display: flex;
  gap: 18px;
}

.footer-meta a:hover {
  color: #fff;
}

@media (max-width: 768px) {
  .footer-links {
    gap: 32px;
  }
}
</style>
