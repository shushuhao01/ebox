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
          <div v-if="heroImages.length" class="hero-gallery">
            <div class="hero-carousel">
              <img
                v-for="(url, idx) in heroImages"
                :key="url"
                :src="url"
                class="hero-slide"
                :class="{ active: idx === activeIndex }"
                alt=""
                @click="openLightbox(idx)"
              />
            </div>
            <div v-if="heroImages.length > 1" class="hero-dots">
              <button
                v-for="(url, idx) in heroImages"
                :key="idx"
                type="button"
                class="hero-dot"
                :class="{ active: idx === activeIndex }"
                :aria-label="`查看第 ${idx + 1} 张配图`"
                @click="goToSlide(idx)"
              ></button>
            </div>
          </div>

          <div v-else class="hero-card">
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
          <p class="section-sub">强开多开与虚拟机多开属于其他常见方式，下表仅对比它们与 {{ siteName }} 的差异，帮助你更清楚地了解 {{ siteName }} 的优势。</p>
        </div>
        <div class="compare-wrap">
          <table class="compare">
            <thead>
              <tr>
                <th rowspan="2" class="compare-item-col">对比项</th>
                <th colspan="2" class="others-col">其他方案</th>
                <th rowspan="2" class="highlight">
                  <span class="ebox-head">
                    <img class="ebox-logo" :src="logo" alt="" />
                    {{ siteName }}
                  </span>
                </th>
              </tr>
              <tr>
                <th>强开多开</th>
                <th>虚拟机多开</th>
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
            <h3 class="case-title">{{ c.title }}</h3>
            <div class="case-main">
              <img v-if="c.avatar" class="case-thumb" :src="c.avatar" :alt="c.title" loading="lazy" />
              <p class="case-summary">{{ c.summary }}</p>
            </div>
            <span class="tag case-tag">{{ c.industry }}</span>
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

    <!-- 配图放大查看 -->
    <div v-if="lightboxIndex >= 0" class="lightbox" @click="closeLightbox">
      <button class="lightbox-close" aria-label="关闭" @click.stop="closeLightbox">×</button>
      <button
        v-if="heroImages.length > 1"
        class="lightbox-nav prev"
        aria-label="上一张"
        @click.stop="prevImage"
      >
        ‹
      </button>
      <img :src="heroImages[lightboxIndex]" class="lightbox-img" alt="" @click.stop />
      <button
        v-if="heroImages.length > 1"
        class="lightbox-nav next"
        aria-label="下一张"
        @click.stop="nextImage"
      >
        ›
      </button>
      <div v-if="heroImages.length > 1" class="lightbox-count">
        {{ lightboxIndex + 1 }} / {{ heroImages.length }}
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import { getCaseList, getLatestRelease, track, type LatestRelease, type SiteCase } from '@/api'
import { useSite } from '@/composables/useSite'
import { usePageHead } from '@/composables/usePageHead'

const { siteName, logo, settings, purchaseUrl } = useSite()
usePageHead('', settings.value.site_description)

const release = ref<LatestRelease | null>(null)
const cases = ref<SiteCase[]>([])

const heroTitle = computed(() => settings.value.home_hero_title || '一台电脑，多开任意应用')
const heroSubtitle = computed(
  () => settings.value.home_hero_subtitle || '环境独立隔离 · 免扫码自动登录 · 体积小不卡顿',
)

/** 首页右侧轮播配图（JSON 数组字符串，空则回退默认展示卡） */
const heroImages = computed(() => {
  const raw = settings.value.home_hero_images
  if (!raw) return []
  try {
    const arr = JSON.parse(raw)
    if (Array.isArray(arr)) return arr.map((u) => String(u)).filter(Boolean)
  } catch {
    // 忽略非法 JSON
  }
  return []
})

const activeIndex = ref(0)
const lightboxIndex = ref(-1)
let slideTimer: number | undefined

function goToSlide(idx: number) {
  activeIndex.value = idx
}

function openLightbox(idx: number) {
  lightboxIndex.value = idx
}

function closeLightbox() {
  lightboxIndex.value = -1
}

function prevImage() {
  const len = heroImages.value.length
  if (len < 2) return
  lightboxIndex.value = (lightboxIndex.value - 1 + len) % len
}

function nextImage() {
  const len = heroImages.value.length
  if (len < 2) return
  lightboxIndex.value = (lightboxIndex.value + 1) % len
}

function onKeydown(e: KeyboardEvent) {
  if (lightboxIndex.value < 0) return
  if (e.key === 'Escape') closeLightbox()
  else if (e.key === 'ArrowLeft') prevImage()
  else if (e.key === 'ArrowRight') nextImage()
}

