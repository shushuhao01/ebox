<template>
  <div class="page-container">
    <!-- 顶部统计卡 -->
    <el-row :gutter="16">
      <el-col v-for="card in statCards" :key="card.label" :xs="12" :sm="12" :md="6">
        <el-card shadow="never" class="stat-card">
          <div class="stat-inner">
            <div class="stat-icon" :style="{ background: card.bg, color: card.color }">
              <el-icon :size="22"><component :is="card.icon" /></el-icon>
            </div>
            <div class="stat-info">
              <div class="stat-value">{{ card.value }}</div>
              <div class="stat-label">{{ card.label }}</div>
            </div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- 中部：趋势 + 状态占比 -->
    <el-row :gutter="16">
      <el-col :xs="24" :md="14">
        <el-card shadow="never" class="chart-card">
          <template #header><span class="chart-title">激活趋势（近 30 天）</span></template>
          <div ref="trendRef" class="chart-box" v-loading="chartLoading" />
        </el-card>
      </el-col>
      <el-col :xs="24" :md="10">
        <el-card shadow="never" class="chart-card">
          <template #header><span class="chart-title">状态占比</span></template>
          <div ref="statusRef" class="chart-box" v-loading="chartLoading" />
        </el-card>
      </el-col>
    </el-row>

    <!-- 下部：时长分布 + 最近心跳 -->
    <el-row :gutter="16">
      <el-col :xs="24" :md="10">
        <el-card shadow="never" class="chart-card">
          <template #header><span class="chart-title">时长分布（库存码）</span></template>
          <div ref="durationRef" class="chart-box" v-loading="chartLoading" />
        </el-card>
      </el-col>
      <el-col :xs="24" :md="14">
        <el-card shadow="never" class="chart-card">
          <template #header><span class="chart-title">最近心跳流水</span></template>
          <el-table :data="recentHbs" size="small" stripe max-height="300">
            <el-table-column label="时间" width="150">
              <template #default="{ row }">{{ formatTime(row.createdAt) }}</template>
            </el-table-column>
            <el-table-column label="动作" width="80" align="center">
              <template #default="{ row }">
                <el-tag :type="HB_ACTION[row.action]?.type || 'info'" size="small">
                  {{ HB_ACTION[row.action]?.label || row.action }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column label="IP" width="110" show-overflow-tooltip>
              <template #default="{ row }">{{ formatIp(row.ip) }}</template>
            </el-table-column>
            <el-table-column label="版本" prop="appVersion" width="80" />
            <el-table-column label="详情" min-width="200" show-overflow-tooltip>
              <template #default="{ row }">{{ formatHbDetail(row.detail) }}</template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import * as echarts from 'echarts'
import {
  getOverview, getTrend, getDistribution, getRecentHeartbeats,
  type Overview, type Trend, type Distribution,
} from '@/api/stats'
import type { Heartbeat } from '@/api/logs'
import { formatTime, formatIp, formatHbDetail, HB_ACTION } from '@/utils/format'
import { initChart, disposeChart, PALETTE, AXIS_TEXT } from '@/utils/echarts'

const overview = ref<Overview>({ total: 0, used: 0, revoked: 0, switched: 0, onlineDevices: 0, todayActivate: 0 })
const chartLoading = ref(false)
const recentHbs = ref<Heartbeat[]>([])

const statCards = computed(() => [
  { label: '码总数', value: overview.value.total, icon: 'Key', color: '#3A7AFE', bg: 'rgba(58,122,254,0.12)' },
  { label: '已用数', value: overview.value.used, icon: 'CircleCheck', color: '#22C55E', bg: 'rgba(34,197,94,0.12)' },
  { label: '在线设备', value: overview.value.onlineDevices, icon: 'Monitor', color: '#06B6D4', bg: 'rgba(6,182,212,0.12)' },
  { label: '今日激活', value: overview.value.todayActivate, icon: 'Lightning', color: '#F59E0B', bg: 'rgba(245,158,11,0.12)' },
])

const trendRef = ref<HTMLElement>()
const statusRef = ref<HTMLElement>()
const durationRef = ref<HTMLElement>()
let trendChart: echarts.ECharts | null = null
let statusChart: echarts.ECharts | null = null
let durationChart: echarts.ECharts | null = null

function renderTrend(t: Trend) {
  if (!trendRef.value) return
  trendChart = initChart(trendRef.value, {
    tooltip: { trigger: 'axis' },
    legend: { data: ['激活', '心跳'], textStyle: { color: AXIS_TEXT } },
    grid: { left: 40, right: 40, top: 40, bottom: 24 },
    xAxis: {
      type: 'category',
      data: t.dates,
      boundaryGap: false,
      axisLine: { lineStyle: { color: '#e5e7eb' } },
      axisLabel: { color: AXIS_TEXT, fontSize: 11 },
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { color: '#f0f2f7' } },
      axisLabel: { color: AXIS_TEXT },
    },
    series: [
      {
        name: '激活',
        type: 'line',
        data: t.activates,
        smooth: true,
        symbolSize: 5,
        itemStyle: { color: '#3A7AFE' },
        areaStyle: { color: 'rgba(58,122,254,0.10)' },
      },
      {
        name: '心跳',
        type: 'line',
        data: t.heartbeats,
        smooth: true,
        symbolSize: 5,
        itemStyle: { color: '#22C55E' },
        areaStyle: { color: 'rgba(34,197,94,0.10)' },
      },
    ],
  })
}

