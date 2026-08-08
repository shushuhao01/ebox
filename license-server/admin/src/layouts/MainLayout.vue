<template>
  <div class="main-layout">
    <!-- 左侧菜单 -->
    <aside class="sidebar" :class="{ collapsed }">
      <div class="logo" @click="$router.push('/dashboard')">
        <el-icon :size="26" color="#fff"><Key /></el-icon>
        <span v-show="!collapsed" class="logo-text">eBox 授权平台</span>
      </div>
      <el-scrollbar class="menu-scroll">
        <el-menu
          :default-active="$route.path"
          :collapse="collapsed"
          :collapse-transition="false"
          background-color="transparent"
          router
        >
          <el-menu-item v-for="item in visibleMenus" :key="item.path" :index="item.path">
            <el-icon><component :is="item.meta.icon" /></el-icon>
            <template #title>{{ item.meta.title }}</template>
          </el-menu-item>
        </el-menu>
      </el-scrollbar>
    </aside>

    <!-- 右侧主体 -->
    <div class="main">
      <!-- 顶栏 -->
      <header class="header">
        <div class="header-left">
          <el-icon class="collapse-btn" :size="20" @click="collapsed = !collapsed">
            <Expand v-if="collapsed" />
            <Fold v-else />
          </el-icon>
          <el-breadcrumb separator="/">
            <el-breadcrumb-item :to="{ path: '/dashboard' }">首页</el-breadcrumb-item>
            <el-breadcrumb-item v-if="currentMenu">{{ currentMenu.meta.title }}</el-breadcrumb-item>
          </el-breadcrumb>
        </div>
        <div class="header-right">
          <!-- 服务健康灯 -->
          <div class="health" :title="healthText">
            <span class="health-dot" :class="health ? 'ok' : 'bad'"></span>
            <span class="health-text">服务{{ health ? '正常' : '异常' }}</span>
          </div>
          <el-divider direction="vertical" />
          <el-dropdown trigger="click" @command="onCommand">
            <span class="user-info">
              <el-avatar :size="30" class="user-avatar">{{ displayName.slice(0, 1) }}</el-avatar>
              <span class="user-name">{{ displayName }}</span>
              <el-icon><ArrowDown /></el-icon>
            </span>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item disabled>
                  角色：{{ store.isSuperAdmin ? '超级管理员' : '管理员' }}
                </el-dropdown-item>
                <el-dropdown-item divided command="logout">
                  <el-icon><SwitchButton /></el-icon>退出登录
                </el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </header>

      <!-- 主内容 -->
      <main class="content">
        <router-view />
      </main>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessageBox } from 'element-plus'
import { useUserStore } from '@/stores/user'
import type { MenuMeta } from '@/router'

const store = useUserStore()
const route = useRoute()
const router = useRouter()

const collapsed = ref(false)

const displayName = computed(() => store.displayName)

/** 菜单项：过滤仅超管可见 */
const menus = [
  { path: '/dashboard', meta: { title: '总览', icon: 'DataAnalysis' } },
  { path: '/licenses', meta: { title: '激活码管理', icon: 'Key' } },
  { path: '/batches', meta: { title: '码批次', icon: 'Collection' } },
  { path: '/customers', meta: { title: '客户管理', icon: 'User' } },
  { path: '/devices', meta: { title: '设备管理', icon: 'Monitor' } },
  { path: '/unbind-logs', meta: { title: '换机记录', icon: 'Refresh' } },
  { path: '/revoke-logs', meta: { title: '作废记录', icon: 'CircleClose' } },
  { path: '/recycle-bin', meta: { title: '回收站', icon: 'Delete' } },
  { path: '/analytics', meta: { title: '使用分析', icon: 'TrendCharts' } },
  { path: '/heartbeats', meta: { title: '心跳日志', icon: 'Timer' } },
  { path: '/settings', meta: { title: '系统设置', icon: 'Setting' } },
  { path: '/logs', meta: { title: '操作日志', icon: 'Document' } },
  { path: '/users', meta: { title: '管理员', icon: 'Lock', superOnly: true } },
]

const visibleMenus = computed(() => menus.filter((m) => !(m.meta as MenuMeta).superOnly || store.isSuperAdmin))

const currentMenu = computed(() => menus.find((m) => m.path === route.path))

// ============ 服务健康 ============
const health = ref(true)
const healthText = ref('正在检测服务状态...')

async function checkHealth() {
  try {
    const res = await fetch('/api/health', { cache: 'no-store' })
    health.value = res.ok
    healthText.value = health.value ? '后端服务正常' : '后端服务异常'
  } catch {
    health.value = false
    healthText.value = '后端服务异常'
  }
}

let healthTimer: number | undefined

onMounted(() => {
  checkHealth()
  healthTimer = window.setInterval(checkHealth, 30000)
})

onBeforeUnmount(() => {
  if (healthTimer) window.clearInterval(healthTimer)
})

// ============ 用户操作 ============
async function onCommand(cmd: string) {
  if (cmd === 'logout') {
    await ElMessageBox.confirm('确定要退出登录吗？', '提示', {
      type: 'warning',
      confirmButtonText: '退出',
      cancelButtonText: '取消',
    })
    store.logout()
    router.push('/login')
  }
}
</script>

<style scoped lang="scss">
.main-layout {
  height: 100%;
  display: flex;
}

.sidebar {
  width: var(--sidebar-width);
  background: linear-gradient(180deg, #1f2d4d 0%, #17233d 100%);
  display: flex;
  flex-direction: column;
  transition: width 0.2s;
  flex-shrink: 0;

  &.collapsed {
    width: var(--sidebar-collapsed-width);
  }

  .logo {
    height: 56px;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 8px;
    cursor: pointer;
    color: #fff;
    flex-shrink: 0;

    .logo-text {
      font-size: 16px;
      font-weight: 600;
      white-space: nowrap;
    }
  }

  .menu-scroll {
    flex: 1;
  }

  :deep(.el-menu) {
    border-right: none;

    .el-menu-item {
      color: #a8b6d4;
      height: 46px;
      margin: 2px 8px;
      border-radius: 6px;

      &:hover {
        background: rgba(58, 122, 254, 0.15);
        color: #fff;
      }

      &.is-active {
        background: var(--primary-color);
        color: #fff;
      }
    }
  }
}

.main {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.header {
  height: 56px;
  background: #fff;
  box-shadow: 0 1px 3px rgba(16, 24, 40, 0.06);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 16px;
  flex-shrink: 0;
  z-index: 10;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 12px;

  .collapse-btn {
    cursor: pointer;
    color: var(--text-secondary);
    &:hover {
      color: var(--primary-color);
    }
  }
}

.header-right {
  display: flex;
  align-items: center;
  gap: 12px;

  .health {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 13px;
    color: var(--text-secondary);

    .health-dot {
      width: 9px;
      height: 9px;
      border-radius: 50%;

      &.ok {
        background: var(--success-color);
        box-shadow: 0 0 0 3px rgba(34, 197, 94, 0.18);
      }
      &.bad {
        background: var(--danger-color);
        box-shadow: 0 0 0 3px rgba(239, 68, 68, 0.18);
      }
    }
  }

  .user-info {
    display: flex;
    align-items: center;
    gap: 8px;
    cursor: pointer;
    color: var(--text-main);
    outline: none;

    .user-avatar {
      background: var(--primary-color);
      font-size: 14px;
    }
    .user-name {
      font-size: 14px;
    }
  }
}

.content {
  flex: 1;
  overflow: auto;
}
</style>
