<template>
  <div class="article-detail">
    <section class="section">
      <div class="container detail-wrap">
        <RouterLink class="back-link" to="/articles">← 返回文章中心</RouterLink>

        <div v-if="loading" class="state-tip">正在加载…</div>
        <div v-else-if="!article" class="state-tip">文章不存在或已下线。</div>

        <template v-else>
          <div class="article-meta">
            <span class="tag">{{ categoryLabel(article.category) }}</span>
            <span class="meta-date">{{ formatDate(article.publishAt || article.createdAt) }}</span>
            <span class="meta-views">{{ article.views }} 次阅读</span>
          </div>
          <h1 class="article-title">{{ article.title }}</h1>
          <p v-if="article.summary" class="article-summary">{{ article.summary }}</p>
          <img v-if="article.cover" :src="article.cover" :alt="article.title" class="article-cover" />

          <article v-if="article.content" class="rich-content" v-html="article.content"></article>

          <div v-if="related.length" class="related">
            <h2 class="related-title">相关文章</h2>
            <div class="grid grid-3">
              <RouterLink v-for="r in related" :key="r.id" class="card related-card" :to="`/articles/${r.slug}`">
                <img v-if="r.cover" :src="r.cover" :alt="r.title" class="related-cover" />
                <h3 class="related-name">{{ r.title }}</h3>
              </RouterLink>
            </div>
          </div>
        </template>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onServerPrefetch, ref } from 'vue'
import { useRoute } from 'vue-router'
import { useHead } from '@unhead/vue'
import { getArticle, type RelatedArticle, type SiteArticle } from '@/api'
import { useSite } from '@/composables/useSite'
import { canonicalUrl } from '@/composables/usePageHead'

const route = useRoute()
const { siteName } = useSite()

const article = ref<SiteArticle | null>(null)
const related = ref<RelatedArticle[]>([])
const loading = ref(true)

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

useHead({
  title: computed(() => {
    const name = article.value?.seoTitle || article.value?.title
    return name ? `${name} - ${siteName.value}` : `文章中心 - ${siteName.value}`
  }),
  meta: [
    {
      name: 'description',
      content: computed(() => article.value?.seoDesc || article.value?.summary || ''),
    },
  ],
  link: computed(() => [{ rel: 'canonical', href: canonicalUrl(route.path) }]),
})

async function load() {
  try {
    const data = await getArticle(String(route.params.slug))
    article.value = data.article
    related.value = data.related || []
  } catch {
    article.value = null
    related.value = []
  } finally {
    loading.value = false
  }
}

// SSG 构建期在服务端预取，使文章正文进入构建产物 HTML（利于 SEO 抓取）
onServerPrefetch(async () => {
  await load()
  // 通过 vite-ssg 的 initialState 通道把数据带入客户端水合，避免重复请求与内容闪烁。
  // 注意：context.initialState 与此处 meta.state 为同一对象引用，必须原地修改而非整体替换。
  const state = route.meta.state as Record<string, unknown> | undefined
  if (state && typeof state === 'object') {
    state.article = article.value
    state.related = related.value
    state.loading = false
  }
})

onMounted(() => {
  // 首次加载（SSG 预渲染页）优先复用水合状态，其余情况走客户端请求
  const state = route.meta.state as
    | { article?: SiteArticle | null; related?: RelatedArticle[] }
    | undefined
  if (state && typeof state === 'object' && 'article' in state) {
    article.value = state.article ?? null
    related.value = state.related ?? []
    loading.value = false
    return
  }
  void load()
})
</script>

<style scoped>
.detail-wrap {
  max-width: 800px;
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

.article-meta {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.meta-date,
.meta-views {
  color: var(--text-3);
  font-size: 13px;
}

.article-title {
  margin-top: 16px;
  font-size: 34px;
}

.article-summary {
  margin-top: 14px;
  color: var(--text-2);
  font-size: 16px;
}

.article-cover {
  margin-top: 26px;
  width: 100%;
  border-radius: var(--radius);
  border: 1px solid var(--border);
}

.rich-content {
  margin-top: 30px;
}

.related {
  margin-top: 56px;
  padding-top: 36px;
  border-top: 1px solid var(--border);
}

.related-title {
  font-size: 20px;
  margin-bottom: 22px;
}

.related-card {
  display: block;
  padding: 14px;
}

.related-cover {
  width: 100%;
  height: 130px;
  object-fit: cover;
  border-radius: 8px;
}

.related-name {
  margin-top: 12px;
  font-size: 15px;
}

.state-tip {
  text-align: center;
  color: var(--text-3);
  padding: 60px 0;
}

@media (max-width: 768px) {
  .article-title {
    font-size: 26px;
  }
}
</style>
