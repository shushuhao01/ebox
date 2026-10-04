<template>
  <section v-if="articles.length" class="section">
    <div class="container">
      <div class="section-head">
        <h2 class="section-title">{{ title }}</h2>
        <p class="section-sub">{{ subtitle }}</p>
      </div>
      <div class="grid grid-3">
        <RouterLink
          v-for="a in articles"
          :key="a.id"
          class="card article-card"
          :to="`/articles/${a.slug}`"
        >
          <div class="article-meta">
            <span class="tag">{{ categoryLabel(a.category) }}</span>
            <span v-if="a.pinned === 1" class="pin-tag">置顶</span>
          </div>
          <h3 class="article-title">{{ a.title }}</h3>
          <p v-if="a.summary" class="article-summary">{{ a.summary }}</p>
          <span class="article-date">{{ formatDate(a.publishAt || a.createdAt) }}</span>
        </RouterLink>
      </div>
      <div class="center more-wrap">
        <RouterLink class="btn btn-ghost" to="/articles">查看更多文章</RouterLink>
      </div>
    </div>
  </section>
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { getArticles, type ArticleBrief } from '@/api'

// 展示置顶优先的文章（后端按 pinned DESC 排序，取前 N 条即置顶在前）
const props = withDefaults(defineProps<{ title?: string; subtitle?: string; size?: number }>(), {
  title: '文章推荐',
  subtitle: '阅读使用教程、行业科普与产品更新。',
  size: 3,
})

const articles = ref<ArticleBrief[]>([])

const categoryMap: Record<string, string> = {
  tutorial: '使用教程',
  science: '科普',
  update: '更新',
  notice: '公告',
}

function categoryLabel(value: string) {
  return categoryMap[value] || value || '文章'
}

function formatDate(value: string | null) {
  if (!value) return ''
  return value.slice(0, 10)
}

onMounted(async () => {
  try {
    const data = await getArticles({ page: 1, size: props.size })
    articles.value = data.list || []
  } catch {
    articles.value = []
  }
})
</script>

<style scoped>
.article-card {
  display: flex;
  flex-direction: column;
  padding: 20px;
}

.article-meta {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.pin-tag {
  font-size: 12px;
  color: #d97706;
  background: #fef3c7;
  padding: 3px 10px;
  border-radius: 999px;
}

.article-title {
  margin-top: 12px;
  font-size: 17px;
  line-height: 1.45;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
  min-height: 2.9em;
}

.article-summary {
  margin-top: 8px;
  color: var(--text-2);
  font-size: 14px;
  line-height: 1.65;
  display: -webkit-box;
  -webkit-line-clamp: 3;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

.article-date {
  margin-top: auto;
  padding-top: 12px;
  color: var(--text-3);
  font-size: 13px;
}

.more-wrap {
  margin-top: 34px;
}
</style>
