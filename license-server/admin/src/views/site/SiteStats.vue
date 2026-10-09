<template>
  <div class="site-panel">
    <div class="sub-toolbar">
      <el-radio-group v-model="days" size="default" @change="reload">
        <el-radio-button :value="7">近 7 天</el-radio-button>
        <el-radio-button :value="30">近 30 天</el-radio-button>
        <el-radio-button :value="90">近 90 天</el-radio-button>
      </el-radio-group>
      <el-button @click="reload">
        <template #icon>
          <el-icon :class="{ spinning: refreshing }"><Refresh /></el-icon>
        </template>
        刷新
      </el-button>
      <span class="range-tip" v-if="analytics">统计区间：{{ analytics.range.start }} ~ {{ analytics.range.end }}</span>
      <div class="sub-toolbar-right">
        <span class="online-badge" :title="`最近 ${onlineMinutes} 分钟内有访问的访客数`">
          <span class="online-dot"></span>实时在线 {{ onlineCount }}
        </span>
        <el-button :icon="RefreshRight" :loading="syncing" @click="handleSyncRelease">同步版本信息</el-button>
      </div>
    </div>

    <el-tabs v-model="activeTab" class="stats-tabs" @tab-change="onTabChange">
      <!-- ==================== 流量分析 ==================== -->
      <el-tab-pane label="流量分析" name="overview">
        <el-row :gutter="12" class="stat-cards">
          <el-col v-for="c in cards" :key="c.label" :xs="12" :sm="8" :md="6" :lg="3">
            <div class="stat-card">
              <div class="stat-num">{{ c.value }}</div>
              <div class="stat-lbl">{{ c.label }}</div>
            </div>
          </el-col>
        </el-row>

        <el-row :gutter="16">
          <el-col :xs="24" :md="16">
            <el-card shadow="never" class="chart-card">
              <template #header>
                <span class="chart-title">访问趋势</span>
                <span class="chart-sub">{{ days }} 天 · PV / UV</span>
              </template>
              <div ref="trendRef" class="chart-box" v-loading="loading" />
            </el-card>
          </el-col>
          <el-col :xs="24" :md="8">
            <el-card shadow="never" class="chart-card">
              <template #header><span class="chart-title">流量来源</span></template>
              <div ref="sourceRef" class="chart-box" v-loading="loading" />
            </el-card>
          </el-col>
        </el-row>

        <el-row :gutter="16">
          <el-col :xs="24" :md="16">
            <el-card shadow="never" class="chart-card">
              <template #header>
                <span class="chart-title">时段分布（0-23 时）</span>
                <span class="chart-sub" v-if="summary.peakHourPv">高峰 {{ padHour(summary.peakHour) }} · {{ summary.peakHourPv }} 次</span>
              </template>
              <div ref="hourRef" class="chart-box" v-loading="loading" />
            </el-card>
          </el-col>
          <el-col :xs="24" :md="8">
            <el-card shadow="never" class="chart-card">
              <template #header><span class="chart-title">设备类型</span></template>
              <div ref="deviceRef" class="chart-box" v-loading="loading" />
            </el-card>
          </el-col>
        </el-row>

        <el-row :gutter="16">
          <el-col :xs="24" :md="12">
            <el-card shadow="never" class="chart-card">
              <template #header><span class="chart-title">操作系统</span></template>
              <div ref="osRef" class="chart-box" v-loading="loading" />
            </el-card>
          </el-col>
          <el-col :xs="24" :md="12">
            <el-card shadow="never" class="chart-card">
              <template #header><span class="chart-title">浏览器</span></template>
              <div ref="browserRef" class="chart-box" v-loading="loading" />
            </el-card>
          </el-col>
        </el-row>

        <el-row :gutter="16">
          <el-col :xs="24" :md="12">
            <el-card shadow="never" class="chart-card">
              <template #header><span class="chart-title">地域分布 Top 10</span></template>
              <div ref="regionRef" class="chart-box" v-loading="loading" />
            </el-card>
          </el-col>
          <el-col :xs="24" :md="12">
            <el-card shadow="never" class="chart-card">
              <template #header><span class="chart-title">网络运营商 Top 10</span></template>
              <div ref="ispRef" class="chart-box" v-loading="loading" />
            </el-card>
          </el-col>
        </el-row>

        <el-row :gutter="16">
          <el-col :xs="24" :md="12">
            <el-card shadow="never" class="chart-card">
              <template #header><span class="chart-title">热门页面 Top 10</span></template>
              <el-table :data="analytics?.pages || []" size="small" stripe>
                <el-table-column type="index" label="#" width="48" />
                <el-table-column label="页面路径" prop="path" min-width="160" show-overflow-tooltip />
                <el-table-column label="PV" prop="pv" width="80" align="right" />
                <el-table-column label="UV" prop="uv" width="80" align="right" />
              </el-table>
            </el-card>
          </el-col>
          <el-col :xs="24" :md="12">
            <el-card shadow="never" class="chart-card">
              <template #header><span class="chart-title">活跃 IP Top 50</span></template>
              <el-table :data="analytics?.ips || []" size="small" stripe height="330">
                <el-table-column type="index" label="#" width="48" />
                <el-table-column label="IP" prop="ip" min-width="130" />
                <el-table-column label="归属地" min-width="140">
                  <template #default="{ row }">{{ [row.province, row.city].filter(Boolean).join(' ') || '-' }}</template>
                </el-table-column>
                <el-table-column label="运营商" prop="isp" min-width="90" show-overflow-tooltip />
                <el-table-column label="次数" prop="pv" width="70" align="right" />
              </el-table>
            </el-card>
          </el-col>
        </el-row>
      </el-tab-pane>

      <!-- ==================== 访问明细 ==================== -->
      <el-tab-pane label="访问明细" name="visits">
        <div class="visit-filter">
          <el-date-picker
            v-model="filters.dateRange"
            type="daterange"
            value-format="YYYY-MM-DD"
            range-separator="至"
            start-placeholder="开始日期"
            end-placeholder="结束日期"
            style="width: 250px"
          />
          <el-input v-model="filters.ip" placeholder="IP" clearable style="width: 140px" />
          <el-input v-model="filters.path" placeholder="访问路径" clearable style="width: 150px" />
          <el-select v-model="filters.device" placeholder="设备" clearable style="width: 120px">
            <el-option v-for="d in deviceOptions" :key="d.value" :label="d.label" :value="d.value" />
          </el-select>
          <el-select v-model="filters.source" placeholder="来源" clearable style="width: 120px">
            <el-option v-for="s in sourceOptions" :key="s" :label="s" :value="s" />
          </el-select>
          <el-input
            v-model="filters.keyword"
            placeholder="IP / 路径 / 归属地 / UA 关键字"
            clearable
            style="width: 200px"
            @keyup.enter="searchVisits"
          />
          <el-button type="primary" :icon="Search" @click="searchVisits">查询</el-button>
          <el-button @click="resetFilters">重置</el-button>
        </div>

        <el-table v-loading="visitLoading" :data="visits" stripe>
          <el-table-column label="时间" width="170">
            <template #default="{ row }">{{ fmtTime(row.createdAt) }}</template>
          </el-table-column>
          <el-table-column label="IP" prop="ip" width="140" />
          <el-table-column label="归属地" min-width="150">
            <template #default="{ row }">
              {{ [row.country, row.province, row.city].filter((v) => v && v !== '中国').join(' ') || '-' }}
            </template>
          </el-table-column>
          <el-table-column label="运营商" prop="isp" min-width="100" show-overflow-tooltip />
          <el-table-column label="设备 / 系统 / 浏览器" min-width="200">
            <template #default="{ row }">
              <el-tag size="small" type="info" class="mr4">{{ deviceLabel(row.device) }}</el-tag>
              <span class="dim">{{ [row.os, row.browser].filter(Boolean).join(' · ') || '-' }}</span>
            </template>
          </el-table-column>
          <el-table-column label="来源" prop="source" width="100" />
          <el-table-column label="访问路径" prop="path" min-width="180" show-overflow-tooltip />
          <el-table-column label="来源页" prop="referer" min-width="180" show-overflow-tooltip>
            <template #default="{ row }">{{ row.referer || '直接访问' }}</template>
          </el-table-column>
        </el-table>
        <el-empty v-if="!visits.length && !visitLoading" description="暂无访问明细" />

        <div class="visit-pager">
          <el-pagination
            v-model:current-page="page"
            v-model:page-size="pageSize"
            :total="total"
            :page-sizes="[20, 50, 100]"
            layout="total, sizes, prev, pager, next, jumper"
            background
            @current-change="loadVisits"
            @size-change="searchVisits"
          />
        </div>
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, nextTick } from 'vue'
import { ElMessage } from 'element-plus'
import { Refresh, RefreshRight, Search } from '@element-plus/icons-vue'
import * as echarts from 'echarts'
import {
  getSiteAnalytics,
  getSiteVisits,
  getSiteOnline,
  syncRelease,
  type NameValue,
  type SiteAnalytics,
  type SiteVisitLog,
} from '@/api/site'
import { initChart, setChartOption, disposeChart, PALETTE, AXIS_TEXT, type EChartsOption } from '@/utils/echarts'

