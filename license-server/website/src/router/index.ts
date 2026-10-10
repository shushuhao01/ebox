import type { RouteRecordRaw } from 'vue-router'

import Home from '@/pages/Home.vue'
import Features from '@/pages/Features.vue'
import Cases from '@/pages/Cases.vue'
import CaseDetail from '@/pages/CaseDetail.vue'
import Download from '@/pages/Download.vue'
import Articles from '@/pages/Articles.vue'
import ArticleDetail from '@/pages/ArticleDetail.vue'
import Docs from '@/pages/Docs.vue'
import Contact from '@/pages/Contact.vue'
import Agreement from '@/pages/Agreement.vue'
import Privacy from '@/pages/Privacy.vue'
import Maintenance from '@/pages/Maintenance.vue'
import NotFound from '@/pages/NotFound.vue'
import ChannelRedirect from '@/pages/ChannelRedirect.vue'

export const routes: RouteRecordRaw[] = [
  { path: '/', name: 'home', component: Home, meta: { title: '首页' } },
  { path: '/features', name: 'features', component: Features, meta: { title: '功能介绍' } },
  { path: '/cases', name: 'cases', component: Cases, meta: { title: '使用案例' } },
  { path: '/cases/:id', name: 'case-detail', component: CaseDetail, meta: { title: '案例详情' } },
  { path: '/download', name: 'download', component: Download, meta: { title: '下载' } },
  { path: '/articles', name: 'articles', component: Articles, meta: { title: '文章中心' } },
  { path: '/articles/:slug', name: 'article-detail', component: ArticleDetail, meta: { title: '文章详情' } },
  { path: '/docs', name: 'docs', component: Docs, meta: { title: '文档中心' } },
  { path: '/contact', name: 'contact', component: Contact, meta: { title: '联系我们' } },
  { path: '/agreement', name: 'agreement', component: Agreement, meta: { title: '用户协议' } },
  { path: '/privacy', name: 'privacy', component: Privacy, meta: { title: '隐私政策' } },
  { path: '/maintenance', name: 'maintenance', component: Maintenance, meta: { title: '维护中' } },
  { path: '/c/:code', name: 'channel', component: ChannelRedirect, meta: { title: '跳转中' } },
  { path: '/:pathMatch(.*)*', name: 'not-found', component: NotFound, meta: { title: '页面不存在' } },
]

export default routes
