<template>
  <div class="page-container">
    <!-- 工具栏 -->
    <div class="toolbar-card">
      <el-select v-model="filters.action" placeholder="动作" clearable filterable style="width: 180px" @change="handleSearch">
        <el-option v-for="a in ACTION_OPTIONS" :key="a" :label="a" :value="a" />
      </el-select>
      <el-input
        v-model="filters.userId"
        placeholder="操作人ID"
        clearable
        style="width: 150px"
        :prefix-icon="Search"
        @keyup.enter="handleSearch"
        @clear="handleSearch"
      />
      <div class="toolbar-right">
        <el-button :icon="Refresh" @click="handleSearch">刷新</el-button>
      </div>
    </div>

    <!-- 表格 -->
    <div class="table-card">
      <el-table :data="list" v-loading="loading" stripe>
        <el-table-column label="时间" width="165">
          <template #default="{ row }">{{ formatTime(row.createdAt) }}</template>
        </el-table-column>
        <el-table-column label="用户ID" prop="userId" width="100" align="center" />
        <el-table-column label="动作" width="130" align="center">
          <template #default="{ row }">
            <el-tag size="small" effect="plain">{{ row.action }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="目标" min-width="200" show-overflow-tooltip>
          <template #default="{ row }">{{ row.target || '-' }}</template>
        </el-table-column>
        <el-table-column label="详情" min-width="220" show-overflow-tooltip>
          <template #default="{ row }">{{ row.detail || '-' }}</template>
        </el-table-column>
        <el-table-column label="IP" width="120" show-overflow-tooltip>
          <template #default="{ row }">{{ formatIp(row.ip) }}</template>
        </el-table-column>
        <template #empty>
          <el-empty description="暂无操作日志" :image-size="80" />
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
import { Search, Refresh } from '@element-plus/icons-vue'
import { getOperationLogs, type OperationLog } from '@/api/logs'
import { formatTime, formatIp } from '@/utils/format'

const ACTION_OPTIONS = [
  '生成激活码',
  '作废激活码',
  '恢复激活码',
  '签发换机码',
  '踢下线',
  '修改系统设置',
  '新建客户',
  '编辑客户',
  '停用客户',
  '新建管理员',
  '修改密码',
  '新建批次',
]

const filters = reactive<{ action: string; userId: string }>({ action: '', userId: '' })
const list = ref<OperationLog[]>([])
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const loading = ref(false)

async function load() {
  loading.value = true
  try {
    const res = await getOperationLogs({
      page: page.value,
      pageSize: pageSize.value,
      action: filters.action,
      userId: filters.userId,
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

function handleSizeChange() {
  page.value = 1
  load()
}

onMounted(load)
</script>

<style scoped lang="scss">
.toolbar-right {
  margin-left: auto;
}
</style>
