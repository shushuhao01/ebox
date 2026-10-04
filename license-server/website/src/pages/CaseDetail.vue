<template>
  <div class="case-detail">
    <section class="section">
      <div class="container detail-wrap">
        <RouterLink class="back-link" to="/cases">← 返回案例列表</RouterLink>

        <div v-if="loading" class="state-tip">正在加载…</div>
        <div v-else-if="!detail" class="state-tip">案例不存在或已下线。</div>

        <template v-else>
          <span class="tag">{{ detail.industry }}</span>
          <h1 class="detail-title">{{ detail.title }}</h1>
          <p v-if="detail.summary" class="detail-summary">{{ detail.summary }}</p>

          <img v-if="detail.avatar" :src="detail.avatar" :alt="detail.title" class="detail-cover" />

          <div v-if="metricsList.length" class="metrics">
            <div v-for="m in metricsList" :key="m.label" class="metric">
              <div class="metric-value">{{ m.value }}</div>
              <div class="metric-label">{{ m.label }}</div>
            </div>
          </div>

          <article v-if="detail.content" class="rich-content" v-html="detail.content"></article>

          <div class="detail-cta">
            <RouterLink class="btn btn-primary" to="/download">免费下载体验</RouterLink>
            <RouterLink class="btn btn-ghost" to="/contact">咨询合作</RouterLink>
          </div>
        </template>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useRoute } from 'vue-router'
import { getCaseDetail, type SiteCase } from '@/api'
import { usePageHead } from '@/composables/usePageHead'

const route = useRoute()
const detail = ref<SiteCase | null>(null)
const loading = ref(true)

usePageHead(detail.value?.title || '案例详情', detail.value?.summary || '')

const metricsList = computed<{ label: string; value: string }[]>(() => {
  const raw = detail.value?.metrics
  if (!raw) return []
  try {
    const parsed = JSON.parse(raw)
    if (Array.isArray(parsed)) {
      return parsed
        .filter((it) => it && it.label !== undefined)
        .map((it) => ({ label: String(it.label ?? ''), value: String(it.value ?? '') }))
    }
    if (parsed && typeof parsed === 'object') {
      return Object.entries(parsed).map(([label, value]) => ({ label, value: String(value ?? '') }))
    }
  } catch {
    // 非 JSON，按纯文本处理
    return [{ label: '数据', value: raw }]
  }
  return []
})

onMounted(async () => {
  try {
    detail.value = await getCaseDetail(String(route.params.id))
  } catch {
    detail.value = null
  } finally {
    loading.value = false
  }
})
</script>

<style scoped>
.detail-wrap {
  max-width: 860px;
}

.back-link {
  display: inline-block;
  margin-bottom: 24px;
  color: var(--text-3);
  font-size: 14px;
}

.back-link:hover {
  color: var(--primary-color);
}

.detail-title {
  margin-top: 14px;
  font-size: 34px;
}

.detail-summary {
  margin-top: 14px;
  color: var(--text-2);
  font-size: 17px;
}

.detail-cover {
  margin-top: 28px;
  width: 100%;
  border-radius: var(--radius);
  border: 1px solid var(--border);
}

.metrics {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
  gap: 18px;
  margin: 32px 0;
  padding: 24px;
  background: var(--bg-soft);
  border-radius: var(--radius);
}

.metric-value {
  font-size: 24px;
  font-weight: 700;
  color: var(--primary-color);
}

.metric-label {
  margin-top: 4px;
  font-size: 13px;
  color: var(--text-3);
}

.detail-cta {
  margin-top: 44px;
  display: flex;
  gap: 14px;
  flex-wrap: wrap;
}

.state-tip {
  text-align: center;
  color: var(--text-3);
  padding: 60px 0;
}

@media (max-width: 768px) {
  .detail-title {
    font-size: 26px;
  }
}
</style>