const days = ref(30)
const activeTab = ref('overview')
const loading = ref(false)
const syncing = ref(false)
const refreshing = ref(false)
const analytics = ref<SiteAnalytics | null>(null)

// 实时在线（最近 N 分钟内有访问行为的去重访客）
const onlineMinutes = 5
const onlineCount = ref(0)
let onlineTimer: number | undefined

const DEVICE_LABEL: Record<string, string> = {
  desktop: '桌面端',
  mobile: '移动端',
  tablet: '平板',
  bot: '机器人',
  unknown: '未知',
}
const deviceOptions = [
  { label: '桌面端', value: 'desktop' },
  { label: '移动端', value: 'mobile' },
  { label: '平板', value: 'tablet' },
  { label: '机器人', value: 'bot' },
  { label: '未知', value: 'unknown' },
]
function deviceLabel(v: string): string {
  return DEVICE_LABEL[v] || v || '未知'
}

const EMPTY_SUMMARY = {
  pv: 0, uv: 0, ips: 0, downloads: 0, buyClicks: 0,
  todayPv: 0, todayUv: 0, avgPv: 0, peakHour: 0, peakHourPv: 0,
}
const summary = computed(() => analytics.value?.summary ?? EMPTY_SUMMARY)
const cards = computed(() => {
  const s = summary.value
  return [
    { label: '今日访问 (PV)', value: s.todayPv },
    { label: '今日访客 (UV)', value: s.todayUv },
    { label: '区间访问 (PV)', value: s.pv },
    { label: '区间访客 (UV)', value: s.uv },
    { label: '独立 IP', value: s.ips },
    { label: '下载点击', value: s.downloads },
    { label: '购买点击', value: s.buyClicks },
    { label: '日均访问', value: s.avgPv },
  ]
})
const sourceOptions = computed(() => (analytics.value?.sources || []).map((s) => s.name))

