<template>
  <div class="cases">
    <section class="page-hero">
      <div class="container">
        <h1 class="page-title">使用案例</h1>
        <p class="page-sub">不同行业的用户如何在同一台电脑上稳定地多开应用。</p>
      </div>
    </section>

    <section class="section">
      <div class="container">
        <div v-if="industries.length > 1" class="filter-bar">
          <button
            v-for="item in industries"
            :key="item"
            type="button"
            class="filter-btn"
            :class="{ active: activeIndustry === item }"
            @click="activeIndustry = item"
          >
            {{ item }}
          </button>
        </div>

        <div v-if="loading" class="state-tip">正在加载案例…</div>
        <div v-else-if="!filtered.length" class="state-tip">暂无案例内容，敬请期待。</div>

        <div v-else class="grid grid-3">
          <RouterLink v-for="c in filtered" :key="c.id" class="card case-card" :to="`/cases/${c.id}`">
            <span class="tag">{{ c.industry }}</span>
            <h3 class="case-title">{{ c.title }}</h3>
            <p class="case-summary">{{ c.summary }}</p>
            <span class="case-more">查看详情 →</span>
          </RouterLink>
        </div>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { getCaseList, type SiteCase } from '@/api'
import { usePageHead } from '@/composables/usePageHead'

usePageHead('使用案例', '查看 eBox 在不同行业中的实际使用案例。')

const ALL = '全部'
const cases = ref<SiteCase[]>([])
const loading = ref(true)
const activeIndustry = ref(ALL)

const industries = computed(() => {
  const set = new Set<string>()
  cases.value.forEach((c) => {
    if (c.industry) set.add(c.industry)
  })
  return [ALL, ...Array.from(set)]
})

const filtered = computed(() => {
  if (activeIndustry.value === ALL) return cases.value
  return cases.value.filter((c) => c.industry === activeIndustry.value)
})

onMounted(async () => {
  try {
    cases.value = await getCaseList()
  } catch {
    cases.value = []
  } finally {
    loading.value = false
  }
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

.case-card {
  display: block;
  padding: 24px 22px;
}

.case-title {
  margin-top: 14px;
  font-size: 18px;
}

.case-summary {
  margin-top: 10px;
  color: var(--text-2);
  font-size: 14px;
}

.case-more {
  display: inline-block;
  margin-top: 16px;
  color: var(--primary-color);
  font-size: 14px;
  font-weight: 600;
}

.state-tip {
  text-align: center;
  color: var(--text-3);
  padding: 60px 0;
}

@media (max-width: 768px) {
  .page-title {
    font-size: 30px;
  }
}
</style>