function renderStatus(d: Distribution) {
  if (!statusRef.value) return
  const entries = Object.entries(d.statusMap || {})
  if (!entries.length) {
    statusChart = initChart(statusRef.value, {
      title: { text: '暂无数据', left: 'center', top: 'middle', textStyle: { color: AXIS_TEXT, fontSize: 13, fontWeight: 'normal' } },
    })
    return
  }
  statusChart = initChart(statusRef.value, {
    tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
    legend: { bottom: 0, textStyle: { color: AXIS_TEXT } },
    color: PALETTE,
    series: [
      {
        type: 'pie',
        radius: ['42%', '68%'],
        center: ['50%', '45%'],
        avoidLabelOverlap: true,
        itemStyle: { borderRadius: 6, borderColor: '#fff', borderWidth: 2 },
        label: { show: false },
        emphasis: { label: { show: true, fontWeight: 'bold' } },
        data: entries.map(([name, value]) => ({ name, value })),
      },
    ],
  })
}

function renderDuration(d: Distribution) {
  if (!durationRef.value) return
  const entries = Object.entries(d.durationMap || {})
  if (!entries.length) {
    durationChart = initChart(durationRef.value, {
      title: { text: '暂无数据', left: 'center', top: 'middle', textStyle: { color: AXIS_TEXT, fontSize: 13, fontWeight: 'normal' } },
    })
    return
  }
  durationChart = initChart(durationRef.value, {
    tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
    legend: { bottom: 0, type: 'scroll', textStyle: { color: AXIS_TEXT } },
    color: PALETTE,
    series: [
      {
        type: 'pie',
        radius: '65%',
        center: ['50%', '45%'],
        itemStyle: { borderRadius: 6, borderColor: '#fff', borderWidth: 2 },
        label: { formatter: '{b}\n{d}%' },
        data: entries.map(([name, value]) => ({ name, value })),
      },
    ],
  })
}

function onResize() {
  trendChart?.resize()
  statusChart?.resize()
  durationChart?.resize()
}

async function loadAll() {
  chartLoading.value = true
  try {
    const [ov, trend, dist, hbs] = await Promise.allSettled([
      getOverview(),
      getTrend(30),
      getDistribution(),
      getRecentHeartbeats(10),
    ])
    if (ov.status === 'fulfilled') overview.value = ov.value
    if (trend.status === 'fulfilled') renderTrend(trend.value)
    if (dist.status === 'fulfilled') {
      renderStatus(dist.value)
      renderDuration(dist.value)
    }
    if (hbs.status === 'fulfilled') recentHbs.value = hbs.value
    const failed = [ov, trend, dist, hbs].filter((r) => r.status === 'rejected').length
    if (failed) ElMessage.warning(`部分统计数据加载失败（${failed} 项）`)
  } finally {
    chartLoading.value = false
  }
}

onMounted(() => {
  loadAll()
  window.addEventListener('resize', onResize)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', onResize)
  disposeChart(trendChart)
  disposeChart(statusChart)
  disposeChart(durationChart)
})
</script>

<style scoped lang="scss">
.stat-card {
  border-radius: var(--card-radius);
  box-shadow: var(--card-shadow);
  margin-bottom: 16px;

  :deep(.el-card__body) {
    padding: 18px;
  }
}

.stat-inner {
  display: flex;
  align-items: center;
  gap: 14px;
}

.stat-icon {
  width: 46px;
  height: 46px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.stat-value {
  font-size: 24px;
  font-weight: 700;
  color: var(--text-main);
  line-height: 1.2;
}

.stat-label {
  font-size: 13px;
  color: var(--text-secondary);
  margin-top: 2px;
}

.chart-card {
  border-radius: var(--card-radius);
  box-shadow: var(--card-shadow);
  margin-bottom: 16px;

  :deep(.el-card__header) {
    padding: 12px 16px;
    border-bottom: 1px solid #f0f2f7;
  }
}

.chart-title {
  font-size: 14px;
  font-weight: 600;
  color: var(--text-main);
}

.chart-box {
  height: 300px;
}
</style>