// ==================== 图表 ====================

const trendRef = ref<HTMLElement>()
const sourceRef = ref<HTMLElement>()
const hourRef = ref<HTMLElement>()
const deviceRef = ref<HTMLElement>()
const osRef = ref<HTMLElement>()
const browserRef = ref<HTMLElement>()
const regionRef = ref<HTMLElement>()
const ispRef = ref<HTMLElement>()

const charts: Record<string, echarts.ECharts | null> = {}

function upsert(key: string, el: HTMLElement | undefined, option: EChartsOption) {
  if (!el) return
  if (!charts[key]) charts[key] = initChart(el, option)
  else setChartOption(charts[key], option)
}

function emptyOption(text = '暂无数据'): EChartsOption {
  return {
    title: { text, left: 'center', top: 'center', textStyle: { color: '#9CA3AF', fontSize: 13, fontWeight: 'normal' } },
  }
}

function pieOption(list: NameValue[]): EChartsOption {
  return {
    tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
    legend: { bottom: 0, type: 'scroll', textStyle: { color: AXIS_TEXT, fontSize: 11 } },
    color: PALETTE,
    series: [
      {
        type: 'pie',
        radius: ['42%', '68%'],
        center: ['50%', '44%'],
        data: list.map((x) => ({ name: x.name, value: x.value })),
        itemStyle: { borderRadius: 6, borderColor: '#fff', borderWidth: 2 },
        label: { show: false },
        emphasis: { label: { show: true, fontSize: 13, fontWeight: 'bold' } },
      },
    ],
  }
}