watch(
  () => heroImages.value.length,
  (len) => {
    if (activeIndex.value >= len) activeIndex.value = 0
    if (lightboxIndex.value >= len) lightboxIndex.value = -1
  },
)

onMounted(() => {
  slideTimer = window.setInterval(() => {
    if (heroImages.value.length > 1) {
      activeIndex.value = (activeIndex.value + 1) % heroImages.value.length
    }
  }, 4000)
  window.addEventListener('keydown', onKeydown)
})

onUnmounted(() => {
  if (slideTimer) window.clearInterval(slideTimer)
  window.removeEventListener('keydown', onKeydown)
})

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

.hero-gallery {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.hero-carousel {
  position: relative;
  border-radius: 16px;
  overflow: hidden;
  box-shadow: var(--shadow-md);
  border: 1px solid var(--border);
  background: #fff;
  aspect-ratio: 4 / 3;
}

.hero-slide {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
  opacity: 0;
  pointer-events: none;
  transition: opacity 0.6s ease;
  cursor: zoom-in;
}

.hero-slide.active {
  opacity: 1;
  pointer-events: auto;
}

.hero-dots {
  display: flex;
  justify-content: center;
  gap: 10px;
}

.hero-dot {
  width: 10px;
  height: 10px;
  padding: 0;
  border: 1px solid rgba(26, 34, 51, 0.25);
  border-radius: 999px;
  background: #cbd5e1;
  cursor: pointer;
  transition: all 0.25s ease;
}

.hero-dot:hover {
  background: #94a3b8;
}

.hero-dot.active {
  width: 26px;
  border-color: var(--primary-color);
  background: var(--primary-color);
}

.lightbox {
  position: fixed;
  inset: 0;
  z-index: 9999;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 56px;
  background: rgba(0, 0, 0, 0.88);
  cursor: zoom-out;
}

.lightbox-img {
  max-width: 100%;
  max-height: 100%;
  border-radius: 8px;
  box-shadow: 0 24px 64px rgba(0, 0, 0, 0.5);
  cursor: default;
}

.lightbox-close,
.lightbox-nav {
  position: absolute;
  z-index: 2;
  display: flex;
  align-items: center;
  justify-content: center;
  border: none;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.16);
  color: #fff;
  cursor: pointer;
  transition: background 0.2s ease;
}

.lightbox-close:hover,
.lightbox-nav:hover {
  background: rgba(255, 255, 255, 0.32);
}

.lightbox-close {
  top: 20px;
  right: 24px;
  width: 42px;
  height: 42px;
  font-size: 26px;
  line-height: 1;
}

.lightbox-nav {
  top: 50%;
  transform: translateY(-50%);
  width: 52px;
  height: 52px;
  padding-bottom: 4px;
  font-size: 34px;
  line-height: 1;
}

.lightbox-nav.prev {
  left: 24px;
}

.lightbox-nav.next {
  right: 24px;
}

.lightbox-count {
  position: absolute;
  left: 50%;
  bottom: 22px;
  transform: translateX(-50%);
  color: rgba(255, 255, 255, 0.85);
  font-size: 14px;
  letter-spacing: 1px;
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

.compare .compare-item-col {
  text-align: left;
  color: var(--text-2);
}

.compare .others-col {
  color: var(--text-2);
  font-weight: 600;
}

.compare .highlight {
  color: var(--primary-color);
  font-weight: 600;
  background: var(--primary-light);
}

.ebox-head {
  display: inline-flex;
  align-items: center;
  gap: 8px;
}

.ebox-logo {
  width: 22px;
  height: 22px;
  border-radius: 6px;
  object-fit: contain;
}

.case-card {
  display: flex;
  flex-direction: column;
  padding: 20px;
}

.case-title {
  font-size: 17px;
  line-height: 1.45;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
  min-height: 2.9em;
}

.case-main {
  display: flex;
  gap: 14px;
  margin-top: 14px;
  margin-bottom: 14px;
}

.case-summary {
  flex: 1;
  min-width: 0;
  color: var(--text-2);
  font-size: 14px;
  line-height: 1.65;
  display: -webkit-box;
  -webkit-line-clamp: 3;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

.case-thumb {
  flex: none;
  width: 72px;
  height: 72px;
  border-radius: 10px;
  object-fit: cover;
  border: 1px solid var(--border);
}

.case-tag {
  align-self: flex-start;
  margin-top: auto;
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
