<template>
  <div class="features">
    <section class="page-hero">
      <div class="container">
        <h1 class="page-title">功能介绍</h1>
        <p class="page-sub">{{ siteName }} 为需要在同一台电脑上运行多个应用实例的用户设计，通过环境隔离让多开更稳定、更省心。</p>
      </div>
    </section>

    <section class="section">
      <div class="container">
        <div v-for="group in groups" :key="group.title" class="feature-group">
          <div class="group-head">
            <h2 class="group-title">{{ group.title }}</h2>
            <p class="group-sub">{{ group.sub }}</p>
          </div>
          <div class="grid grid-3">
            <div v-for="item in group.items" :key="item.title" class="card feature-card">
              <div class="feature-icon">{{ item.icon }}</div>
              <h3 class="feature-title">{{ item.title }}</h3>
              <p class="feature-desc">{{ item.desc }}</p>
            </div>
          </div>
        </div>
      </div>
    </section>

    <section class="section section-soft">
      <div class="container">
        <div class="section-head">
          <h2 class="section-title">使用流程</h2>
          <p class="section-sub">四步完成部署，无需复杂配置。</p>
        </div>
        <div class="steps">
          <div v-for="(step, index) in steps" :key="step.title" class="step">
            <div class="step-index">{{ index + 1 }}</div>
            <h3 class="step-title">{{ step.title }}</h3>
            <p class="step-desc">{{ step.desc }}</p>
          </div>
        </div>
      </div>
    </section>

    <section class="section">
      <div class="container center">
        <h2 class="section-title">准备开始了吗？</h2>
        <p class="section-sub">下载安装包，立即体验环境隔离带来的多开便利。</p>
        <div class="cta-actions">
          <RouterLink class="btn btn-primary btn-lg" to="/download">免费下载</RouterLink>
          <RouterLink class="btn btn-ghost btn-lg" to="/docs">查看使用手册</RouterLink>
        </div>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { useSite } from '@/composables/useSite'
import { usePageHead } from '@/composables/usePageHead'

const { siteName } = useSite()
usePageHead('功能介绍', '了解 eBox 的核心功能：环境隔离、自动登录、可视化管理与轻量多开。')

const groups = [
  {
    title: '核心能力',
    sub: '围绕环境隔离打造的多开能力，稳定且易用。',
    items: [
      { icon: '🔒', title: '环境独立隔离', desc: '每个环境拥有独立的运行空间，配置与数据互不干扰，避免相互影响。' },
      { icon: '⚡', title: '免扫码自动登录', desc: '环境登录状态可保存复用，省去反复扫码的繁琐步骤。' },
      { icon: '🧩', title: '可视化管理', desc: '以卡片方式展示所有环境，启动、停止与状态查看一目了然。' },
      { icon: '🪶', title: '轻量低占用', desc: '无需虚拟机，资源占用低，多个环境同时运行依然流畅。' },
      { icon: '🗂️', title: '数据集中管理', desc: '所有环境的数据统一存放，便于备份、迁移与维护。' },
      { icon: '🔄', title: '在线更新', desc: '支持在线检查并更新到最新版本，持续获得功能与稳定性改进。' },
    ],
  },
  {
    title: '系统工具',
    sub: '内置实用工具，随时掌握电脑状态，保持系统整洁。',
    items: [
      { icon: '🧹', title: '全盘垃圾清理', desc: '一键清理系统临时文件、浏览器与应用缓存、聊天图片视频等全盘垃圾，并清空回收站，释放磁盘空间。' },
      { icon: '🎯', title: '改名与一键置前', desc: '环境可随时重命名；已运行的环境点击启动即把窗口快速切换到前台，无需在任务栏中翻找。' },
      { icon: '📊', title: '系统状态看板', desc: '实时查看 CPU、内存、GPU、显存与各磁盘的使用率曲线，快速掌握电脑运行情况。' },
    ],
  },
  {
    title: '使用体验',
    sub: '从细节出发，降低多开操作的门槛。',
    items: [
      { icon: '🚀', title: '安装即用', desc: '下载安装包后按引导完成安装，无需额外环境配置。' },
      { icon: '📦', title: '按需多开', desc: '根据需要创建任意数量的环境，随时增减。' },
      { icon: '🛡️', title: '稳定运行', desc: '环境之间相互独立，单个环境异常不影响其他环境。' },
    ],
  },
]

const steps = [
  { title: '下载安装', desc: '从下载中心获取最新安装包并完成安装。' },
  { title: '创建环境', desc: '在软件中创建所需数量的独立环境。' },
  { title: '配置登录', desc: '为每个环境完成一次登录配置。' },
  { title: '开始多开', desc: '同时启动多个环境，独立运行互不干扰。' },
]
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
  max-width: 680px;
  color: var(--text-2);
  font-size: 17px;
}

.feature-group {
  margin-bottom: 56px;
}

.feature-group:last-child {
  margin-bottom: 0;
}

.group-head {
  margin-bottom: 28px;
}

.group-title {
  font-size: 24px;
}

.group-sub {
  margin-top: 8px;
  color: var(--text-3);
  font-size: 15px;
}

.feature-card {
  padding: 26px 22px;
}

.feature-icon {
  font-size: 28px;
}

.feature-title {
  margin-top: 14px;
  font-size: 17px;
}

.feature-desc {
  margin-top: 8px;
  color: var(--text-2);
  font-size: 14px;
}

.steps {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 22px;
}

.step {
  background: #fff;
  border: 1px solid var(--border);
  border-radius: var(--radius);
  padding: 26px 22px;
  box-shadow: var(--shadow-sm);
}

.step-index {
  width: 34px;
  height: 34px;
  border-radius: 50%;
  background: var(--primary-color);
  color: #fff;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 700;
}

.step-title {
  margin-top: 16px;
  font-size: 17px;
}

.step-desc {
  margin-top: 8px;
  color: var(--text-2);
  font-size: 14px;
}

.cta-actions {
  margin-top: 26px;
  display: flex;
  gap: 14px;
  justify-content: center;
  flex-wrap: wrap;
}

@media (max-width: 900px) {
  .steps {
    grid-template-columns: repeat(2, 1fr);
  }
}

@media (max-width: 768px) {
  .page-title {
    font-size: 30px;
  }
  .steps {
    grid-template-columns: 1fr;
  }
}
</style>
