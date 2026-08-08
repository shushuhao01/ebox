<template>
  <div class="page-container">
    <!-- 工具栏 -->
    <div class="toolbar-card">
      <el-input
        v-model="filters.search"
        placeholder="搜索码 / 客户名 / 指纹"
        clearable
        style="width: 240px"
        :prefix-icon="Search"
        @keyup.enter="handleSearch"
        @clear="handleSearch"
      />
      <el-select v-model="filters.status" placeholder="状态" clearable style="width: 130px" @change="handleSearch">
        <el-option v-for="(v, k) in KEY_STATUS" :key="k" :label="v.label" :value="Number(k)" />
      </el-select>
      <el-select v-model="filters.type" placeholder="类型" clearable style="width: 120px" @change="handleSearch">
        <el-option v-for="(v, k) in KEY_TYPE" :key="k" :label="v.label" :value="Number(k)" />
      </el-select>
      <el-date-picker
        v-model="dateRange"
        type="daterange"
        range-separator="至"
        start-placeholder="开始日期"
        end-placeholder="结束日期"
        value-format="YYYY-MM-DD"
        style="width: 240px"
        @change="handleSearch"
      />
      <el-button :icon="RefreshLeft" @click="handleReset">重置</el-button>

      <div class="toolbar-right">
        <template v-if="selection.length">
          <el-tag type="warning" effect="plain">已选 {{ selection.length }} 条</el-tag>
          <el-button type="danger" plain :icon="CircleClose" :loading="batchRevoking" @click="handleBatchRevoke">
            批量作废
          </el-button>
          <el-button type="danger" :icon="Delete" :loading="batchDeleting" @click="handleBatchDelete">
            批量删除
          </el-button>
          <el-button :icon="RefreshLeft" @click="selection = []">取消选择</el-button>
        </template>
        <el-button type="primary" :icon="Plus" @click="openGenerate">生成激活码</el-button>
        <el-button :icon="Download" :loading="exporting" @click="handleExport">导出 CSV</el-button>
      </div>
    </div>

    <!-- 表格 -->
    <div class="table-card">
      <el-table :data="list" v-loading="loading" stripe height="calc(100vh - 300px)" @selection-change="onSelectionChange">
        <el-table-column type="selection" width="45" fixed="left" />
        <el-table-column label="激活码" min-width="260">
          <template #default="{ row }">
            <div class="code-cell">
              <span class="code-font">{{ row.code }}</span>
              <el-button link type="primary" :icon="CopyDocument" @click="copyText(row.code)" />
            </div>
          </template>
        </el-table-column>
        <el-table-column label="类型" width="80" align="center">
          <template #default="{ row }">
            <el-tag :type="KEY_TYPE[row.type]?.type || 'info'" size="small">{{ KEY_TYPE[row.type]?.label || row.type }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="时长 / 到期" width="120">
          <template #default="{ row }">
            <span v-if="row.type === 1">{{ formatDuration(row.durationSec) }}</span>
            <span v-else class="text-secondary">{{ formatUnixTs(row.expireAt) }}</span>
          </template>
        </el-table-column>
        <el-table-column label="绑定" width="80" align="center">
          <template #default="{ row }">
            <el-tag :type="row.bound === 1 ? 'primary' : 'info'" size="small" effect="plain">
              {{ row.bound === 1 ? '绑定' : '通用' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="解绑上限" width="110" align="center">
          <template #default="{ row }">{{ formatUnbindMax(row.unbindMax) }}</template>
        </el-table-column>
        <el-table-column label="状态" width="90" align="center">
          <template #default="{ row }">
            <el-tag :type="KEY_STATUS[row.status]?.type || 'info'" size="small">
              {{ KEY_STATUS[row.status]?.label || row.status }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="客户ID" width="90" align="center">
          <template #default="{ row }">{{ row.customerId || '-' }}</template>
        </el-table-column>
        <el-table-column label="首次激活" width="165">
          <template #default="{ row }">{{ formatTime(row.usedAt) }}</template>
        </el-table-column>
        <el-table-column label="创建时间" width="165">
          <template #default="{ row }">{{ formatTime(row.createdAt) }}</template>
        </el-table-column>
        <el-table-column label="操作" width="220" fixed="right">
          <template #default="{ row }">
            <el-button link type="primary" size="small" @click="openDetail(row)">详情</el-button>
            <el-button
              v-if="row.status === 1"
              link
              type="success"
              size="small"
              @click="handleConvert(row)"
            >换机</el-button>
            <el-button v-if="row.status === 2" link type="success" size="small" @click="handleRestore(row)">恢复</el-button>
            <el-button v-if="row.status === 2" link type="danger" size="small" @click="handleDelete(row)">删除</el-button>
            <el-button v-if="row.status !== 2 && row.status !== 5" link type="danger" size="small" @click="handleRevoke(row)">作废</el-button>
          </template>
        </el-table-column>
        <template #empty>
          <el-empty description="暂无激活码" :image-size="80" />
        </template>
      </el-table>

      <div class="pagination-wrap">
        <el-pagination
          v-model:current-page="page"
          v-model:page-size="pageSize"
          :total="total"
          :page-sizes="[20, 50, 100]"
          layout="total, sizes, prev, pager, next, jumper"
          @current-change="load"
          @size-change="handleSizeChange"
        />
      </div>
    </div>

    <!-- 生成激活码对话框（两步） -->
    <el-dialog v-model="generateVisible" title="生成激活码" width="620px" :close-on-click-modal="false" @closed="resetGenerate">
      <el-steps :active="genStep" finish-status="success" align-center style="margin-bottom: 20px">
        <el-step title="基础设置" />
        <el-step title="生成结果" />
      </el-steps>

      <!-- 第一步：基础设置 -->
      <el-form v-if="genStep === 0" ref="genFormRef" :model="genForm" label-width="90px" label-position="left">
        <div class="step-header">
          <span>基础设置</span>
          <el-button link type="primary" :icon="RefreshLeft" @click="resetGenerateForm">一键清空</el-button>
        </div>
        <el-form-item label="时长" required>
          <div class="chip-wrap">
            <el-check-tag
              v-for="d in durationChips"
              :key="d.label"
              :checked="!customMode && genForm.durationSec === d.sec"
              @change="selectDuration(d.sec)"
            >
              {{ d.label }}
            </el-check-tag>
          </div>
          <div class="custom-duration">
            <el-checkbox v-model="customMode" @change="onCustomModeChange">自定义</el-checkbox>
            <el-input-number
              v-model="customDays"
              :min="1"
              :max="36500"
              :step="30"
              :disabled="!customMode"
              style="width: 130px"
              @change="onCustomDaysChange"
            />
            <span class="text-secondary">天（输入即生效）</span>
          </div>
        </el-form-item>
        <el-form-item label="绑定模式" required>
          <el-radio-group v-model="genForm.bindMode">
            <el-radio-button v-for="m in bindModes" :key="m.value" :value="m.value">{{ m.label }}</el-radio-button>
          </el-radio-group>
        </el-form-item>
        <el-form-item label="数量" required>
          <el-input-number v-model="genForm.count" :min="1" :max="500" />
          <span class="text-secondary" style="margin-left: 8px">（1 ~ 500）</span>
        </el-form-item>
        <el-form-item label="客户">
          <el-select v-model="genForm.customerId" clearable filterable placeholder="选择客户（可选）" style="width: 300px">
            <el-option v-for="c in customers" :key="c.id" :label="c.name" :value="c.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="码批次">
          <el-select
            v-model="genForm.batchId"
            filterable
            clearable
            allow-create
            default-first-option
            placeholder="选择已有批次，或输入新批次名回车创建（可选）"
            style="width: 300px"
          >
            <el-option v-for="b in batches" :key="b.id" :label="`${b.name}（${b.totalCount ?? 0}码）`" :value="b.id" />
          </el-select>
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="genForm.remark" placeholder="备注（可选）" maxlength="255" style="width: 300px" />
        </el-form-item>
      </el-form>

      <!-- 第二步：生成结果 -->
      <div v-else>
        <div class="step-header">
          <span>生成结果（{{ genResult.codes.length }} 个）</span>
          <el-button link type="primary" :icon="CopyDocument" @click="copyAllCodes">全部复制</el-button>
        </div>
        <el-scrollbar max-height="320px">
          <div v-for="(c, i) in genResult.codes" :key="i" class="code-line">
            <span class="code-font">{{ c }}</span>
            <el-button link type="primary" :icon="CopyDocument" @click="copyText(c)" />
          </div>
        </el-scrollbar>
        <el-alert
          v-if="genResult.batchId"
          type="info"
          :closable="false"
          show-icon
          :title="`已归入批次 #${genResult.batchId}`"
          style="margin-top: 12px"
        />
      </div>

      <template #footer>
        <el-button v-if="genStep === 1" @click="genStep = 0">上一步</el-button>
        <el-button @click="generateVisible = false">关闭</el-button>
        <el-button v-if="genStep === 0" type="primary" :loading="generating" @click="submitGenerate">
          立即生成
        </el-button>
        <el-button v-else type="primary" @click="resetGenerate">再次生成</el-button>
      </template>
    </el-dialog>

    <!-- 详情抽屉 -->
    <el-drawer v-model="detailVisible" title="激活码详情" size="560px">
      <template v-if="detail">
        <div class="detail-code code-font">{{ detail.key.code }}</div>
        <el-descriptions :column="2" border size="small" style="margin-top: 16px">
          <el-descriptions-item label="状态">
            <el-tag :type="KEY_STATUS[detail.key.status]?.type || 'info'" size="small">
              {{ KEY_STATUS[detail.key.status]?.label || detail.key.status }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="类型">
            <el-tag :type="KEY_TYPE[detail.key.type]?.type || 'info'" size="small">
              {{ KEY_TYPE[detail.key.type]?.label || detail.key.type }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="时长 / 到期">
            {{ detail.key.type === 1 ? formatDuration(detail.key.durationSec) : formatUnixTs(detail.key.expireAt) }}
          </el-descriptions-item>
          <el-descriptions-item label="绑定">
            {{ detail.key.bound === 1 ? '绑定' : '通用' }} / {{ formatUnbindMax(detail.key.unbindMax) }}
          </el-descriptions-item>
          <el-descriptions-item label="客户ID">{{ detail.key.customerId || '-' }}</el-descriptions-item>
          <el-descriptions-item label="批次ID">{{ detail.key.batchId || '-' }}</el-descriptions-item>
          <el-descriptions-item label="创建时间">{{ formatTime(detail.key.createdAt) }}</el-descriptions-item>
          <el-descriptions-item label="使用时间">{{ formatTime(detail.key.usedAt) }}</el-descriptions-item>
          <el-descriptions-item label="作废原因" :span="2">{{ detail.key.revokedReason || '-' }}</el-descriptions-item>
          <el-descriptions-item label="备注" :span="2">{{ detail.key.remark || '-' }}</el-descriptions-item>
        </el-descriptions>

        <div v-if="detail.key.status === 2" style="margin-top: 14px; text-align: right">
          <el-button type="danger" plain :icon="Delete" @click="handleDelete(detail.key)">删除该激活码</el-button>
        </div>

        <div class="detail-section-title">
          绑定设备（{{ detail.devices.length }}） · 本月解绑次数：<b>{{ detail.unbindCount }}</b>
        </div>
        <el-table :data="detail.devices" size="small" stripe max-height="280">
          <el-table-column label="设备ID" prop="id" width="90" />
          <el-table-column label="指纹" min-width="120">
            <template #default="{ row }">
              <el-tooltip :content="row.machineFp" placement="top">
                <span class="code-font">{{ row.machineFp.slice(0, 8) }}...</span>
              </el-tooltip>
            </template>
          </el-table-column>
          <el-table-column label="状态" width="80" align="center">
            <template #default="{ row }">
              <span class="status-dot" :class="deviceStatusClass(row.status, isOnline(row.lastOnlineAt))"></span>
              {{ deviceStatusText(row.status, isOnline(row.lastOnlineAt)) }}
            </template>
          </el-table-column>
          <el-table-column label="最后在线" width="150">
            <template #default="{ row }">{{ formatTime(row.lastOnlineAt) }}</template>
          </el-table-column>
        </el-table>
      </template>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox, type FormInstance } from 'element-plus'
import {
  Search, RefreshLeft, Plus, Download, CopyDocument, CircleClose, Delete,
} from '@element-plus/icons-vue'
import dayjs from 'dayjs'
import {
  generateKeys, getKeys, revokeKey, restoreKey, convertKey, getKeyDetail, exportKeys,
  batchRevokeKeys, batchDeleteKeys, deleteKey,
  type GenerateParams, type LicenseKeyItem,
} from '@/api/keys'
import { getCustomers, type Customer } from '@/api/customers'
import { getBatches, type KeyBatch } from '@/api/batches'
import {
  KEY_STATUS, KEY_TYPE, copyText, formatDuration, formatTime, formatUnixTs, formatUnbindMax,
  isOnline, deviceStatusClass, deviceStatusText,
} from '@/utils/format'

// ============ 筛选 ============
const filters = reactive<{
  search: string
  status: number | string
  type: number | string
}>({
  search: '',
  status: '',
  type: '',
})
const dateRange = ref<[string, string] | null>(null)

const loading = ref(false)
const list = ref<LicenseKeyItem[]>([])
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)

async function load() {
  loading.value = true
  try {
    const res = await getKeys({
      page: page.value,
      pageSize: pageSize.value,
      status: filters.status,
      type: filters.type,
      start: dateRange.value?.[0] || '',
      end: dateRange.value?.[1] || '',
      search: filters.search,
    })
    list.value = res.list
    total.value = res.total
  } finally {
    loading.value = false
  }
}

function handleSearch() {
  page.value = 1
  load()
}

function handleReset() {
  filters.search = ''
  filters.status = ''
  filters.type = ''
  dateRange.value = null
  handleSearch()
}

function handleSizeChange() {
  page.value = 1
  load()
}

// ============ 勾选批量操作 ============
const selection = ref<LicenseKeyItem[]>([])
const batchRevoking = ref(false)
const batchDeleting = ref(false)

function onSelectionChange(rows: LicenseKeyItem[]) {
  selection.value = rows
}

async function handleBatchRevoke() {
  if (!selection.value.length) return
  try {
    const { value } = await ElMessageBox.prompt(
      `确定批量作废所选 <b class="danger-text">${selection.value.length}</b> 个激活码吗？作废后不可激活使用。<br/>请输入作废原因：`,
      '批量作废确认',
      {
        confirmButtonText: '确认作废',
        cancelButtonText: '取消',
        type: 'error',
        inputType: 'textarea',
        inputPlaceholder: '请输入作废原因（必填）',
        inputValidator: (v: string) => (v && v.trim() ? true : '作废原因不能为空'),
        confirmButtonClass: 'el-button--danger',
        dangerouslyUseHTMLString: true,
      }
    )
    batchRevoking.value = true
    try {
      const res = await batchRevokeKeys(selection.value.map((r) => r.id), value.trim())
      ElMessage.success(`批量作废完成：成功 ${res.okCount} 个${res.failCount ? `，失败 ${res.failCount} 个` : ''}`)
      selection.value = []
      load()
    } finally {
      batchRevoking.value = false
    }
  } catch {
    // 取消或失败
  }
}

async function handleBatchDelete() {
  if (!selection.value.length) return
  try {
    await ElMessageBox.confirm(
      `确定删除所选 <b class="danger-text">${selection.value.length}</b> 个激活码吗？<br/>仅「已作废」的码可删除，删除后不再显示（客户端已作废锁定状态不受影响）。`,
      '批量删除确认',
      {
        confirmButtonText: '确认删除',
        cancelButtonText: '取消',
        type: 'error',
        confirmButtonClass: 'el-button--danger',
        dangerouslyUseHTMLString: true,
      }
    )
    batchDeleting.value = true
    try {
      const res = await batchDeleteKeys(selection.value.map((r) => r.id))
      ElMessage.success(`已删除 ${res.deleted} 个激活码`)
      selection.value = []
      load()
    } finally {
      batchDeleting.value = false
    }
  } catch {
    // 取消或失败
  }
}

async function handleDelete(row: LicenseKeyItem) {
  try {
    await ElMessageBox.confirm(
      `确定删除激活码 <b>…${row.code.slice(-6)}</b> 吗？<br/>删除后不再显示，客户端保持作废锁定状态。`,
      '删除确认',
      {
        confirmButtonText: '确认删除',
        cancelButtonText: '取消',
        type: 'error',
        confirmButtonClass: 'el-button--danger',
        dangerouslyUseHTMLString: true,
      }
    )
    await deleteKey(row.id)
    ElMessage.success('已删除')
    detailVisible.value = false
    load()
  } catch {
    // 取消或失败
  }
}

// ============ 生成激活码 ============
const generateVisible = ref(false)
const genStep = ref(0)
const generating = ref(false)
const genFormRef = ref<FormInstance>()

const durationChips = [
  { label: '30天', sec: 30 * 86400 },
  { label: '90天', sec: 90 * 86400 },
  { label: '180天', sec: 180 * 86400 },
  { label: '1年', sec: 365 * 86400 },
  { label: '2年', sec: 730 * 86400 },
  { label: '永久', sec: 0 },
]

const bindModes = [
  { label: '不绑定通用', value: 'unbound' },
  { label: '一机一码每月0次', value: 'bound-0' },
  { label: '每月1次', value: 'bound-1' },
  { label: '每月3次', value: 'bound-3' },
  { label: '不限制次数', value: 'bound--1' },
]

const genForm = reactive({
  durationSec: 30 * 86400,
  bindMode: 'bound-1',
  count: 1,
  customerId: null as string | null,
  batchId: null as string | null,
  remark: '',
})

const customDays = ref(30)
const customMode = ref(false) // true=自定义时长（与快捷时长互斥）
const genResult = reactive<{ codes: string[]; batchId: string | null }>({ codes: [], batchId: null })

const customers = ref<Customer[]>([])
const batches = ref<KeyBatch[]>([])

async function loadCustomers() {
  try {
    const res = await getCustomers({ page: 1, pageSize: 1000 })
    customers.value = res.list
  } catch {
    customers.value = []
  }
}

async function loadBatches() {
  try {
    const res = await getBatches({ page: 1, pageSize: 100 })
    batches.value = res.list
  } catch {
    batches.value = []
  }
}

function selectDuration(sec: number) {
  // 选择快捷时长：退出自定义模式
  customMode.value = false
  genForm.durationSec = sec
}

function onCustomDaysChange() {
  // 自定义时长：输入即生效，无需再点"应用"
  if (customMode.value) genForm.durationSec = customDays.value * 86400
}

function onCustomModeChange() {
  // 勾选"自定义"：立即按当前输入生效，并解除快捷选中
  if (customMode.value) genForm.durationSec = customDays.value * 86400
}

function resetGenerateForm() {
  genForm.durationSec = 30 * 86400
  genForm.bindMode = 'bound-1'
  genForm.count = 1
  genForm.customerId = null
  genForm.batchId = null
  genForm.remark = ''
  customDays.value = 30
  customMode.value = false
}

function openGenerate() {
  generateVisible.value = true
  genStep.value = 0
  resetGenerateForm()
  genResult.codes = []
  genResult.batchId = null
}

async function submitGenerate() {
  if (genForm.count < 1 || genForm.count > 500) {
    ElMessage.warning('数量需在 1 ~ 500 之间')
    return
  }
  const bindMode = genForm.bindMode
  // 批次：下拉选中已有批次传 batchId；allow-create 输入的新名称传 batchName
  const picked = genForm.batchId
  const isExistingBatch = picked !== null && batches.value.some((b) => String(b.id) === String(picked))
  const params: GenerateParams = {
    durationSec: genForm.durationSec,
    bound: bindMode !== 'unbound',
    unbindMax: bindMode === 'unbound' ? 0 : Number(bindMode.replace('bound-', '')),
    count: genForm.count,
    customerId: genForm.customerId || null,
    batchId: isExistingBatch ? String(picked) : null,
    batchName: !isExistingBatch && picked ? String(picked) : null,
    remark: genForm.remark || null,
  }
  generating.value = true
  try {
    const res = await generateKeys(params)
    genResult.codes = res.codes
    genResult.batchId = res.batchId
    genStep.value = 1
    loadBatches()
    ElMessage.success(`成功生成 ${res.codes.length} 个激活码`)
  } catch {
    // 拦截器已提示
  } finally {
    generating.value = false
  }
}

function resetGenerate() {
  genStep.value = 0
  resetGenerateForm()
  genResult.codes = []
  genResult.batchId = null
  load()
}

async function copyAllCodes() {
  await copyText(genResult.codes.join('\n'), `已复制全部 ${genResult.codes.length} 个激活码`)
}

// ============ 行操作 ============
async function handleRevoke(row: LicenseKeyItem) {
  const tail = row.code.slice(-6)
  try {
    const { value } = await ElMessageBox.prompt(
      `确定要作废激活码 <b class="danger-text">…${tail}</b> 吗？作废后不可激活使用。<br/>请输入作废原因：`,
      '作废确认',
      {
        confirmButtonText: '确认作废',
        cancelButtonText: '取消',
        type: 'error',
        inputType: 'textarea',
        inputPlaceholder: '请输入作废原因（必填）',
        inputValidator: (v: string) => (v && v.trim() ? true : '作废原因不能为空'),
        confirmButtonClass: 'el-button--danger',
        dangerouslyUseHTMLString: true,
      }
    )
    await revokeKey(row.id, value.trim())
    ElMessage.success('已作废')
    load()
  } catch {
    // 取消或失败
  }
}

async function handleRestore(row: LicenseKeyItem) {
  try {
    await ElMessageBox.confirm(`确定恢复激活码 <b>…${row.code.slice(-6)}</b> 吗？`, '恢复确认', {
      confirmButtonText: '确认恢复',
      cancelButtonText: '取消',
      type: 'warning',
      dangerouslyUseHTMLString: true,
    })
    await restoreKey(row.id)
    ElMessage.success('已恢复')
    load()
  } catch {
    // 取消或失败
  }
}

async function handleConvert(row: LicenseKeyItem) {
  try {
    await ElMessageBox.confirm(
      `确定为已用激活码 <b>…${row.code.slice(-6)}</b> 签发换机码吗？<br/>原码将标记为「已换机」。`,
      '签发换机码',
      {
        confirmButtonText: '确认签发',
        cancelButtonText: '取消',
        type: 'warning',
        dangerouslyUseHTMLString: true,
      }
    )
    const res = await convertKey(row.id)
    await ElMessageBox.alert(
      `换机码生成成功：<br/><b class="code-font">${res.code}</b><br/>到期时间：${formatUnixTs(res.expireAt)}`,
      '换机码已签发',
      {
        confirmButtonText: '复制并关闭',
        dangerouslyUseHTMLString: true,
      }
    )
    copyText(res.code, '换机码已复制')
    load()
  } catch {
    // 取消或失败
  }
}

// ============ 导出 ============
const exporting = ref(false)

async function handleExport() {
  exporting.value = true
  try {
    const blob = await exportKeys({
      status: filters.status || '',
      type: filters.type || '',
      start: dateRange.value?.[0] || '',
      end: dateRange.value?.[1] || '',
      search: filters.search,
    })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `激活码_${dayjs().format('YYYYMMDD_HHmmss')}.csv`
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    URL.revokeObjectURL(url)
    ElMessage.success('导出成功')
  } catch {
    // 拦截器已提示
  } finally {
    exporting.value = false
  }
}

// ============ 详情 ============
const detailVisible = ref(false)
const detail = ref<{ key: LicenseKeyItem; devices: any[]; unbindCount: number } | null>(null)

async function openDetail(row: LicenseKeyItem) {
  detailVisible.value = true
  detail.value = null
  try {
    detail.value = await getKeyDetail(row.id)
  } catch {
    detailVisible.value = false
  }
}

onMounted(() => {
  load()
  loadCustomers()
  loadBatches()
})
</script>

<style scoped lang="scss">
.toolbar-right {
  margin-left: auto;
  display: flex;
  gap: 10px;
}

.code-cell {
  display: flex;
  align-items: center;
  gap: 4px;
  min-width: 0;

  .code-font {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
}

.text-secondary {
  color: var(--text-secondary);
}

.step-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 12px;
  font-weight: 600;
  color: var(--text-main);
}

.chip-wrap {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 10px;
}

.custom-duration {
  display: flex;
  align-items: center;
  gap: 10px;
}

.code-line {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 10px;
  border-radius: 6px;
  background: #f7f9fc;
  margin-bottom: 6px;
}

.detail-code {
  background: #f7f9fc;
  border-radius: 6px;
  padding: 12px;
  word-break: break-all;
}

.detail-section-title {
  margin: 20px 0 10px;
  font-weight: 600;
  color: var(--text-main);
}

:deep(.danger-text) {
  color: var(--danger-color);
}
</style>
