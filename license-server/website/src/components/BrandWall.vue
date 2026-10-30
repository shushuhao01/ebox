<template>
  <section class="section brand-wall">
    <div class="container">
      <div class="section-head">
        <h2 class="section-title">{{ title }}</h2>
        <p class="section-sub">{{ subtitle }}</p>
      </div>

      <template v-if="grouped">
        <div v-for="group in brandGroups" :key="group.label" class="brand-group">
          <div class="brand-group-label">{{ group.label }}</div>
          <div class="brand-tiles">
            <div v-for="b in group.items" :key="b.file" class="brand-tile">
              <img class="brand-logo" :src="iconUrl(b.file)" :alt="b.name" loading="lazy" />
              <span class="brand-name">{{ b.name }}</span>
            </div>
          </div>
        </div>
      </template>

      <div v-else class="brand-tiles">
        <div v-for="b in flatBrands" :key="b.file" class="brand-tile">
          <img class="brand-logo" :src="iconUrl(b.file)" :alt="b.name" loading="lazy" />
          <span class="brand-name">{{ b.name }}</span>
        </div>
      </div>

      <p class="brand-note">
        以上应用均提供 Windows 电脑客户端，都可以在 {{ siteName }} 中创建多个独立环境同时运行；
        其他任意 Windows 桌面程序同样支持多开。
      </p>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useSite } from '@/composables/useSite'

withDefaults(
  defineProps<{
    title?: string
    subtitle?: string
    grouped?: boolean
  }>(),
  {
    title: '支持多开的电脑客户端应用',
    subtitle: '企业微信、微信、QQ、钉钉、飞书……一台电脑同时登录多个客户端，互不干扰。',
    grouped: false,
  },
)

const { siteName } = useSite()

const base = import.meta.env.BASE_URL.endsWith('/')
  ? import.meta.env.BASE_URL
  : `${import.meta.env.BASE_URL}/`

function iconUrl(file: string) {
  return `${base}brands/${file}`
}

const brandGroups = [
  {
    label: '国内常用',
    items: [
      { file: 'wecom.png', name: '企业微信' },
      { file: 'wechat.png', name: '微信' },
      { file: 'qq.png', name: 'QQ' },
      { file: 'dingtalk.png', name: '钉钉' },
      { file: 'feishu.png', name: '飞书' },
      { file: 'douyin.png', name: '抖音' },
      { file: 'live-companion.png', name: '抖音直播伴侣' },
      { file: 'doubao.png', name: '豆包' },
      { file: 'capcut.png', name: '剪映' },
      { file: 'netease-music.png', name: '网易云音乐' },
      { file: 'uu-remote.png', name: 'UU远程' },
      { file: 'bitbrowser.png', name: '比特浏览器' },
      { file: 'qqmusic.png', name: 'QQ音乐' },
      { file: 'baidupan.png', name: '百度网盘' },
      { file: 'wps.png', name: 'WPS Office' },
      { file: 'xunlei.png', name: '迅雷' },
      { file: 'iqiyi.png', name: '爱奇艺' },
      { file: 'youku.png', name: '优酷' },
    ],
  },
  {
    label: '境外常用',
    items: [
      { file: 'whatsapp.png', name: 'WhatsApp' },
      { file: 'telegram.png', name: 'Telegram' },
      { file: 'discord.png', name: 'Discord' },
    ],
  },
]

const flatBrands = computed(() => brandGroups.flatMap((g) => g.items))
</script>

<style scoped>
.brand-wall .section-head {
  margin-bottom: 32px;
}

.brand-tiles {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(112px, 1fr));
  gap: 12px;
}

.brand-tile {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 10px;
  padding: 16px 8px;
  border: 1px solid var(--border);
  border-radius: var(--radius);
  background: #fff;
  transition:
    border-color 0.2s ease,
    box-shadow 0.2s ease,
    transform 0.2s ease;
}

.brand-tile:hover {
  border-color: var(--primary-color);
  box-shadow: var(--shadow-sm);
  transform: translateY(-2px);
}

.brand-logo {
  width: 40px;
  height: 40px;
  border-radius: 22%;
  object-fit: contain;
}

.brand-name {
  max-width: 100%;
  overflow: hidden;
  color: var(--text-2);
  font-size: 12px;
  line-height: 1.3;
  text-align: center;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.brand-group + .brand-group {
  margin-top: 26px;
}

.brand-group-label {
  margin-bottom: 12px;
  color: var(--text-3);
  font-size: 13px;
  font-weight: 600;
}

.brand-note {
  margin-top: 22px;
  color: var(--text-3);
  font-size: 13px;
  text-align: center;
}

@media (max-width: 768px) {
  .brand-tiles {
    grid-template-columns: repeat(auto-fill, minmax(92px, 1fr));
    gap: 10px;
  }
  .brand-logo {
    width: 34px;
    height: 34px;
  }
  .brand-tile {
    padding: 12px 6px;
  }
}
</style>
