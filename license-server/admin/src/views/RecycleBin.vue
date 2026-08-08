<template>
  <div class="page-container">
    <!-- 工具栏 -->
    <div class="toolbar-card">
      <el-input
        v-model="filters.search"
        placeholder="搜索码 / 指纹"
        clearable
        style="width: 240px"
        :prefix-icon="Search"
        @keyup.enter="handleSearch"
        @clear="handleSearch"
      />
      <el-button :icon="RefreshLeft" @click="handleReset">重置</el-button>

      <div class="toolbar-right">
        <template v-if="selection.length">
          <el-tag type="warning" effect="plain">已选 {{ selection.length }} 条</el-tag>
          <el-button type="success" :icon="RefreshLeft" :loading="restoring" @click="handleBatchRestore">
            批量恢复
          </el-button>
          <el-button :icon="Close" @click="selection = []">取消选择</el-button>
        </template>
        <el-button :icon="RefreshLeft" @click="load">刷新</el-button>
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
        <el-table-column label="状态" width="90" align="center">
          <template #default="{ row }">
            <el-tag :type="KEY_STATUS[row.status]?.type || 'info'" size="small">
              {{ KEY_STATUS[row.status]?.label || row.status }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="作废时间" width="165">
          <template #default="{ row }">{{ formatTime(row.revokedAt) }}</template>
        </el-table-column>
        <el-table-column label="作废原因" min-width="180" show-overflow-tooltip>
          <template #default="{ row }">{{ row.revokedReason || '-' }}</template>
        </el-table-column>
        <el-table-column label="创建时间" width="165">
          <template #default="{ row }">{{ formatTime(row.createdAt) }}</template>
        </el-table-column>
        <el-table-column label="操作" width="90" fixed="right">
          <template #default="{ row }">
            <el-button link type="success" size="small" @click="handleRestore(row)">恢复</el-button>
          </template>
        </el-table-column>
        <template #empty>
          <el-empty description="回收站为空" :image-size="80" />
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
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Search, RefreshLeft, CopyDocument, Close } from '@element-plus/icons-vue'
import { getKeys, batchRestoreKeys, type LicenseKeyItem } from '@/api/keys'
import { KEY_STATUS, KEY_TYPE, copyText, formatDuration, formatTime, formatUnixTs } from '@/utils/format'

// ============ 筛选 ============
const filters = reactive<{ search: string }>({ search: '' })

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
      status: 5, // 仅回收站（已删除）
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
  handleSearch()
}

function handleSizeChange() {
  page.value = 1
  load()
}

// ============ 恢复 ============
const selection = ref<LicenseKeyItem[]>([])
const restoring = ref(false)

function onSelectionChange(rows: LicenseKeyItem[]) {
  selection.value = rows
}

async function handleBatchRestore() {
  if (!selection.value.length) return
  await doRestore(selection.value.map((r) => r.id), selection.value.length)
}

async function handleRestore(row: LicenseKeyItem) {
  await doRestore([row.id], 1)
}

async function doRestore(ids: string[], count: number) {
  try {
    await ElMessageBox.confirm(
      `确定恢复所选 <b class="danger-text">${count}</b> 个激活码吗？<br/>恢复后将回到「激活码管理」（作废状态），如需使用可再点「恢复」。`,
      '恢复确认',
      {
        confirmButtonText: '确认恢复',
        cancelButtonText: '取消',
        type: 'warning',
        confirmButtonClass: 'el-button--success',
        dangerouslyUseHTMLString: true,
      }
    )
    restoring.value = true
    try {
      const res = await batchRestoreKeys(ids)
      ElMessage.success(`已恢复 ${res.restored} 个激活码`)
      selection.value = []
      load()
    } finally {
      restoring.value = false
    }
  } catch {
    // 取消或失败
  }
}

onMounted(load)
</script>

<style scoped lang="scss">
.code-cell {
  display: flex;
  align-items: center;
  gap: 6px;
}
</style>