function hbarOption(list: NameValue[]): EChartsOption {
  if (!list.length) return emptyOption()
  const items = list.slice().reverse()
  return {
    tooltip: { trigger: 'axis', axisPointer: { type: 'shadow' } },
    grid: { left: 96, right: 32, top: 16, bottom: 16 },
    xAxis: { type: 'value', splitLine: { lineStyle: { color: '#f0f2f7' } }, axisLabel: { color: AXIS_TEXT } },
    yAxis: {
      type: 'category',
      data: items.map((x) => x.name),
      axisLine: { lineStyle: { color: '#e5e7eb' } },
      axisLabel: { color: AXIS_TEXT, fontSize: 11 },
    },
    series: [
      {
        type: 'bar',
        data: items.map((x) => x.value),
        barMaxWidth: 16,
        itemStyle: {
          borderRadius: [0, 4, 4, 0],
          color: {
            type: 'linear', x: 0, y: 0, x2: 1, y2: 0,
            colorStops: [{ offset: 0, color: '#3A7AFE' }, { offset: 1, color: '#93B5FE' }],
          },
        },
      },
    ],
  }
}

function renderCharts() {
  const a = analytics.value
  if (!a) return

  if (!a.trend.dates.length) upsert('trend', trendRef.value, emptyOption())
  else {
    upsert('trend', trendRef.value, {
      tooltip: { trigger: 'axis' },
      legend: { data: ['访问量(PV)', '访客数(UV)'], top: 0, left: 'center', textStyle: { color: AXIS_TEXT } },
      grid: { left: 48, right: 24, top: 40, bottom: 24 },
      xAxis: {
        type: 'category', data: a.trend.dates, boundaryGap: false,
        axisLine: { lineStyle: { color: '#e5e7eb' } },
        axisLabel: { color: AXIS_TEXT, fontSize: 11 },
      },
      yAxis: { type: 'value', splitLine: { lineStyle: { color: '#f0f2f7' } }, axisLabel: { color: AXIS_TEXT } },
      series: [
        {
          name: '访问量(PV)', type: 'line', smooth: true, showSymbol: false, data: a.trend.pv,
          itemStyle: { color: '#3A7AFE' }, areaStyle: { color: 'rgba(58,122,254,0.12)' },
        },
        {
          name: '访客数(UV)', type: 'line', smooth: true, showSymbol: false, data: a.trend.uv,
          itemStyle: { color: '#22C55E' }, areaStyle: { color: 'rgba(34,197,94,0.10)' },
        },
      ],
    })
  }

  upsert('source', sourceRef.value, a.sources.length ? pieOption(a.sources) : emptyOption())

  const hourTotal = a.hours.pv.reduce((s, v) => s + v, 0)
  if (!hourTotal) upsert('hour', hourRef.value, emptyOption())
  else {
    upsert('hour', hourRef.value, {
      tooltip: { trigger: 'axis' },
      grid: { left: 44, right: 16, top: 24, bottom: 28 },
      xAxis: {
        type: 'category', data: a.hours.labels,
        axisLine: { lineStyle: { color: '#e5e7eb' } },
        axisLabel: { color: AXIS_TEXT, fontSize: 10, interval: 2 },
      },
      yAxis: { type: 'value', splitLine: { lineStyle: { color: '#f0f2f7' } }, axisLabel: { color: AXIS_TEXT } },
      series: [
        {
          name: '访问量', type: 'bar', data: a.hours.pv, barMaxWidth: 22,
          itemStyle: {
            borderRadius: [4, 4, 0, 0],
            color: {
              type: 'linear', x: 0, y: 0, x2: 0, y2: 1,
              colorStops: [{ offset: 0, color: '#3A7AFE' }, { offset: 1, color: '#93B5FE' }],
            },
          },
        },
      ],
    })
  }

  const devices = a.devices.map((d) => ({ name: deviceLabel(d.name), value: d.value }))
  upsert('device', deviceRef.value, devices.length ? pieOption(devices) : emptyOption())
  upsert('os', osRef.value, a.os.length ? pieOption(a.os) : emptyOption())
  upsert('browser', browserRef.value, a.browsers.length ? pieOption(a.browsers) : emptyOption())
  upsert('region', regionRef.value, hbarOption(a.regions))
  upsert('isp', ispRef.value, hbarOption(a.isps))
}

function resizeCharts() {
  Object.values(charts).forEach((c) => c?.resize())
}

function onTabChange(name: string | number) {
  if (name === 'overview') nextTick(() => resizeCharts())
}

// ==================== 访问明细 ====================

const visits = ref<SiteVisitLog[]>([])
const visitLoading = ref(false)
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const filters = ref<{ dateRange: [string, string] | null; ip: string; path: string; device: string; source: string; keyword: string }>({
  dateRange: null,
  ip: '',
  path: '',
  device: '',
  source: '',
  keyword: '',
})

