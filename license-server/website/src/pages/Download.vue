<template>
  <div class="download">
    <section class="page-hero">
      <div class="container">
        <h1 class="page-title">下载中心</h1>
        <p class="page-sub">获取 {{ siteName }} 最新版本安装包。若主下载地址较慢，可尝试下方的备用地址。</p>
        <div v-if="release" class="version-line">
          <span class="version-badge">v{{ release.latestVersion }}</span>
          <span v-if="release.releaseDate" class="version-meta">更新于 {{ formatDate(release.releaseDate) }}</span>
        </div>
      </div>
    </section>

    <section class="section">
      <div class="container download-wrap">
        <div v-if="loading" class="state-tip">正在获取版本信息…</div>

        <div v-else-if="!release" class="state-tip">
          暂无可下载的版本信息，请稍后再试或
          <RouterLink class="inline-link" to="/contact">联系我们</RouterLink>。
        </div>

        <template v-else>
          <div class="main-card">
            <div class="main-info">
              <h2 class="main-title">{{ siteName }} v{{ release.latestVersion }}</h2>
              <ul class="main-meta">
                <li v-if="release.releaseDate">
                  <span>更新日期</span>{{ formatDate(release.releaseDate) }}
                </li>
                <li v-if="release.downloadSize">
                  <span>文件大小</span>{{ formatSize(release.downloadSize) }}
                </li>
                <li v-if="release.downloadSha256">
                  <span>SHA-256</span><code class="sha">{{ release.downloadSha256 }}</code>
                </li>
              </ul>
              <div class="main-actions">
                <a
                  v-if="release.downloadUrl"
                  class="btn btn-primary btn-lg"
                  :href="release.downloadUrl"
                  target="_blank"
                  rel="noopener"
                  @click="onDownload"
                >
                  立即下载
                </a>
                <span v-else class="text-3">主下载地址暂未配置</span>
              </div>
            </div>

            <div class="main-visual">
              <div class="os-badge">
                <div class="os-icon">🖥️</div>
                <div class="os-name">Windows</div>
                <div class="os-desc">支持主流 Windows 系统</div>
              </div>
            </div>
          </div>

          <div v-if="release.mirrors.length" class="mirrors">
            <h2 class="block-title">其他下载地址</h2>
            <p class="block-sub">包含网盘、镜像等备用渠道，可按需选择。</p>
            <div class="grid grid-3">
              <div v-for="m in release.mirrors" :key="m.id" class="card mirror-card">
                <a
                  class="mirror-main"
                  :href="m.url"
                  target="_blank"
                  rel="noopener"
                  @click="onDownload"
                >
                  <div class="mirror-icon">{{ typeIcon(m.type) }}</div>
                  <div class="mirror-body">
                    <div class="mirror-name">{{ m.name || typeLabel(m.type) }}</div>
                    <div class="mirror-type">{{ typeLabel(m.type) }}</div>
                  </div>
                </a>
                <div v-if="m.password || m.extractCode" class="mirror-cred">
                  <div v-if="m.password" class="cred-row">
                    <span class="cred-label">密码</span>
                    <span class="cred-value">{{ m.password }}</span>
                    <button
                      type="button"
                      class="cred-copy"
                      @click="copyCred(`${m.id}-pwd`, m.password)"
                    >{{ copiedKey === `${m.id}-pwd` ? '已复制' : '复制' }}</button>
                  </div>
                  <div v-if="m.extractCode" class="cred-row">
                    <span class="cred-label">提取码</span>
                    <span class="cred-value">{{ m.extractCode }}</span>
                    <button
                      type="button"
                      class="cred-copy"
                      @click="copyCred(`${m.id}-code`, m.extractCode)"
                    >{{ copiedKey === `${m.id}-code` ? '已复制' : '复制' }}</button>
                  </div>
                </div>
              </div>
            </div>
          </div>

          <div v-if="release.changelog.length" class="changelog">
            <h2 class="block-title">更新日志</h2>
            <ul class="changelog-list">
              <li v-for="(item, index) in release.changelog" :key="index">{{ item }}</li>
            </ul>
          </div>

          <div class="download-tips">
            <h2 class="block-title">安装说明</h2>
            <ol class="tips-list">
              <li>下载完成后运行安装程序，按引导完成安装。</li>
              <li>首次启动可能需要创建运行环境，请耐心等待。</li>
              <li>若下载失败，请尝试其他下载地址或更换浏览器。</li>
              <li>安装或使用中遇到问题，可查看<RouterLink class="inline-link" to="/docs">使用手册</RouterLink>或<RouterLink class="inline-link" to="/contact">联系我们</RouterLink>。</li>
            </ol>
          </div>
        </template>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { getLatestRelease, track, type LatestRelease } from '@/api'
import { useSite } from '@/composables/useSite'
import { usePageHead } from '@/composables/usePageHead'

const { siteName } = useSite()
usePageHead('下载中心', '下载 eBox 最新版本安装包，提供直链、镜像与网盘等多种下载地址。')

const release = ref<LatestRelease | null>(null)
const loading = ref(true)

function typeLabel(type: string) {
  switch (type) {
    case 'netdisk':
      return '网盘'
    case 'github':
      return 'GitHub'
    case 'mirror':
      return '镜像'
    default:
      return '其他'
  }
}

