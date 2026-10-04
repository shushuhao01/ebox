<template>
  <div v-if="visible" class="svc-widget" :class="{ 'is-open': open }">
    <transition name="svc-pop">
      <div v-if="open" class="svc-panel" role="dialog" aria-label="联系客服">
        <div class="svc-head">
          <span class="svc-head-title">🎧 联系我们</span>
          <button class="svc-close" type="button" aria-label="关闭" @click="open = false">×</button>
        </div>

        <a v-if="email" class="svc-row" :href="`mailto:${email}`">
          <span class="svc-row-icon">✉️</span>
          <span class="svc-row-body">
            <span class="svc-row-label">邮箱</span>
            <span class="svc-row-value">{{ email }}</span>
          </span>
        </a>

        <a v-if="wechatLink" class="svc-wechat-btn" :href="wechatLink" target="_blank" rel="noopener">
          💬 微信客服
        </a>
        <p v-if="wechatLink" class="svc-wechat-tip">点击直达微信客服</p>

        <div v-if="wechatQr" class="svc-qr">
          <p class="svc-qr-title">微信二维码，长按识别二维码</p>
          <img :src="wechatQr" alt="微信二维码" />
          <p class="svc-qr-foot">长按二维码 → 识别图中二维码 → 添加客服</p>
        </div>

        <p v-if="!email && !wechatLink && !wechatQr" class="svc-empty">暂未配置联系方式</p>
      </div>
    </transition>

    <button
      class="svc-fab"
      type="button"
      :aria-label="open ? '收起客服' : '联系客服'"
      @click="toggle"
    >
      <svg
        v-if="!open"
        class="svc-fab-icon"
        viewBox="0 0 24 24"
        fill="none"
        stroke="currentColor"
        stroke-width="2"
        stroke-linecap="round"
        stroke-linejoin="round"
        aria-hidden="true"
      >
        <path d="M3 18v-6a9 9 0 0 1 18 0v6" />
        <path
          d="M21 19a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2v-3a2 2 0 0 1 2-2h3zM3 19a2 2 0 0 0 2 2h1a2 2 0 0 0 2-2v-3a2 2 0 0 0-2-2H3z"
        />
      </svg>
      <span v-else class="svc-fab-close">×</span>
    </button>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { getContacts, type SiteContact } from '@/api'

const contacts = ref<SiteContact[]>([])
const open = ref(false)

const visible = computed(() => contacts.value.length > 0)
const email = computed(() => contacts.value.find((c) => c.type === 'email' && c.value)?.value || '')
const wechatLink = computed(
  () => contacts.value.find((c) => c.type === 'wechat_service' && c.value)?.value || '',
)
const wechatQr = computed(
  () =>
    contacts.value.find(
      (c) => (c.type === 'wechat_service' || c.type === 'wechat') && c.qrcode,
    )?.qrcode || '',
)

function toggle() {
  open.value = !open.value
}

onMounted(async () => {
  try {
    contacts.value = await getContacts()
  } catch {
    contacts.value = []
  }
})
</script>

<style scoped>
.svc-widget {
  position: fixed;
  right: 24px;
  bottom: 24px;
  z-index: 999;
  display: flex;
  flex-direction: column;
  align-items: flex-end;
}

/* 呼吸悬浮按钮 */
.svc-fab {
  width: 56px;
  height: 56px;
  border: none;
  border-radius: 50%;
  background: var(--primary-color);
  color: #fff;
  font-size: 24px;
  line-height: 1;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: var(--shadow-md);
  animation: svc-breathe 2.4s ease-in-out infinite;
  transition: background 0.2s;
}

.svc-fab:hover {
  background: var(--primary-dark);
}

.svc-widget.is-open .svc-fab {
  animation: none;
}

.svc-fab-icon {
  width: 26px;
  height: 26px;
}

.svc-fab-close {
  font-size: 26px;
  line-height: 1;
}

@keyframes svc-breathe {
  0%,
  100% {
    transform: scale(1);
    box-shadow: 0 0 0 0 rgba(58, 122, 254, 0.45);
  }
  50% {
    transform: scale(1.06);
    box-shadow: 0 0 0 12px rgba(58, 122, 254, 0);
  }
}

/* 展开面板 */
.svc-panel {
  width: 300px;
  max-width: calc(100vw - 32px);
  margin-bottom: 14px;
  padding: 18px;
  background: #fff;
  border: 1px solid var(--border);
  border-radius: var(--radius);
  box-shadow: var(--shadow-md);
}

.svc-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 14px;
}

.svc-head-title {
  font-size: 16px;
  font-weight: 700;
}

.svc-close {
  border: none;
  background: none;
  color: var(--text-3);
  font-size: 20px;
  line-height: 1;
  cursor: pointer;
}

.svc-close:hover {
  color: var(--text-1);
}

.svc-row {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 12px 14px;
  border: 1px solid var(--border);
  border-radius: 10px;
}

.svc-row-icon {
  font-size: 18px;
}

.svc-row-body {
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.svc-row-label {
  color: var(--text-3);
  font-size: 12px;
}

.svc-row-value {
  color: var(--text-1);
  font-size: 14px;
  word-break: break-all;
}

.svc-wechat-btn {
  display: block;
  margin-top: 10px;
  padding: 12px;
  text-align: center;
  background: var(--primary-color);
  color: #fff;
  font-size: 15px;
  font-weight: 600;
  border-radius: 10px;
  transition: background 0.2s;
}

.svc-wechat-btn:hover {
  background: var(--primary-dark);
}

.svc-wechat-tip {
  margin-top: 8px;
  text-align: center;
  color: var(--text-3);
  font-size: 12px;
}

.svc-qr {
  margin-top: 14px;
  padding: 14px;
  border: 1px solid var(--border);
  border-radius: 10px;
  text-align: center;
}

.svc-qr-title {
  color: var(--text-2);
  font-size: 13px;
}

.svc-qr img {
  margin: 10px auto 0;
  width: 150px;
  height: 150px;
  object-fit: contain;
}

.svc-qr-foot {
  margin-top: 8px;
  color: var(--text-3);
  font-size: 12px;
}

.svc-empty {
  padding: 12px 0;
  text-align: center;
  color: var(--text-3);
  font-size: 13px;
}

/* 展开动画 */
.svc-pop-enter-active,
.svc-pop-leave-active {
  transition: opacity 0.2s ease, transform 0.2s ease;
}

.svc-pop-enter-from,
.svc-pop-leave-to {
  opacity: 0;
  transform: translateY(10px) scale(0.98);
}

@media (max-width: 768px) {
  .svc-widget {
    right: 16px;
    bottom: 16px;
  }
}
</style>
