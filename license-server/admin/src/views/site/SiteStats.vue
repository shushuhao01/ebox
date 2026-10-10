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
        <VisitCharts ref="overviewChartsRef" :analytics="analytics" :days="days" :loading="loading" />
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

      <!-- ==================== 渠道链接 ==================== -->
      <el-tab-pane label="渠道链接" name="channels">
        <!-- 渠道列表 -->
        <template v-if="channelView === 'list'">
          <div class="visit-filter">
            <el-input
              v-model="channelKeyword"
              placeholder="名称 / 编码 / 渠道标识"
              clearable
              style="width: 220px"
              @keyup.enter="searchChannels"
            />
            <el-button type="primary" :icon="Search" @click="searchChannels">查询</el-button>
            <el-button type="primary" :icon="Plus" @click="openCreateChannel">新建渠道链接</el-button>
          </div>

          <el-table v-loading="channelLoading" :data="channels" stripe>
            <el-table-column label="渠道名称" prop="name" min-width="150" show-overflow-tooltip />
            <el-table-column label="渠道编码" width="130">
              <template #default="{ row }">
                <el-tag size="small" effect="plain">{{ row.code }}</el-tag>
              </template>
            </el-table-column>
            <el-table-column label="推广短链" min-width="240">
              <template #default="{ row }">
                <span class="short-url" @click="copyText(row.shortUrl, '短链已复制')">{{ row.shortUrl }}</span>
              </template>
            </el-table-column>
            <el-table-column label="渠道标识" prop="channel" width="110" show-overflow-tooltip />
            <el-table-column label="落地页" prop="targetPath" min-width="150" show-overflow-tooltip />
            <el-table-column label="点击 / 去重" width="120" align="right">
              <template #default="{ row }">{{ row.clickCount }} / {{ row.uniqueClickCount }}</template>
            </el-table-column>
            <el-table-column label="状态" width="80">
              <template #default="{ row }">
                <el-tag size="small" :type="row.enabled === 1 ? 'success' : 'info'">
                  {{ row.enabled === 1 ? '启用' : '停用' }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column label="操作" width="270" fixed="right">
              <template #default="{ row }">
                <el-button link type="primary" :icon="DataAnalysis" @click="openChannelDetail(row)">分析</el-button>
                <el-button link type="primary" :icon="Picture" @click="openQrcode(row)">二维码</el-button>
                <el-button link type="primary" :icon="Edit" @click="openEditChannel(row)">编辑</el-button>
                <el-button link type="danger" :icon="Delete" @click="removeChannel(row)">删除</el-button>
              </template>
            </el-table-column>
          </el-table>
          <el-empty v-if="!channels.length && !channelLoading" description="暂无渠道链接" />

          <div class="visit-pager">
            <el-pagination
              v-model:current-page="channelPage"
              v-model:page-size="channelPageSize"
              :total="channelTotal"
              :page-sizes="[20, 50, 100]"
              layout="total, sizes, prev, pager, next, jumper"
              background
              @current-change="loadChannels"
              @size-change="searchChannels"
            />
          </div>
        </template>

        <!-- 渠道分析 -->
        <template v-else>
          <div class="channel-detail-head">
            <el-button link :icon="ArrowLeft" @click="backToChannelList">返回列表</el-button>
            <span class="channel-detail-name">{{ currentChannel?.name }}</span>
            <el-tag size="small" effect="plain">{{ currentChannel?.code }}</el-tag>
            <span class="channel-detail-url" @click="copyText(currentChannel?.shortUrl || '', '短链已复制')">
              {{ currentChannel?.shortUrl }}
            </span>
          </div>

          <VisitCharts
            ref="channelChartsRef"
            :analytics="channelAnalytics"
            :days="days"
            :loading="channelAnalyticsLoading"
          />

          <el-card shadow="never" class="chart-card">
            <template #header><span class="chart-title">访问明细</span></template>
            <div class="visit-filter">
              <el-date-picker
                v-model="channelFilters.dateRange"
                type="daterange"
                value-format="YYYY-MM-DD"
                range-separator="至"
                start-placeholder="开始日期"
                end-placeholder="结束日期"
                style="width: 250px"
              />
              <el-input v-model="channelFilters.ip" placeholder="IP" clearable style="width: 140px" />
              <el-input
                v-model="channelFilters.keyword"
                placeholder="IP / 路径 / 归属地 / UA 关键字"
                clearable
                style="width: 200px"
                @keyup.enter="searchChannelVisits"
              />
              <el-button type="primary" :icon="Search" @click="searchChannelVisits">查询</el-button>
              <el-button @click="resetChannelFilters">重置</el-button>
            </div>

            <el-table v-loading="channelVisitLoading" :data="channelVisits" stripe>
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
              <el-table-column label="访问路径" prop="path" min-width="180" show-overflow-tooltip />
              <el-table-column label="来源页" prop="referer" min-width="180" show-overflow-tooltip>
                <template #default="{ row }">{{ row.referer || '直接访问' }}</template>
              </el-table-column>
            </el-table>
            <el-empty v-if="!channelVisits.length && !channelVisitLoading" description="暂无访问明细" />

            <div class="visit-pager">
              <el-pagination
                v-model:current-page="channelVisitPage"
                v-model:page-size="channelVisitPageSize"
                :total="channelVisitTotal"
                :page-sizes="[20, 50, 100]"
                layout="total, sizes, prev, pager, next, jumper"
                background
                @current-change="loadChannelVisits"
                @size-change="searchChannelVisits"
              />
            </div>
          </el-card>
        </template>
      </el-tab-pane>
    </el-tabs>

    <!-- ==================== 新建 / 编辑渠道链接 ==================== -->
    <el-dialog
      v-model="channelDialogVisible"
      :title="channelDialogMode === 'create' ? '新建渠道链接' : '编辑渠道链接'"
      width="520px"
      destroy-on-close
    >
      <el-form ref="channelFormRef" :model="channelForm" :rules="channelRules" label-width="90px">
        <el-form-item label="渠道名称" prop="name">
          <el-input v-model="channelForm.name" placeholder="如：微博推广 / 春季活动" maxlength="128" />
        </el-form-item>
        <el-form-item label="渠道编码" prop="code">
          <el-input v-model="channelForm.code" placeholder="留空自动生成 6 位随机码（小写字母/数字/_-）" maxlength="32" />
        </el-form-item>
        <el-form-item label="渠道标识" prop="channel">
          <el-input v-model="channelForm.channel" placeholder="可选，如：weibo / wechat / douyin" maxlength="64" />
        </el-form-item>
        <el-form-item label="落地页" prop="targetPath">
          <el-input v-model="channelForm.targetPath" placeholder="如：/ 或 /download" maxlength="255" />
        </el-form-item>
        <el-form-item label="备注" prop="remark">
          <el-input v-model="channelForm.remark" type="textarea" :rows="2" placeholder="可选" maxlength="512" />
        </el-form-item>
        <el-form-item label="状态">
          <el-switch v-model="channelForm.enabled" :active-value="1" :inactive-value="0" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="channelDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="channelSaving" @click="submitChannel">保存</el-button>
      </template>
    </el-dialog>

    <!-- ==================== 渠道二维码 ==================== -->
    <el-dialog v-model="qrcodeVisible" title="渠道二维码" width="360px">
      <div class="qrcode-box" v-loading="qrcodeLoading">
        <img v-if="qrcodeData?.dataUrl" :src="qrcodeData.dataUrl" alt="渠道二维码" class="qrcode-img" />
        <div v-if="qrcodeData" class="qrcode-url">{{ qrcodeData.url }}</div>
      </div>
      <template #footer>
        <el-button @click="copyText(qrcodeData?.url || '', '短链已复制')">复制短链</el-button>
        <a
          v-if="qrcodeData?.dataUrl"
          class="el-button el-button--primary"
          :href="qrcodeData.dataUrl"
          :download="`channel-${qrcodeData.code}.png`"
        >
          下载二维码
        </a>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox, type FormInstance, type FormRules } from 'element-plus'
