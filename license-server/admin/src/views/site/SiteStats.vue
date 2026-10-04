<template>
  <div class="site-panel">
    <div class="sub-toolbar">
      <el-select v-model="days" style="width: 130px" @change="load">
        <el-option label="近 7 天" :value="7" />
        <el-option label="近 30 天" :value="30" />
        <el-option label="近 90 天" :value="90" />
      </el-select>
      <el-button :icon="Refresh" @click="load">刷新</el-button>
      <div class="sub-toolbar-right">
        <el-button :icon="RefreshRight" :loading="syncing" @click="handleSyncRelease">同步版本信息</el-button>
      </div>
    </div>

    <el-row :gutter="16" class="stat-cards">
      <el-col :xs="12" :sm="6">
        <div class="stat-card">
          <div class="stat-num">{{ totals.pv }}</div>
          <div class="stat-lbl">页面访问 (PV)</div>
        </div>
      </el-col>
      <el-col :xs="12" :sm="6">
        <div class="stat-card">
          <div class="stat-num">{{ totals.uv }}</div>
          <div class="stat-lbl">独立访客 (UV)</div>
        </div>
      </el-col>
      <el-col :xs="12" :sm="6">
        <div class="stat-card">
          <div class="stat-num">{{ totals.downloads }}</div>
          <div class="stat-lbl">下载点击</div>
        </div>
      </el-col>
      <el-col :xs="12" :sm="6">
        <div class="stat-card">
          <div class="stat-num">{{ totals.buyClicks }}</div>
          <div class="stat-lbl">购买点击</div>
        </div>
      </el-col>
    </el-row>

    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="日期" width="140" prop="statDate" />
      <el-table-column label="页面访问 (PV)" prop="pv" align="center" />
      <el-table-column label="独立访客 (UV)" prop="uv" align="center" />
      <el-table-column label="下载点击" prop="downloads" align="center" />
      <el-table-column label="购买点击" prop="buyClicks" align="center" />
    </el-table>
    <el-empty v-if="!list.length && !loading" description="暂无统计数据" />
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { Refresh, RefreshRight } from '@element-plus/icons-vue'
import { getSiteStats, syncRelease, type SiteStatsDaily } from '@/api/site'

const days = ref(30)
const list = ref<SiteStatsDaily[]>([])
const totals = ref({ pv: 0, uv: 0, downloads: 0, buyClicks: 0 })
const loading = ref(false)
const syncing = ref(false)

async function load() {
  loading.value = true
  try {
    const res = await getSiteStats(days.value)
    list.value = res.list.slice().reverse()
    totals.value = res.totals
  } finally {
    loading.value = false
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

onMounted(load)
</script>

<style scoped lang="scss">
.sub-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 14px;

  .sub-toolbar-right {
    margin-left: auto;
  }
}

.stat-cards {
  margin-bottom: 18px;
}

.stat-card {
  background: #f7f9fc;
  border-radius: 8px;
  padding: 16px;
  text-align: center;

  .stat-num {
    font-size: 26px;
    font-weight: 700;
    color: var(--primary-color);
  }

  .stat-lbl {
    margin-top: 4px;
    font-size: 12px;
    color: var(--text-secondary);
  }
}
</style>
