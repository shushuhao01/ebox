<template>
  <div class="page-container">
    <!-- 工具栏 -->
    <div class="toolbar-card">
      <el-select v-model="filters.action" placeholder="动作" clearable style="width: 130px" @change="handleSearch">
        <el-option v-for="(v, k) in HB_ACTION" :key="k" :label="v.label" :value="Number(k)" />
      </el-select>
      <el-input
        v-model="filters.code"
        placeholder="搜索激活码"
        clearable
        style="width: 240px"
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
        <el-table-column label="激活码" min-width="250">
          <template #default="{ row }">
            <span v-if="codeMap[row.keyId]" class="code-font">{{ codeMap[row.keyId] }}</span>
            <span v-else class="text-secondary">#{{ row.keyId }}</span>
          </template>
        </el-table-column>
        <el-table-column label="设备ID" width="110" align="center">
          <template #default="{ row }">{{ row.deviceId || '-' }}</template>
        </el-table-column>
        <el-table-column label="IP" width="120" show-overflow-tooltip>
          <template #default="{ row }">{{ formatIp(row.ip) }}</template>
        </el-table-column>
        <el-table-column label="动作" width="80" align="center">
          <template #default="{ row }">
            <el-tag :type="HB_ACTION[row.action]?.type || 'info'" size="small">
              {{ HB_ACTION[row.action]?.label || row.action }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="App版本" prop="appVersion" width="90" align="center">
          <template #default="{ row }">{{ row.appVersion || '-' }}</template>
        </el-table-column>
        <el-table-column label="详情" min-width="200" show-overflow-tooltip>
          <template #default="{ row }">{{ formatHbDetail(row.detail) }}</template>
        </el-table-column>
        <template #empty>
          <el-empty description="暂无心跳日志" :image-size="80" />
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
import { getHeartbeats, type Heartbeat } from '@/api/logs'
import { getKeyCodeMap } from '@/api/keys'
import { formatTime, formatIp, formatHbDetail, HB_ACTION } from '@/utils/format'

const filters = reactive<{ action: number | string; code: string }>({ action: '', code: '' })
const list = ref<Heartbeat[]>([])
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const loading = ref(false)
const codeMap = ref<Record<string, string>>({})

async function load() {
  loading.value = true
  try {
    const res = await getHeartbeats({
      page: page.value,
      pageSize: pageSize.value,
      action: filters.action,
      code: filters.code,
    })
    list.value = res.list
    total.value = res.total
    codeMap.value = await getKeyCodeMap(res.list.map((h) => h.keyId))
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

.text-secondary {
  color: var(--text-secondary);
}
</style>
