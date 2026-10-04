<template>
  <div class="home">
    <!-- 首屏 -->
    <section class="hero">
      <div class="container hero-inner">
        <div class="hero-copy fade-up">
          <span class="hero-badge">环境隔离 · 多开无冲突</span>
          <h1 class="hero-title">{{ heroTitle }}</h1>
          <p class="hero-sub">{{ heroSubtitle }}</p>
          <div class="hero-actions">
            <RouterLink class="btn btn-primary btn-lg" to="/download">立即下载</RouterLink>
            <RouterLink class="btn btn-ghost btn-lg" to="/features">查看功能</RouterLink>
          </div>
          <p v-if="release" class="hero-version">
            最新版本 v{{ release.latestVersion }}
            <span v-if="release.releaseDate">· {{ formatDate(release.releaseDate) }}</span>
          </p>
        </div>

        <div class="hero-visual fade-up">
          <div class="hero-card">
            <div class="hero-card-bar">
              <span></span><span></span><span></span>
            </div>
            <div class="hero-envs">
              <div v-for="i in 4" :key="i" class="hero-env">
                <div class="hero-env-dot"></div>
                <div class="hero-env-lines">
                  <span></span><span></span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </section>

    <!-- 信任条 -->
    <section class="trust">
      <div class="container trust-inner">
        <div v-for="item in stats" :key="item.label" class="trust-item">
          <div class="trust-value">{{ item.value }}</div>
          <div class="trust-label">{{ item.label }}</div>
        </div>
      </div>
    </section>

    <!-- 核心卖点 -->
    <section class="section">
      <div class="container">
        <div class="section-head">
          <h2 class="section-title">为什么选择 {{ siteName }}</h2>
          <p class="section-sub">不是虚拟机，也不是简单复制，而是真正独立运行的应用环境。</p>
        </div>
        <div class="grid grid-4">
          <div v-for="f in features" :key="f.title" class="card feature-card">
            <div class="feature-icon">{{ f.icon }}</div>
            <h3 class="feature-title">{{ f.title }}</h3>
            <p class="feature-desc">{{ f.desc }}</p>
          </div>
        </div>
      </div>
    </section>

    <!-- 对比表 -->
    <section class="section section-soft">
      <div class="container">
        <div class="section-head">
          <h2 class="section-title">多开方案对比</h2>
          <p class="section-sub">不同方案的稳定性与使用成本差异明显，选择适合自己的方式最重要。</p>
        </div>
        <div class="compare-wrap">
          <table class="compare">
            <thead>
              <tr>
                <th>对比项</th>
                <th>强开多开</th>
                <th>虚拟机多开</th>
                <th class="highlight">{{ siteName }}</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="row in compareRows" :key="row.name">
                <td class="compare-name">{{ row.name }}</td>
                <td>{{ row.force }}</td>
                <td>{{ row.vm }}</td>
                <td class="highlight">{{ row.ebox }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>
    </section>

    <!-- 使用案例 -->
    <section v-if="cases.length" class="section">
      <div class="container">
        <div class="section-head">
          <h2 class="section-title">使用案例</h2>
          <p class="section-sub">看看不同行业的用户是如何使用 {{ siteName }} 的。</p>
        </div>
        <div class="grid grid-3">
          <RouterLink v-for="c in cases" :key="c.id" class="card case-card" :to="`/cases/${c.id}`">
            <span class="tag">{{ c.industry }}</span>
            <h3 class="case-title">{{ c.title }}</h3>
            <p class="case-summary">{{ c.summary }}</p>
          </RouterLink>
        </div>
        <div class="center more-wrap">
          <RouterLink class="btn btn-ghost" to="/cases">查看更多案例</RouterLink>
        </div>
      </div>
    </section>

    <!-- CTA -->
    <section class="section">
      <div class="container">
        <div class="cta">
          <div class="cta-col">
            <h2 class="cta-title">免费下载体验</h2>
            <p class="cta-desc">支持 Windows 系统，安装即用。</p>
            <RouterLink class="btn btn-primary btn-lg" to="/download">前往下载</RouterLink>
          </div>
          <div class="cta-divider"></div>
          <div class="cta-col">
            <h2 class="cta-title">解锁全部功能</h2>
            <p class="cta-desc">购买后即可使用完整功能与持续更新。</p>
            <a
              v-if="purchaseUrl"
              class="btn btn-ghost btn-lg"
              :href="purchaseUrl"
              target="_blank"
              rel="noopener"
              @click="track('buy')"
            >
              立即购买
            </a>
            <RouterLink v-else class="btn btn-ghost btn-lg" to="/contact">联系我们</RouterLink>
          </div>
        </div>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { getCaseList, getLatestRelease, track, type LatestRelease, type SiteCase } from '@/api'
import { useSite } from '@/composables/useSite'
import { usePageHead } from '@/composables/usePageHead'

const { siteName, settings, purchaseUrl } = useSite()
usePageHead('', settings.value.site_description)

const release = ref<LatestRelease | null>(null)
const cases = ref<SiteCase[]>([])

const heroTitle = computed(() => settings.value.home_hero_title || '一台电脑，多开任意应用')
const heroSubtitle = computed(
  () => settings.value.home_hero_subtitle || '环境独立隔离 · 免扫码自动登录 · 体积小不卡顿',
)

const features = [
  { icon: '🔒', title: '环境独立隔离', desc: '每个多开环境相互独立，配置、数据互不干扰。' },
  { icon: '⚡', title: '免扫码自动登录', desc: '一次配置，重复使用，省去反复扫码的麻烦。' },
  { icon: '🪶', title: '体积小不卡顿', desc: '无需虚拟机，占用资源低，多个环境同时运行也流畅。' },
  { icon: '🧩', title: '可视化管理', desc: '环境卡片化管理，启动、停止、查看状态一目了然。' },
]

const compareRows = [
  { name: '环境隔离', force: '无', vm: '完整隔离', ebox: '独立隔离' },
  { name: '资源占用', force: '低', vm: '高', ebox: '低' },
  { name: '多开数量', force: '不稳定', vm: '受限于硬件', ebox: '按需多开' },
  { name: '使用门槛', force: '操作复杂', vm: '配置繁琐', ebox: '安装即用' },
  { name: '数据管理', force: '易冲突', vm: '割裂', ebox: '集中管理' },
]

const defaultStats = [
  { label: '支持系统', value: 'Windows' },
  { label: '运行方式', value: '环境隔离' },
  { label: '安装方式', value: '下载即用' },
  { label: '更新方式', value: '在线更新' },
]

const stats = computed(() => {
  const raw = settings.value.home_stats
  if (raw) {
    try {
      const arr = JSON.parse(raw)
      if (Array.isArray(arr) && arr.length) {
        return arr
          .filter((it) => it && it.label !== undefined)
          .map((it) => ({ label: String(it.label ?? ''), value: String(it.value ?? '') }))
      }
    } catch {
      // 忽略非法 JSON，使用默认值
    }
  }
  return defaultStats
})

function formatDate(value: string) {
  if (!value) return ''
  return value.slice(0, 10)
}

onMounted(async () => {
  try {
    release.value = await getLatestRelease()
  } catch {
    // 版本接口不可用时隐藏版本信息
  }
  try {
    const list = await getCaseList()
    cases.value = list.slice(0, 3)
  } catch {
    // 忽略
  }
})
</script>

<style scoped>
.hero {
  background: linear-gradient(160deg, #eef4ff 0%, #ffffff 60%);
  padding: 76px 0 64px;
}

.hero-inner {
  display: grid;
  grid-template-columns: 1.1fr 0.9fr;
  gap: 48px;
  align-items: center;
}

.hero-badge {
  display: inline-block;
  padding: 6px 14px;
  border-radius: 999px;
  background: var(--primary-light);
  color: var(--primary-color);
  font-size: 13px;
  font-weight: 600;
}

.hero-title {
  margin-top: 18px;
  font-size: 46px;
  letter-spacing: -1px;
}

.hero-sub {
  margin-top: 18px;
  font-size: 18px;
  color: var(--text-2);
}

.hero-actions {
  margin-top: 30px;
  display: flex;
  gap: 14px;
  flex-wrap: wrap;
}

.hero-version {
  margin-top: 18px;
  color: var(--text-3);
  font-size: 14px;
}

.hero-card {
  background: #fff;
  border-radius: 16px;
  box-shadow: var(--shadow-md);
  border: 1px solid var(--border);
  overflow: hidden;
}

.hero-card-bar {
  display: flex;
  gap: 6px;
  padding: 12px 14px;
  border-bottom: 1px solid var(--border);
}

.hero-card-bar span {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #dbe3ef;
}

.hero-envs {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.hero-env {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px;
  border-radius: 10px;
  background: var(--bg-soft);
}

.hero-env-dot {
  width: 12px;
  height: 12px;
  border-radius: 50%;
  background: var(--primary-color);
  flex: none;
}

.hero-env-lines {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.hero-env-lines span {
  height: 8px;
  border-radius: 4px;
  background: #dbe4f5;
}

.hero-env-lines span:last-child {
  width: 60%;
}

.trust {
  border-top: 1px solid var(--border);
  border-bottom: 1px solid var(--border);
  background: #fbfcfe;
}

.trust-inner {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 20px;
  padding-top: 30px;
  padding-bottom: 30px;
}

.trust-item {
  text-align: center;
}

.trust-value {
  font-size: 22px;
  font-weight: 700;
  color: var(--primary-color);
}

.trust-label {
  margin-top: 4px;
  font-size: 13px;
  color: var(--text-3);
}

.feature-card {
  padding: 26px 22px;
}

.feature-icon {
  font-size: 30px;
}

.feature-title {
  margin-top: 14px;
  font-size: 18px;
}

.feature-desc {
  margin-top: 8px;
  color: var(--text-2);
  font-size: 14px;
}

.compare-wrap {
  overflow-x: auto;
}

.compare {
  width: 100%;
  min-width: 640px;
  border-collapse: collapse;
  background: #fff;
  border-radius: var(--radius);
  overflow: hidden;
  box-shadow: var(--shadow-sm);
}

.compare th,
.compare td {
  padding: 16px 18px;
  text-align: center;
  border-bottom: 1px solid var(--border);
  font-size: 15px;
}

.compare th {
  background: #f7f9fd;
  font-weight: 600;
}

.compare-name {
  text-align: left;
  color: var(--text-2);
}

.compare .highlight {
  color: var(--primary-color);
  font-weight: 600;
  background: var(--primary-light);
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

.more-wrap {
  margin-top: 34px;
}

.cta {
  display: flex;
  align-items: center;
  gap: 40px;
  background: linear-gradient(120deg, #eef4ff, #f7faff);
  border: 1px solid var(--border);
  border-radius: 18px;
  padding: 44px;
}

.cta-col {
  flex: 1;
}

.cta-title {
  font-size: 24px;
}

.cta-desc {
  margin: 10px 0 20px;
  color: var(--text-2);
}

.cta-divider {
  width: 1px;
  align-self: stretch;
  background: var(--border);
}

@media (max-width: 900px) {
  .hero-inner {
    grid-template-columns: 1fr;
  }
  .hero-title {
    font-size: 34px;
  }
  .trust-inner {
    grid-template-columns: repeat(2, 1fr);
  }
  .cta {
    flex-direction: column;
    align-items: stretch;
    padding: 30px;
  }
  .cta-divider {
    width: auto;
    height: 1px;
  }
}
</style>
