<template>
  <div class="docs">
    <section class="page-hero">
      <div class="container">
        <h1 class="page-title">文档中心</h1>
        <p class="page-sub">查看 {{ siteName }} 的使用手册与操作指引。</p>
      </div>
    </section>

    <section class="section">
      <div class="container docs-wrap">
        <div class="manual-card">
          <div class="manual-icon">📘</div>
          <div class="manual-body">
            <h2 class="manual-title">{{ docTitle }}</h2>
            <p class="manual-desc">
              涵盖安装、环境创建、登录配置与常见问题排查，帮助你快速上手 {{ siteName }}。
            </p>
            <div class="manual-actions">
              <a
                v-if="docUrl"
                class="btn btn-primary btn-lg"
                :href="docUrl"
                :target="docTarget"
                :rel="docTarget === '_blank' ? 'noopener' : undefined"
              >
                打开{{ docTitle }}
              </a>
              <span v-else class="manual-empty">{{ docTitle }}地址尚未配置，请稍后再试。</span>
            </div>
          </div>
        </div>

        <div class="doc-tips">
          <h2 class="block-title">常用入口</h2>
          <div class="grid grid-3">
            <RouterLink class="card tip-card" to="/download">
              <div class="tip-icon">⬇️</div>
              <div class="tip-name">下载安装包</div>
              <div class="tip-desc">获取最新版本并完成安装</div>
            </RouterLink>
            <RouterLink class="card tip-card" to="/articles">
              <div class="tip-icon">📝</div>
              <div class="tip-name">使用教程</div>
              <div class="tip-desc">阅读分步操作教程</div>
            </RouterLink>
            <RouterLink class="card tip-card" to="/contact">
              <div class="tip-icon">💬</div>
              <div class="tip-name">联系客服</div>
              <div class="tip-desc">遇到问题可随时咨询</div>
            </RouterLink>
          </div>
        </div>
      </div>
    </section>

    <!-- 使用教程 -->
    <ArticlePinned title="使用教程" subtitle="阅读分步操作教程与常见问题排查。" />
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useSite } from '@/composables/useSite'
import { usePageHead } from '@/composables/usePageHead'
import ArticlePinned from '@/components/ArticlePinned.vue'

const { siteName, docUrl, docTitle, settings } = useSite()
usePageHead('文档中心', 'eBox 使用手册与操作指引。')

const docTarget = computed(() => settings.value.doc_target || '_blank')
</script>

<style scoped>
.page-hero {
  padding: 64px 0 20px;
  background: linear-gradient(160deg, #eef4ff 0%, #ffffff 70%);
  text-align: center;
}

.page-title {
  font-size: 40px;
  letter-spacing: -1px;
}

.page-sub {
  margin: 16px auto 0;
  max-width: 640px;
  color: var(--text-2);
  font-size: 17px;
}

.docs-wrap {
  max-width: 940px;
}

.manual-card {
  display: flex;
  gap: 26px;
  align-items: flex-start;
  background: #fff;
  border: 1px solid var(--border);
  border-radius: 16px;
  box-shadow: var(--shadow-md);
  padding: 34px;
}

.manual-icon {
  font-size: 46px;
  line-height: 1;
}

.manual-body {
  flex: 1;
  min-width: 0;
}

.manual-title {
  font-size: 24px;
}

.manual-desc {
  margin-top: 12px;
  color: var(--text-2);
  font-size: 15px;
}

.manual-actions {
  margin-top: 24px;
}

.manual-empty {
  color: var(--text-3);
  font-size: 14px;
}

.doc-tips {
  margin-top: 48px;
}

.block-title {
  font-size: 22px;
  margin-bottom: 20px;
}

.tip-card {
  padding: 24px 22px;
}

.tip-icon {
  font-size: 26px;
}

.tip-name {
  margin-top: 12px;
  font-size: 17px;
  font-weight: 600;
}

.tip-desc {
  margin-top: 6px;
  color: var(--text-3);
  font-size: 13px;
}

@media (max-width: 768px) {
  .page-title {
    font-size: 30px;
  }
  .manual-card {
    flex-direction: column;
    padding: 24px;
  }
}
</style>
