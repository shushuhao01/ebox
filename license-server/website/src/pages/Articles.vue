<template>
  <div class="articles">
    <section class="page-hero">
      <div class="container">
        <h1 class="page-title">文章中心</h1>
        <p class="page-sub">教程、科普与产品更新，帮助你更好地使用 {{ siteName }}。</p>
      </div>
    </section>

    <section class="section">
      <div class="container">
        <div class="filter-bar">
          <button
            v-for="item in categories"
            :key="item.value"
            type="button"
            class="filter-btn"
            :class="{ active: activeCategory === item.value }"
            @click="switchCategory(item.value)"
          >
            {{ item.label }}
          </button>
        </div>

        <div v-if="loading" class="state-tip">正在加载文章…</div>
        <div v-else-if="!list.length" class="state-tip">暂无文章内容。</div>

        <template v-else>
          <div class="article-list">
            <RouterLink v-for="a in list" :key="a.id" class="article-item" :to="`/articles/${a.slug}`">
              <div v-if="a.cover" class="article-cover">
                <img :src="a.cover" :alt="a.title" />
              </div>
              <div class="article-body">
                <div class="article-meta">
                  <span class="tag">{{ categoryLabel(a.category) }}</span>
                  <span v-if="a.pinned === 1" class="pin-tag">置顶</span>
                  <span class="article-date">{{ formatDate(a.publishAt || a.createdAt) }}</span>
                </div>
                <h3 class="article-title">{{ a.title }}</h3>
                <p v-if="a.summary" class="article-summary">{{ a.summary }}</p>
                <span class="article-views">{{ a.views }} 次阅读</span>
              </div>
            </RouterLink>
          </div>

          <div v-if="total > size" class="pagination">
            <button type="button" class="page-btn" :disabled="page <= 1" @click="goPage(page - 1)">
              上一页
            </button>
            <span class="page-info">{{ page }} / {{ totalPages }}</span>
            <button type="button" class="page-btn" :disabled="page >= totalPages" @click="goPage(page + 1)">
              下一页
            </button>
          </div>
        </template>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { getArticles, type ArticleBrief } from '@/api'
import { useSite } from '@/composables/useSite'
import { usePageHead } from '@/composables/usePageHead'

const { siteName } = useSite()
usePageHead('文章中心', '阅读 eBox 的使用教程、行业科普与产品更新。')

const categories = [
  { label: '全部', value: '' },
  { label: '使用教程', value: 'tutorial' },
  { label: '科普', value: 'science' },
  { label: '更新', value: 'update' },
  { label: '公告', value: 'notice' },
]

const categoryMap: Record<string, string> = {
  tutorial: '使用教程',
  science: '科普',
  update: '更新',
  notice: '公告',
}

const list = ref<ArticleBrief[]>([])
const total = ref(0)
const page = ref(1)
const size = ref(10)
const loading = ref(true)
const activeCategory = ref('')

const totalPages = computed(() => Math.max(1, Math.ceil(total.value / size.value)))

function categoryLabel(value: string) {
  return categoryMap[value] || value || '文章'
}

function formatDate(value: string | null) {
  if (!value) return ''
  return value.slice(0, 10)
}

async function load() {
  loading.value = true
  try {
    const data = await getArticles({
      category: activeCategory.value || undefined,
      page: page.value,
      size: size.value,
    })
    list.value = data.list || []
    total.value = data.total || 0
  } catch {
    list.value = []
    total.value = 0
  } finally {
    loading.value = false
  }
}

function switchCategory(value: string) {
  if (activeCategory.value === value) return
  activeCategory.value = value
  page.value = 1
  void load()
}

function goPage(target: number) {
  if (target < 1 || target > totalPages.value) return
  page.value = target
  void load()
  if (typeof window !== 'undefined') window.scrollTo({ top: 0, behavior: 'smooth' })
}

onMounted(() => {
  void load()
})
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

.filter-bar {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  justify-content: center;
  margin-bottom: 40px;
}

.filter-btn {
  padding: 8px 18px;
  border-radius: 999px;
  border: 1px solid var(--border);
  background: #fff;
  color: var(--text-2);
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s ease;
}

.filter-btn:hover {
  border-color: var(--primary-color);
  color: var(--primary-color);
}

.filter-btn.active {
  background: var(--primary-color);
  border-color: var(--primary-color);
  color: #fff;
}

.article-list {
  display: flex;
  flex-direction: column;
  gap: 18px;
}

.article-item {
  display: flex;
  gap: 20px;
  background: #fff;
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 20px;
  box-shadow: var(--shadow-sm);
  transition: transform 0.2s ease, box-shadow 0.2s ease;
}

.article-item:hover {
  transform: translateY(-3px);
  box-shadow: var(--shadow-md);
}

.article-cover {
  width: 180px;
  flex: none;
  border-radius: 10px;
  overflow: hidden;
}

.article-cover img {
  width: 100%;
  height: 120px;
  object-fit: cover;
}

.article-body {
  flex: 1;
  min-width: 0;
}

.article-meta {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.pin-tag {
  font-size: 12px;
  color: #d97706;
  background: #fef3c7;
  padding: 3px 10px;
  border-radius: 999px;
}

.article-date {
  color: var(--text-3);
  font-size: 13px;
}

.article-title {
  margin-top: 12px;
  font-size: 19px;
}

.article-summary {
  margin-top: 8px;
  color: var(--text-2);
  font-size: 14px;
}

.article-views {
  display: inline-block;
  margin-top: 12px;
  color: var(--text-3);
  font-size: 13px;
}

.pagination {
  margin-top: 40px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 18px;
}

.page-btn {
  padding: 9px 20px;
  border-radius: 8px;
  border: 1px solid var(--border);
  background: #fff;
  color: var(--text-1);
  font-size: 14px;
  cursor: pointer;
  transition: all 0.2s ease;
}

.page-btn:hover:not(:disabled) {
  border-color: var(--primary-color);
  color: var(--primary-color);
}

.page-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.page-info {
  color: var(--text-2);
  font-size: 14px;
}

.state-tip {
  text-align: center;
  color: var(--text-3);
  padding: 60px 0;
}

@media (max-width: 640px) {
  .page-title {
    font-size: 30px;
  }
  .article-item {
    flex-direction: column;
  }
  .article-cover {
    width: 100%;
  }
}
</style>