function typeIcon(type: string) {
  switch (type) {
    case 'netdisk':
      return '☁️'
    case 'github':
      return '🐙'
    case 'mirror':
      return '🚀'
    default:
      return '🔗'
  }
}

function formatDate(value: string) {
  if (!value) return ''
  return value.slice(0, 10)
}

function formatSize(bytes: number) {
  if (!bytes || bytes <= 0) return ''
  const units = ['B', 'KB', 'MB', 'GB']
  let size = bytes
  let i = 0
  while (size >= 1024 && i < units.length - 1) {
    size /= 1024
    i += 1
  }
  return `${size.toFixed(i === 0 ? 0 : 1)} ${units[i]}`
}

function onDownload() {
  track('download')
}

const copiedKey = ref('')
let copyTimer: ReturnType<typeof setTimeout> | null = null

async function copyCred(key: string, value: string | null) {
  if (!value) return
  try {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(value)
    } else {
      const ta = document.createElement('textarea')
      ta.value = value
      ta.style.position = 'fixed'
      ta.style.opacity = '0'
      document.body.appendChild(ta)
      ta.select()
      document.execCommand('copy')
      document.body.removeChild(ta)
    }
    copiedKey.value = key
    if (copyTimer) clearTimeout(copyTimer)
    copyTimer = setTimeout(() => {
      copiedKey.value = ''
    }, 1500)
  } catch {
    // 复制失败时静默处理
  }
}

onMounted(async () => {
  try {
    const data = await getLatestRelease()
    release.value = data && data.latestVersion ? data : null
  } catch {
    release.value = null
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

.version-line {
  margin-top: 18px;
  display: flex;
  gap: 12px;
  align-items: center;
  justify-content: center;
}

.version-badge {
  padding: 5px 14px;
  border-radius: 999px;
  background: var(--primary-light);
  color: var(--primary-color);
  font-weight: 600;
  font-size: 14px;
}

.version-meta {
  color: var(--text-3);
  font-size: 14px;
}

.download-wrap {
  max-width: 940px;
}

.main-card {
  display: grid;
  grid-template-columns: 1.6fr 1fr;
  gap: 28px;
  align-items: center;
  background: #fff;
  border: 1px solid var(--border);
  border-radius: 16px;
  box-shadow: var(--shadow-md);
  padding: 34px;
}

.main-title {
  font-size: 24px;
}

.main-meta {
  margin-top: 18px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.main-meta li {
  font-size: 14px;
  color: var(--text-1);
  display: flex;
  gap: 12px;
  align-items: baseline;
  flex-wrap: wrap;
}

.main-meta li span {
  color: var(--text-3);
  min-width: 72px;
}

.sha {
  font-family: 'JetBrains Mono', Consolas, monospace;
  font-size: 12px;
  color: var(--text-2);
  word-break: break-all;
}

.main-actions {
  margin-top: 26px;
}

.main-visual {
  display: flex;
  justify-content: center;
}

.os-badge {
  text-align: center;
  padding: 26px 30px;
  border-radius: var(--radius);
  background: var(--bg-soft);
  width: 100%;
}

.os-icon {
  font-size: 40px;
}

.os-name {
  margin-top: 10px;
  font-size: 18px;
  font-weight: 700;
}

.os-desc {
  margin-top: 6px;
  font-size: 13px;
  color: var(--text-3);
}

.mirrors,
.changelog,
.download-tips {
  margin-top: 48px;
}

.block-title {
  font-size: 22px;
}

.block-sub {
  margin-top: 8px;
  color: var(--text-3);
  font-size: 14px;
}

.mirror-card {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 20px;
}

.mirror-main {
  display: flex;
  align-items: center;
  gap: 14px;
}

.mirror-icon {
  font-size: 26px;
}

.mirror-name {
  font-weight: 600;
  font-size: 16px;
}

.mirror-type {
  margin-top: 4px;
  font-size: 13px;
  color: var(--text-3);
}

.mirror-cred {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding-top: 12px;
  border-top: 1px dashed var(--border);
}

.cred-row {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
}

.cred-label {
  color: var(--text-3);
}

.cred-value {
  font-family: 'JetBrains Mono', Consolas, monospace;
  font-weight: 600;
  color: var(--text-1);
  word-break: break-all;
}

.cred-copy {
  margin-left: auto;
  padding: 2px 10px;
  border: 1px solid var(--border);
  border-radius: 999px;
  background: #fff;
  color: var(--primary-color);
  font-size: 12px;
  cursor: pointer;
  transition: all 0.15s ease;
}

.cred-copy:hover {
  border-color: var(--primary-color);
  background: var(--primary-light);
}

.changelog-list,
.tips-list {
  margin-top: 18px;
  padding-left: 22px;
  list-style: disc;
  color: var(--text-2);
  font-size: 15px;
}

.tips-list {
  list-style: decimal;
}

.changelog-list li,
.tips-list li {
  margin-bottom: 10px;
}

.inline-link {
  color: var(--primary-color);
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
  .main-card {
    grid-template-columns: 1fr;
    padding: 24px;
  }
}
</style>
