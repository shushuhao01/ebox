<template>
  <div class="page-container">
    <!-- 顶部工具栏 -->
    <div class="toolbar-card">
      <span class="page-title">
        <el-icon><Monitor /></el-icon>
        官网管理
      </span>
      <div class="toolbar-right">
        <el-tooltip content="在新窗口打开官网首页" placement="top">
          <el-button link type="primary" :icon="View" @click="openSite">预览官网</el-button>
        </el-tooltip>
      </div>
    </div>

    <!-- 模块切换 -->
    <el-tabs v-model="activeTab" tab-position="left" class="site-tabs">
      <el-tab-pane label="站点设置" name="settings">
        <SiteSettings v-if="rendered.settings" />
      </el-tab-pane>
      <el-tab-pane label="使用手册" name="doc">
        <SiteDoc v-if="rendered.doc" />
      </el-tab-pane>
      <el-tab-pane label="文章管理" name="articles">
        <SiteArticles v-if="rendered.articles" categories="tutorial,science,update" />
      </el-tab-pane>
      <el-tab-pane label="公告管理" name="notices">
        <SiteArticles v-if="rendered.notices" categories="notice" fixed-category />
      </el-tab-pane>
      <el-tab-pane label="案例管理" name="cases">
        <SiteCases v-if="rendered.cases" />
      </el-tab-pane>
      <el-tab-pane label="下载地址" name="mirrors">
        <SiteMirrors v-if="rendered.mirrors" />
      </el-tab-pane>
      <el-tab-pane label="导航菜单" name="nav">
        <SiteNav v-if="rendered.nav" />
      </el-tab-pane>
      <el-tab-pane label="联系方式" name="contacts">
        <SiteContacts v-if="rendered.contacts" />
      </el-tab-pane>
      <el-tab-pane label="媒体库" name="media">
        <SiteMedia v-if="rendered.media" />
      </el-tab-pane>
      <el-tab-pane label="访问与安全" name="security">
        <SiteSecurity v-if="rendered.security" />
      </el-tab-pane>
      <el-tab-pane label="访问统计" name="stats">
        <SiteStats v-if="rendered.stats" />
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup lang="ts">
import { reactive, ref, watch } from 'vue'
import { View } from '@element-plus/icons-vue'
import SiteSettings from './SiteSettings.vue'
import SiteDoc from './SiteDoc.vue'
import SiteArticles from './SiteArticles.vue'
import SiteCases from './SiteCases.vue'
import SiteMirrors from './SiteMirrors.vue'
import SiteNav from './SiteNav.vue'
import SiteContacts from './SiteContacts.vue'
import SiteMedia from './SiteMedia.vue'
import SiteSecurity from './SiteSecurity.vue'
import SiteStats from './SiteStats.vue'

const activeTab = ref('settings')

/** 已渲染过的面板，切换到时才挂载，避免一次性发起大量请求 */
const rendered = reactive<Record<string, boolean>>({ settings: true })

watch(activeTab, (tab) => {
  rendered[tab] = true
})

function openSite() {
  window.open('/', '_blank')
}
</script>

<style scoped lang="scss">
.toolbar-right {
  margin-left: auto;
}

.site-tabs {
  background: #fff;
  border-radius: var(--card-radius);
  box-shadow: var(--card-shadow);
  padding: 12px;
  min-height: 520px;

  :deep(.el-tabs__header) {
    min-width: 120px;
  }

  :deep(.el-tabs__content) {
    padding: 4px 8px;
    overflow: visible;
  }
}
</style>
