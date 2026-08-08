<template>
  <div class="page-container">
    <!-- 统计卡 -->
    <el-row :gutter="16">
      <el-col :xs="24" :sm="12" :md="6">
        <el-card shadow="never" class="stat-card">
          <div class="stat-inner">
            <div class="stat-icon danger"><el-icon :size="22"><CircleClose /></el-icon></div>
            <div>
              <div class="stat-value">{{ total }}</div>
              <div class="stat-label">作废记录总数</div>
            </div>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="24" :sm="12" :md="6">
        <el-card shadow="never" class="stat-card">
          <div class="stat-inner">
            <div class="stat-icon month"><el-icon :size="22"><Calendar /></el-icon></div>
            <div>
              <div class="stat-value">{{ thisMonthCount }}</div>
              <div class="stat-label">本月作废数</div>
            </div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <!-- 表格 -->
    <div class="table-card">
      <el-table :data="list" v-loading="loading" stripe>
        <el-table-column label="激活码" min-width="250">
          <template #default="{ row }">
            <span v-if="codeMap[row.keyId]" class="code-font">{{ codeMap[row.keyId] }}</span>
            <span v-else class="text-secondary">#{{ row.keyId }}</span>
          </template>
        </el-table-column>
        <el-table-column label="作废时间" width="165">
          <template #default="{ row }">{{ formatTime(row.createdAt) }}</template>
        </el-table-column>
        <el-table-column label="操作人ID" prop="operatorId" width="110" align="center" />
        <el-table-column label="原因" min-width="200" show-overflow-tooltip>
          <template #default="{ row }">{{ row.reason || '-' }}</template>
        </el-table-column>
        <template #empty>
          <el-empty description="暂无作废记录" :image-size="80" />
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
import { computed, onMounted, ref } from 'vue'
import dayjs from 'dayjs'
import { CircleClose, Calendar } from '@element-plus/icons-vue'
import { getRevokeLogs, type RevokeLog } from '@/api/logs'
import { getKeyCodeMap } from '@/api/keys'
import { formatTime } from '@/utils/format'

const list = ref<RevokeLog[]>([])
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const loading = ref(false)
const codeMap = ref<Record<string, string>>({})

// 本月作废数：通过本页列表过滤（覆盖当前页），辅助展示
const thisMonthCount = computed(() => {
  const monthPrefix = dayjs().format('YYYY-MM')
  return list.value.filter((l) => l.createdAt && l.createdAt.startsWith(monthPrefix)).length
})

async function load() {
  loading.value = true
  try {
    const res = await getRevokeLogs({ page: page.value, pageSize: pageSize.value })
    list.value = res.list
    total.value = res.total
    codeMap.value = await getKeyCodeMap(res.list.map((l) => l.keyId))
  } finally {
    loading.value = false
  }
}

onMounted(load)
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

  &.danger {
    background: rgba(239, 68, 68, 0.12);
    color: var(--danger-color);
  }
  &.month {
    background: rgba(245, 158, 11, 0.12);
    color: var(--warning-color);
  }
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

.text-secondary {
  color: var(--text-secondary);
}
</style>
