<template>
  <div class="contact">
    <section class="page-hero">
      <div class="container">
        <h1 class="page-title">联系我们</h1>
        <p class="page-sub">使用过程中遇到任何问题，欢迎通过以下方式与我们取得联系。</p>
      </div>
    </section>

    <section class="section">
      <div class="container contact-wrap">
        <div v-if="loading" class="state-tip">正在加载联系方式…</div>
        <div v-else-if="!contacts.length" class="state-tip">暂未配置联系方式，请稍后再试。</div>

        <div v-else class="grid grid-3">
          <div v-for="c in contacts" :key="c.id" class="card contact-card">
            <div class="contact-icon">{{ typeIcon(c.type) }}</div>
            <div class="contact-name">{{ c.name }}</div>
            <div v-if="c.value" class="contact-value">
              <a v-if="linkOf(c)" :href="linkOf(c)" target="_blank" rel="noopener">{{ c.value }}</a>
              <span v-else>{{ c.value }}</span>
            </div>
            <img v-if="c.qrcode" :src="c.qrcode" :alt="c.name" class="contact-qrcode" />
          </div>
        </div>

        <div class="contact-note">
          <h2 class="block-title">温馨提示</h2>
          <ul class="note-list">
            <li>咨询时请简要描述问题现象，便于我们更快定位与处理。</li>
            <li>涉及授权与订单问题，请提供相关订单信息以便核对。</li>
            <li>请通过本站公布的正规渠道联系我们，谨防上当受骗。</li>
          </ul>
        </div>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { getContacts, type SiteContact } from '@/api'
import { usePageHead } from '@/composables/usePageHead'

usePageHead('联系我们', '通过客服、邮箱或社群与 eBox 取得联系。')

const contacts = ref<SiteContact[]>([])
const loading = ref(true)

function typeIcon(type: string) {
  const t = (type || '').toLowerCase()
  if (t.includes('qq')) return '🐧'
  if (t.includes('wechat') || t.includes('wx')) return '💬'
  if (t.includes('mail') || t.includes('email')) return '✉️'
  if (t.includes('phone') || t.includes('tel')) return '📞'
  if (t.includes('group')) return '👥'
  return '🔗'
}

function linkOf(c: SiteContact) {
  const value = c.value || ''
  if (!value) return ''
  const t = (c.type || '').toLowerCase()
  if (t.includes('mail') || t.includes('email')) {
    return value.includes('@') ? `mailto:${value}` : ''
  }
  if (t.includes('phone') || t.includes('tel')) {
    return `tel:${value}`
  }
  if (/^https?:\/\//i.test(value)) return value
  return ''
}

onMounted(async () => {
  try {
    contacts.value = await getContacts()
  } catch {
    contacts.value = []
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

.contact-wrap {
  max-width: 940px;
}

.contact-card {
  padding: 28px 22px;
  text-align: center;
}

.contact-icon {
  font-size: 32px;
}

.contact-name {
  margin-top: 12px;
  font-size: 17px;
  font-weight: 600;
}

.contact-value {
  margin-top: 8px;
  color: var(--text-2);
  font-size: 14px;
  word-break: break-all;
}

.contact-value a:hover {
  color: var(--primary-color);
}

.contact-qrcode {
  margin: 16px auto 0;
  width: 140px;
  height: 140px;
  object-fit: contain;
  border: 1px solid var(--border);
  border-radius: 10px;
}

.contact-note {
  margin-top: 48px;
}

.block-title {
  font-size: 22px;
  margin-bottom: 16px;
}

.note-list {
  padding-left: 22px;
  list-style: disc;
  color: var(--text-2);
  font-size: 15px;
}

.note-list li {
  margin-bottom: 10px;
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