import { Refresh, RefreshRight, Search, Plus, Edit, Delete, DataAnalysis, Picture, ArrowLeft } from '@element-plus/icons-vue'
import {
  getSiteAnalytics,
  getSiteVisits,
  getSiteOnline,
  syncRelease,
  getChannels,
  createChannel,
  updateChannel,
  deleteChannel,
  getChannelAnalytics,
  getChannelVisits,
  getChannelQrcode,
  type SiteAnalytics,
  type SiteVisitLog,
  type ChannelLink,
  type ChannelPayload,
} from '@/api/site'
import { copyText } from '@/utils/format'
import VisitCharts from '@/components/VisitCharts.vue'

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

const sourceOptions = computed(() => (analytics.value?.sources || []).map((s) => s.name))

const overviewChartsRef = ref<InstanceType<typeof VisitCharts>>()
const channelChartsRef = ref<InstanceType<typeof VisitCharts>>()

function onTabChange(name: string | number) {
  nextTick(() => {
    if (name === 'overview') overviewChartsRef.value?.resize()
    else if (name === 'channels' && channelView.value === 'detail') channelChartsRef.value?.resize()
  })
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

// ==================== 渠道链接 ====================

const channelView = ref<'list' | 'detail'>('list')
const channels = ref<ChannelLink[]>([])
const channelLoading = ref(false)
const channelPage = ref(1)
const channelPageSize = ref(20)
const channelTotal = ref(0)
const channelKeyword = ref('')
const currentChannel = ref<ChannelLink | null>(null)

const channelAnalytics = ref<SiteAnalytics | null>(null)
const channelAnalyticsLoading = ref(false)
const channelVisits = ref<SiteVisitLog[]>([])
const channelVisitLoading = ref(false)
const channelVisitPage = ref(1)
const channelVisitPageSize = ref(20)
const channelVisitTotal = ref(0)
const channelFilters = ref<{ dateRange: [string, string] | null; ip: string; keyword: string }>({
  dateRange: null,
  ip: '',
  keyword: '',
})

async function loadChannels() {
  channelLoading.value = true
  try {
    const res = await getChannels({
      page: channelPage.value,
      pageSize: channelPageSize.value,
      keyword: channelKeyword.value || undefined,
    })
    channels.value = res.list
    channelTotal.value = res.total
  } finally {
    channelLoading.value = false
  }
}

function searchChannels() {
  channelPage.value = 1
  loadChannels()
}

async function openChannelDetail(row: ChannelLink) {
  currentChannel.value = row
  channelView.value = 'detail'
  channelVisitPage.value = 1
  channelFilters.value = { dateRange: null, ip: '', keyword: '' }
  await Promise.all([loadChannelAnalytics(), loadChannelVisits()])
  await nextTick()
  channelChartsRef.value?.resize()
}

function backToChannelList() {
  channelView.value = 'list'
  currentChannel.value = null
}

async function loadChannelAnalytics() {
  if (!currentChannel.value) return
  channelAnalyticsLoading.value = true
  try {
    channelAnalytics.value = await getChannelAnalytics(currentChannel.value.code, days.value)
  } finally {
    channelAnalyticsLoading.value = false
  }
}

async function loadChannelVisits() {
  if (!currentChannel.value) return
  channelVisitLoading.value = true
  try {
    const res = await getChannelVisits(currentChannel.value.code, {
      page: channelVisitPage.value,
      pageSize: channelVisitPageSize.value,
      ip: channelFilters.value.ip || undefined,
      keyword: channelFilters.value.keyword || undefined,
      start: channelFilters.value.dateRange?.[0],
      end: channelFilters.value.dateRange?.[1],
    })
    channelVisits.value = res.list
    channelVisitTotal.value = res.total
  } finally {
    channelVisitLoading.value = false
  }
}

function searchChannelVisits() {
  channelVisitPage.value = 1
  loadChannelVisits()
}

function resetChannelFilters() {
  channelFilters.value = { dateRange: null, ip: '', keyword: '' }
  searchChannelVisits()
}

// ---------- 新建 / 编辑 ----------

interface ChannelForm {
  name: string
  code: string
  channel: string
  targetPath: string
  remark: string
  enabled: number
}

const channelDialogVisible = ref(false)
const channelDialogMode = ref<'create' | 'edit'>('create')
const channelSaving = ref(false)
const channelFormRef = ref<FormInstance>()
const channelForm = ref<ChannelForm>({
  name: '',
  code: '',
  channel: '',
  targetPath: '/',
  remark: '',
  enabled: 1,
})
const channelRules: FormRules = {
  name: [{ required: true, message: '请输入渠道名称', trigger: 'blur' }],
}

function openCreateChannel() {
  channelDialogMode.value = 'create'
  channelForm.value = { name: '', code: '', channel: '', targetPath: '/', remark: '', enabled: 1 }
  channelDialogVisible.value = true
}

function openEditChannel(row: ChannelLink) {
  channelDialogMode.value = 'edit'
  channelForm.value = {
    name: row.name,
    code: row.code,
    channel: row.channel,
    targetPath: row.targetPath,
    remark: row.remark || '',
    enabled: row.enabled,
  }
  currentChannel.value = row
  channelDialogVisible.value = true
}

async function submitChannel() {
  const form = channelFormRef.value
  if (!form) return
  const valid = await form.validate().catch(() => false)
  if (!valid) return
  channelSaving.value = true
  try {
    if (channelDialogMode.value === 'create') {
      await createChannel(channelForm.value)
      ElMessage.success('渠道链接已创建')
    } else if (currentChannel.value) {
      await updateChannel(currentChannel.value.id, channelForm.value)
      ElMessage.success('渠道链接已更新')
    }
    channelDialogVisible.value = false
    await loadChannels()
  } catch {
    // 拦截器已提示
  } finally {
    channelSaving.value = false
  }
}

async function removeChannel(row: ChannelLink) {
  try {
    await ElMessageBox.confirm(`确定删除渠道链接「${row.name}」吗？删除后短链将失效。`, '删除确认', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消',
    })
  } catch {
    return
  }
  try {
    await deleteChannel(row.id)
    ElMessage.success('已删除')
    await loadChannels()
  } catch {
    // 拦截器已提示
  }
}

