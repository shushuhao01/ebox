<template>
  <div class="page-container">
    <!-- 日期范围 -->
    <div class="toolbar-card">
      <span class="page-title" style="margin-right: 8px">使用分析</span>
      <el-radio-group v-model="days" size="default" @change="loadAll">
        <el-radio-button :value="7">近 7 天</el-radio-button>
        <el-radio-button :value="30">近 30 天</el-radio-button>
        <el-radio-button :value="90">近 90 天</el-radio-button>
      </el-radio-group>
    </div>

    <!-- 行1：趋势 + 状态占比 -->
    <el-row :gutter="16">
      <el-col :xs="24" :md="16">
        <el-card shadow="never" class="chart-card">
          <template #header>
            <span class="chart-title">激活 / 心跳趋势</span>
            <span class="chart-sub">{{ days }} 天</span>
          </template>
          <div ref="trendRef" class="chart-box" v-loading="loading" />
        </el-card>
      </el-col>
      <el-col :xs="24" :md="8">
        <el-card shadow="never" class="chart-card">
          <template #header><span class="chart-title">状态占比</span></template>
          <div ref="statusRef" class="chart-box" v-loading="loading" />
        </el-card>
      </el-col>
    </el-row>

    <!-- 行2：时长分布 + 解绑统计 -->
    <el-row :gutter="16">
      <el-col :xs="24" :md="16">
        <el-card shadow="never" class="chart-card">
          <template #header><span class="chart-title">时长分布（库存码）</span></template>
          <div ref="durationRef" class="chart-box" v-loading="loading" />
        </el-card>
      </el-col>
      <el-col :xs="24" :md="8">
        <el-card shadow="never" class="chart-card">
          <template #header><span class="chart-title">解绑统计</span></template>
          <div class="unbind-wrap">
            <div class="unbind-big">{{ distribution.unbindCount }}</div>
            <div class="unbind-label">累计换机（解绑）次数</div>
            <el-divider />
            <div class="unbind-legend">换机码由管理员为已用激活码签发，用户换机解绑后系统自动记录。</div>
          </div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import * as echarts from 'echarts'
import { getTrend, getDistribution, type Distribution, type Trend } from '@/api/stats'
import { initChart, disposeChart, PALETTE, AXIS_TEXT } from '@/utils/echarts'

const days = ref(30)
const loading = ref(false)
const distribution = ref<Distribution>({ durationMap: {}, statusMap: {}, unbindCount: 0 })

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
    grid: { left: 50, right: 50, top: 40, bottom: 24 },
    xAxis: {
      type: 'category',
      data: t.dates,
      boundaryGap: false,
      axisLine: { lineStyle: { color: '#e5e7eb' } },
      axisLabel: { color: AXIS_TEXT, fontSize: 11 },
    },
    yAxis: [
      {
        type: 'value',
        name: '激活',
        splitLine: { lineStyle: { color: '#f0f2f7' } },
        axisLabel: { color: AXIS_TEXT },
      },
      {
        type: 'value',
        name: '心跳',
        splitLine: { show: false },
        axisLabel: { color: AXIS_TEXT },
      },
    ],
    series: [
      {
        name: '激活',
        type: 'line',
        yAxisIndex: 0,
        data: t.activates,
        smooth: true,
        symbolSize: 5,
        itemStyle: { color: '#3A7AFE' },
        areaStyle: { color: 'rgba(58,122,254,0.10)' },
      },
      {
        name: '心跳',
        type: 'line',
        yAxisIndex: 1,
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
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    grid: { left: 40, right: 20, top: 30, bottom: 30 },
    xAxis: {
      type: 'category',
      data: entries.map(([name]) => name),
      axisLine: { lineStyle: { color: '#e5e7eb' } },
      axisLabel: { color: AXIS_TEXT },
    },
    yAxis: {
      type: 'value',
      splitLine: { lineStyle: { color: '#f0f2f7' } },
      axisLabel: { color: AXIS_TEXT },
    },
    series: [
      {
        name: '激活码数量',
        type: 'bar',
        data: entries.map(([, value]) => value),
        barMaxWidth: 42,
        itemStyle: {
          color: {
            type: 'linear',
            x: 0, y: 0, x2: 0, y2: 1,
            colorStops: [
              { offset: 0, color: '#3A7AFE' },
              { offset: 1, color: '#93B5FE' },
            ],
          },
          borderRadius: [4, 4, 0, 0],
        },
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
  loading.value = true
  try {
    const [trend, dist] = await Promise.all([getTrend(days.value), getDistribution()])
    distribution.value = dist
    renderTrend(trend)
    renderStatus(dist)
    renderDuration(dist)
  } catch {
    // 拦截器已提示
  } finally {
    loading.value = false
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

.chart-sub {
  font-size: 12px;
  color: var(--text-secondary);
  margin-left: 8px;
}

.chart-box {
  height: 320px;
}

.unbind-wrap {
  padding: 30px 20px;
  text-align: center;

  .unbind-big {
    font-size: 52px;
    font-weight: 700;
    color: var(--primary-color);
    line-height: 1;
  }

  .unbind-label {
    margin-top: 12px;
    font-size: 14px;
    color: var(--text-main);
  }

  .unbind-legend {
    font-size: 12px;
    color: var(--text-secondary);
    text-align: left;
    line-height: 1.8;
  }
}
</style>