async function loadVisits() {
  visitLoading.value = true
  try {
    const res = await getSiteVisits({
      page: page.value,
      pageSize: pageSize.value,
      ip: filters.value.ip || undefined,
      path: filters.value.path || undefined,
      device: filters.value.device || undefined,
      source: filters.value.source || undefined,
      keyword: filters.value.keyword || undefined,
      start: filters.value.dateRange?.[0],
      end: filters.value.dateRange?.[1],
    })
    visits.value = res.list
    total.value = res.total
  } finally {
    visitLoading.value = false
  }
}

function searchVisits() {
  page.value = 1
  loadVisits()
}

function resetFilters() {
  filters.value = { dateRange: null, ip: '', path: '', device: '', source: '', keyword: '' }
  searchVisits()
}

// ==================== 加载 ====================

async function loadAnalytics() {
  loading.value = true
  try {
    analytics.value = await getSiteAnalytics(days.value)
    await nextTick()
    renderCharts()
  } finally {
    loading.value = false
  }
}

async function reload() {
  refreshing.value = true
  try {
    await loadAnalytics()
    await loadVisits()
    await loadOnline()
  } finally {
    refreshing.value = false
  }
}

async function loadOnline() {
  try {
    const r = await getSiteOnline(onlineMinutes)
    onlineCount.value = r.online
  } catch {
    // 静默失败：保留上一次数值，避免轮询报错打断页面
  }
}

async function handleSyncRelease() {
  syncing.value = true
  try {
    await syncRelease()
    ElMessage.success('版本信息已同步')
  } catch {
    // 拦截器已提示
  } finally {
    syncing.value = false
  }
}

// ==================== 工具 ====================

function fmtTime(s: string): string {
  if (!s) return '-'
  const d = new Date(s)
  if (Number.isNaN(d.getTime())) return s
  const p = (n: number) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`
}
function padHour(h: number): string {
  return `${String(h).padStart(2, '0')}:00`
}

onMounted(async () => {
  await reload()
  window.addEventListener('resize', resizeCharts)
  // 实时在线数据每 30 秒轮询一次
  onlineTimer = window.setInterval(loadOnline, 30000)
})

onBeforeUnmount(() => {
  window.removeEventListener('resize', resizeCharts)
  if (onlineTimer) window.clearInterval(onlineTimer)
  Object.values(charts).forEach((c) => disposeChart(c))
})
</script>

<style scoped lang="scss">
// 刷新按钮点击后图标旋转动画
@keyframes refresh-spin {
  from {
    transform: rotate(0deg);
  }
  to {
    transform: rotate(360deg);
  }
}

.sub-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 14px;

  .spinning {
    animation: refresh-spin 0.8s linear infinite;
  }

  .range-tip {
    font-size: 12px;
    color: var(--text-secondary);
  }

  .sub-toolbar-right {
    margin-left: auto;
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .online-badge {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    height: 32px;
    padding: 0 12px;
    border: 1px solid var(--el-border-color);
    border-radius: var(--el-border-radius-base);
    background: var(--el-fill-color-blank);
    color: var(--el-text-color-regular);
    font-size: 14px;
    line-height: 1;
    white-space: nowrap;

    .online-dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background: #22c55e;
      box-shadow: 0 0 0 3px rgba(34, 197, 94, 0.15);
    }
  }
}

.stats-tabs {
  :deep(.el-tabs__header) {
    margin-bottom: 14px;
  }
}

.stat-cards {
  margin-bottom: 4px;
}

.stat-card {
  background: #f7f9fc;
  border-radius: 8px;
  padding: 14px 10px;
  text-align: center;
  margin-bottom: 12px;

  .stat-num {
    font-size: 24px;
    font-weight: 700;
    color: var(--primary-color);
    line-height: 1.2;
  }

  .stat-lbl {
    margin-top: 4px;
    font-size: 12px;
    color: var(--text-secondary);
  }
}

.chart-card {
  margin-bottom: 16px;

  .chart-title {
    font-weight: 600;
  }

  .chart-sub {
    margin-left: 8px;
    font-size: 12px;
    color: var(--text-secondary);
  }
}

.chart-box {
  height: 300px;
}

.visit-filter {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
  margin-bottom: 14px;
}

.visit-pager {
  display: flex;
  justify-content: flex-end;
  margin-top: 14px;
}

.mr4 {
  margin-right: 4px;
}

.dim {
  color: var(--text-secondary);
}
</style>
