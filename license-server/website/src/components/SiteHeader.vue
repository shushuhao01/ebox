<template>
  <header class="site-header">
    <div class="container header-inner">
      <RouterLink to="/" class="brand">
        <img :src="logo" :alt="siteName" class="brand-logo" />
        <span class="brand-name">{{ siteName }}</span>
      </RouterLink>

      <nav class="nav" :class="{ open: menuOpen }">
        <template v-for="item in menus" :key="item.url">
          <a
            v-if="isExternal(item.url)"
            class="nav-link"
            :href="item.url"
            target="_blank"
            rel="noopener"
            @click="menuOpen = false"
          >
            {{ item.label }}
          </a>
          <RouterLink v-else class="nav-link" :to="item.url" @click="menuOpen = false">
            {{ item.label }}
          </RouterLink>
        </template>

        <a
          v-if="purchaseUrl"
          class="nav-link"
          :href="purchaseUrl"
          target="_blank"
          rel="noopener"
          @click="onBuy"
        >
          购买
        </a>

        <div class="nav-actions">
          <RouterLink class="btn btn-primary" to="/download" @click="menuOpen = false">免费下载</RouterLink>
        </div>
      </nav>

      <button class="menu-toggle" type="button" aria-label="菜单" @click="menuOpen = !menuOpen">
        <span></span>
        <span></span>
        <span></span>
      </button>
    </div>
  </header>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
import { track } from '@/api'
import { useSite } from '@/composables/useSite'

const { siteName, logo, purchaseUrl, topNav } = useSite()
const menuOpen = ref(false)

const defaultNav = [
  { label: '功能', url: '/features' },
  { label: '案例', url: '/cases' },
  { label: '下载', url: '/download' },
  { label: '文档', url: '/docs' },
  { label: '联系', url: '/contact' },
]

const menus = computed(() => {
  if (topNav.value.length) {
    return topNav.value.map((n) => ({ label: n.label, url: n.url }))
  }
  return defaultNav
})

function isExternal(url: string) {
  return /^https?:\/\//i.test(url)
}

function onBuy() {
  track('buy')
}
</script>

<style scoped>
.site-header {
  position: sticky;
  top: 0;
  z-index: 50;
  background: rgba(255, 255, 255, 0.92);
  backdrop-filter: saturate(180%) blur(12px);
  border-bottom: 1px solid var(--border);
}

.header-inner {
  height: 66px;
  display: flex;
  align-items: center;
  gap: 24px;
}

.brand {
  display: flex;
  align-items: center;
  gap: 10px;
  font-weight: 700;
  font-size: 19px;
}

.brand-logo {
  width: 32px;
  height: 32px;
  border-radius: 8px;
}

.nav {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-left: auto;
}

.nav-link {
  padding: 8px 14px;
  border-radius: 8px;
  font-size: 15px;
  color: var(--text-2);
  transition: all 0.2s ease;
}

.nav-link:hover,
.nav-link.router-link-active {
  color: var(--primary-color);
  background: var(--primary-light);
}

.nav-actions {
  margin-left: 10px;
}

.menu-toggle {
  display: none;
  flex-direction: column;
  gap: 5px;
  background: none;
  border: none;
  cursor: pointer;
  padding: 8px;
}

.menu-toggle span {
  width: 22px;
  height: 2px;
  background: var(--text-1);
  border-radius: 2px;
}

@media (max-width: 860px) {
  .menu-toggle {
    display: flex;
    margin-left: auto;
  }

  .nav {
    position: absolute;
    top: 66px;
    left: 0;
    right: 0;
    flex-direction: column;
    align-items: stretch;
    gap: 4px;
    background: #fff;
    border-bottom: 1px solid var(--border);
    padding: 12px 20px 20px;
    display: none;
  }

  .nav.open {
    display: flex;
  }

  .nav-link {
    padding: 12px 10px;
  }

  .nav-actions {
    margin: 10px 0 0;
  }

  .nav-actions .btn {
    width: 100%;
  }
}
</style>