// ---------- 二维码 ----------

const qrcodeVisible = ref(false)
const qrcodeLoading = ref(false)
const qrcodeData = ref<{ code: string; url: string; dataUrl: string } | null>(null)

async function openQrcode(row: ChannelLink) {
  qrcodeVisible.value = true
  qrcodeLoading.value = true
  qrcodeData.value = null
  try {
    qrcodeData.value = await getChannelQrcode(row.code)
  } catch {
    // 拦截器已提示
  } finally {
    qrcodeLoading.value = false
  }
}

// ==================== 加载 ====================

async function loadAnalytics() {
  loading.value = true
  try {
    analytics.value = await getSiteAnalytics(days.value)
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
    if (activeTab.value === 'channels') {
      if (channelView.value === 'list') await loadChannels()
      else await Promise.all([loadChannelAnalytics(), loadChannelVisits()])
    }
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

onMounted(async () => {
  await reload()
  // 实时在线数据每 30 秒轮询一次
  onlineTimer = window.setInterval(loadOnline, 30000)
})

onBeforeUnmount(() => {
  if (onlineTimer) window.clearInterval(onlineTimer)
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

.chart-card {
  margin-bottom: 16px;

  .chart-title {
    font-weight: 600;
  }
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

.short-url {
  color: var(--primary-color);
  cursor: pointer;
  word-break: break-all;

  &:hover {
    text-decoration: underline;
  }
}

.channel-detail-head {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 14px;

  .channel-detail-name {
    font-size: 16px;
    font-weight: 600;
  }

  .channel-detail-url {
    color: var(--primary-color);
    cursor: pointer;
    font-size: 13px;

    &:hover {
      text-decoration: underline;
    }
  }
}

.qrcode-box {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  min-height: 220px;

  .qrcode-img {
    width: 240px;
    height: 240px;
  }

  .qrcode-url {
    font-size: 12px;
    color: var(--text-secondary);
    word-break: break-all;
    text-align: center;
  }
}
</style>
