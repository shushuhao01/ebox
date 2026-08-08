<template>
  <div class="page-container">
    <!-- 工具栏 -->
    <div class="toolbar-card">
      <el-date-picker
        v-model="month"
        type="month"
        placeholder="选择月份"
        value-format="YYYYMM"
        style="width: 140px"
        @change="handleSearch"
      />
      <el-tag type="warning" effect="light" size="large">
        <el-icon style="vertical-align: -2px"><Refresh /></el-icon>
        &nbsp;{{ month }} 月解绑次数：{{ total }}
      </el-tag>
    </div>

    <!-- 表格 -->
    <div class="table-card">
      <el-table :data="list" v-loading="loading" stripe>
        <el-table-column label="时间" width="165">
          <template #default="{ row }">{{ formatTime(row.createdAt) }}</template>
        </el-table-column>
        <el-table-column label="旧激活码" min-width="250">
          <template #default="{ row }">
            <span v-if="codeMap[row.keyId]" class="code-font">{{ codeMap[row.keyId] }}</span>
            <span v-else class="text-secondary">#{{ row.keyId }}</span>
          </template>
        </el-table-column>
        <el-table-column label="换机码" min-width="250">
          <template #default="{ row }">
            <span v-if="row.newKeyId && codeMap[row.newKeyId]" class="code-font">{{ codeMap[row.newKeyId] }}</span>
            <span v-else-if="row.newKeyId" class="text-secondary">#{{ row.newKeyId }}</span>
            <span v-else class="text-secondary">-</span>
          </template>
        </el-table-column>
        <el-table-column label="设备ID" prop="deviceId" width="110" align="center" />
        <el-table-column label="月份" prop="month" width="90" align="center" />
        <template #empty>
          <el-empty description="暂无换机记录" :image-size="80" />
        </template>
      </el-table>

      <div class="pagination-wrap">
        <el-pagination
          v-model:current-page="page"
          v-model:page-size="pageSize"
          :total="total"
          layout="total, prev, pager, next, jumper"
          @current-change="load"
        />
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import dayjs from 'dayjs'
import { Refresh } from '@element-plus/icons-vue'
import { getUnbindLogs, type UnbindLog } from '@/api/logs'
import { getKeyCodeMap } from '@/api/keys'
import { formatTime } from '@/utils/format'

const month = ref(dayjs().format('YYYYMM'))
const list = ref<UnbindLog[]>([])
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const loading = ref(false)
const codeMap = ref<Record<string, string>>({})

async function load() {
  loading.value = true
  try {
    const res = await getUnbindLogs({ page: page.value, pageSize: pageSize.value, month: month.value })
    list.value = res.list
    total.value = res.total
    const ids: Array<string | null> = []
    res.list.forEach((l) => {
      ids.push(l.keyId)
      if (l.newKeyId) ids.push(l.newKeyId)
    })
    codeMap.value = await getKeyCodeMap(ids)
  } finally {
    loading.value = false
  }
}

function handleSearch() {
  page.value = 1
  load()
}

onMounted(load)
</script>

<style scoped lang="scss">
.text-secondary {
  color: var(--text-secondary);
}
</style>
