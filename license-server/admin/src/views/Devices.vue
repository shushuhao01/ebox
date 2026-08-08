<template>
  <div class="page-container">
    <!-- 工具栏 -->
    <div class="toolbar-card">
      <el-radio-group v-model="filters.mode" @change="handleSearch">
        <el-radio-button value="all">全部</el-radio-button>
        <el-radio-button value="online">在线</el-radio-button>
        <el-radio-button value="offline">离线</el-radio-button>
        <el-radio-button value="kicked">被踢</el-radio-button>
        <el-radio-button value="unbound">已解绑</el-radio-button>
      </el-radio-group>
      <el-input
        v-model="filters.search"
        placeholder="搜索指纹 / 激活码 / IP"
        clearable
        style="width: 240px"
        :prefix-icon="Search"
        @keyup.enter="handleSearch"
        @clear="handleSearch"
      />
      <div class="toolbar-right">
        <span class="legend"><span class="status-dot online"></span>在线</span>
        <span class="legend"><span class="status-dot offline"></span>离线</span>
        <span class="legend"><span class="status-dot kicked"></span>被踢</span>
        <span class="legend"><span class="status-dot unbound"></span>已解绑</span>
      </div>
    </div>

    <!-- 表格 -->
    <div class="table-card">
      <el-table :data="list" v-loading="loading" stripe>
        <el-table-column label="设备ID" prop="id" width="90" />
        <el-table-column label="设备指纹" min-width="160">
          <template #default="{ row }">
            <el-tooltip :content="row.machineFp" placement="top">
              <span class="code-font">{{ row.machineFp.slice(0, 8) }}...</span>
            </el-tooltip>
          </template>
        </el-table-column>
        <el-table-column label="激活码" min-width="240">
          <template #default="{ row }">
            <span v-if="codeMap[row.keyId]" class="code-font">{{ codeMap[row.keyId] }}</span>
            <span v-else class="text-secondary">#{{ row.keyId }}</span>
          </template>
        </el-table-column>
        <el-table-column label="状态" width="90" align="center">
          <template #default="{ row }">
            <span class="status-dot" :class="deviceStatusClass(row.status, isOnline(row.lastOnlineAt))"></span>
            {{ deviceStatusText(row.status, isOnline(row.lastOnlineAt)) }}
          </template>
        </el-table-column>
        <el-table-column label="最后在线" width="160">
          <template #default="{ row }">{{ formatTime(row.lastOnlineAt) }}</template>
        </el-table-column>
        <el-table-column label="IP" width="120" show-overflow-tooltip>
          <template #default="{ row }">{{ formatIp(row.lastIp) }}</template>
        </el-table-column>
        <el-table-column label="系统版本" min-width="120" show-overflow-tooltip>
          <template #default="{ row }">{{ row.osInfo || '-' }}</template>
        </el-table-column>
        <el-table-column label="App版本" prop="appVersion" width="90" align="center">
          <template #default="{ row }">{{ row.appVersion || '-' }}</template>
        </el-table-column>
        <el-table-column label="操作" width="110" fixed="right" align="center">
          <template #default="{ row }">
            <el-button
              v-if="row.status !== 0"
              link
              type="danger"
              size="small"
              @click="handleKick(row)"
            >踢下线</el-button>
            <span v-else class="text-secondary">-</span>
          </template>
        </el-table-column>
        <template #empty>
          <el-empty description="暂无设备" :image-size="80" />
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
import { Search } from '@element-plus/icons-vue'
import { getDevices, kickDevice, type Device, type DeviceMode } from '@/api/devices'
import { getKeyCodeMap } from '@/api/keys'
import { formatTime, formatIp, isOnline, deviceStatusClass, deviceStatusText } from '@/utils/format'

const filters = reactive<{ mode: DeviceMode | string; search: string }>({ mode: 'all', search: '' })
const list = ref<Device[]>([])
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const loading = ref(false)
const codeMap = ref<Record<string, string>>({})

async function load() {
  loading.value = true
  try {
    const res = await getDevices({
      page: page.value,
      pageSize: pageSize.value,
      mode: filters.mode as DeviceMode,
      search: filters.search,
    })
    list.value = res.list
    total.value = res.total
    // 解析激活码
    codeMap.value = await getKeyCodeMap(res.list.map((d) => d.keyId))
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

async function handleKick(row: Device) {
  try {
    await ElMessageBox.confirm(
      `确定将设备 <b class="code-font">${row.machineFp.slice(0, 8)}...</b> 踢下线吗？<br/>该设备将立即失去授权，需重新激活。`,
      '踢下线确认',
      {
        confirmButtonText: '确认踢下线',
        cancelButtonText: '取消',
        type: 'error',
        confirmButtonClass: 'el-button--danger',
        dangerouslyUseHTMLString: true,
      }
    )
    await kickDevice(row.id)
    ElMessage.success('已踢下线')
    load()
  } catch {
    // 取消或失败
  }
}

onMounted(load)
</script>

<style scoped lang="scss">
.toolbar-right {
  margin-left: auto;
  display: flex;
  align-items: center;
  gap: 14px;

  .legend {
    font-size: 12px;
    color: var(--text-secondary);
    display: flex;
    align-items: center;
  }
}

.text-secondary {
  color: var(--text-secondary);
}
</style>
