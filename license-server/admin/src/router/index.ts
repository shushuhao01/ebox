import { createRouter, createWebHistory } from 'vue-router'
import { useUserStore } from '@/stores/user'

export interface MenuMeta {
  title: string
  icon?: string
  superOnly?: boolean
}

const routes = [
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/views/Login.vue'),
    meta: { public: true, title: '登录' },
  },
  {
    path: '/',
    component: () => import('@/layouts/MainLayout.vue'),
    redirect: '/dashboard',
    children: [
      { path: 'dashboard', name: 'Dashboard', component: () => import('@/views/Dashboard.vue'), meta: { title: '总览', icon: 'DataAnalysis' } },
      { path: 'licenses', name: 'Licenses', component: () => import('@/views/Licenses.vue'), meta: { title: '激活码管理', icon: 'Key' } },
      { path: 'batches', name: 'Batches', component: () => import('@/views/Batches.vue'), meta: { title: '码批次', icon: 'Collection' } },
      { path: 'customers', name: 'Customers', component: () => import('@/views/Customers.vue'), meta: { title: '客户管理', icon: 'User' } },
      { path: 'devices', name: 'Devices', component: () => import('@/views/Devices.vue'), meta: { title: '设备管理', icon: 'Monitor' } },
      { path: 'unbind-logs', name: 'UnbindLogs', component: () => import('@/views/UnbindLogs.vue'), meta: { title: '换机记录', icon: 'Refresh' } },
      { path: 'revoke-logs', name: 'RevokeLogs', component: () => import('@/views/RevokeLogs.vue'), meta: { title: '作废记录', icon: 'CircleClose' } },
      { path: 'recycle-bin', name: 'RecycleBin', component: () => import('@/views/RecycleBin.vue'), meta: { title: '回收站', icon: 'Delete' } },
      { path: 'analytics', name: 'Analytics', component: () => import('@/views/Analytics.vue'), meta: { title: '使用分析', icon: 'TrendCharts' } },
      { path: 'heartbeats', name: 'Heartbeats', component: () => import('@/views/Heartbeats.vue'), meta: { title: '心跳日志', icon: 'Timer' } },
      { path: 'settings', name: 'Settings', component: () => import('@/views/Settings.vue'), meta: { title: '系统设置', icon: 'Setting' } },
      { path: 'logs', name: 'Logs', component: () => import('@/views/Logs.vue'), meta: { title: '操作日志', icon: 'Document' } },
      { path: 'users', name: 'Users', component: () => import('@/views/Users.vue'), meta: { title: '管理员', icon: 'Lock', superOnly: true } },
    ],
  },
  {
    path: '/:pathMatch(.*)*',
    name: 'NotFound',
    component: () => import('@/views/NotFound.vue'),
    meta: { title: '404' },
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

router.beforeEach((to) => {
  const store = useUserStore()

  if (!to.meta.public && !store.isLogin) {
    return { path: '/login', query: { redirect: to.fullPath } }
  }
  if (to.path === '/login' && store.isLogin) {
    return '/dashboard'
  }
  // 仅超管可见页面
  if ((to.meta as MenuMeta | undefined)?.superOnly && !store.isSuperAdmin) {
    return '/dashboard'
  }
  return true
})

router.afterEach((to) => {
  const title = to.meta?.title as string | undefined
  document.title = title ? `${title} - eBox 授权服务平台` : 'eBox 授权服务平台'
})

export default router
