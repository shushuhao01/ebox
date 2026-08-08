<template>
  <div class="page-container">
    <!-- 工具栏 -->
    <div class="toolbar-card">
      <el-input
        v-model="filters.search"
        placeholder="搜索名称 / 电话 / 微信 / QQ"
        clearable
        style="width: 240px"
        :prefix-icon="Search"
        @keyup.enter="handleSearch"
        @clear="handleSearch"
      />
      <el-select v-model="filters.status" placeholder="状态" clearable style="width: 120px" @change="handleSearch">
        <el-option label="启用" :value="1" />
        <el-option label="停用" :value="0" />
      </el-select>
      <div class="toolbar-right">
        <el-button type="primary" :icon="Plus" @click="openCreate">新建客户</el-button>
      </div>
    </div>

    <!-- 客户卡片网格 -->
    <div v-loading="loading">
      <el-empty v-if="!list.length && !loading" description="暂无客户" />
      <el-row :gutter="16">
        <el-col v-for="c in list" :key="c.id" :xs="24" :sm="12" :md="8" :lg="6">
          <el-card shadow="hover" class="customer-card" @click="openDetail(c)">
            <div class="customer-head">
              <div class="customer-avatar" :style="{ background: avatarColor(c.name) }">{{ c.name.slice(0, 1) }}</div>
              <div class="customer-info">
                <div class="customer-name">
                  {{ c.name }}
                  <el-tag v-if="c.status === 0" type="danger" size="small" effect="plain">已停用</el-tag>
                </div>
                <div class="customer-contact">
                  <span v-if="c.phone"><el-icon><Iphone /></el-icon>{{ c.phone }}</span>
                  <span v-else-if="c.wechat"><el-icon><ChatDotRound /></el-icon>{{ c.wechat }}</span>
                  <span v-else-if="c.qq">QQ: {{ c.qq }}</span>
                  <span v-else class="muted">暂无联系方式</span>
                </div>
              </div>
            </div>
            <div class="customer-stats">
              <div class="stat">
                <div class="num">{{ c.keyCount ?? 0 }}</div>
                <div class="lbl">名下码数</div>
              </div>
              <el-divider direction="vertical" />
              <div class="stat">
                <div class="num">{{ c.deviceCount ?? 0 }}</div>
                <div class="lbl">在用设备</div>
              </div>
              <el-divider direction="vertical" />
              <div class="stat">
                <div class="num date">{{ formatTime(c.createdAt, 'YYYY-MM-DD') }}</div>
                <div class="lbl">创建时间</div>
              </div>
            </div>
            <div class="customer-actions" @click.stop>
              <el-button size="small" :icon="Edit" @click="openEdit(c)">编辑</el-button>
              <el-button
                v-if="c.status === 1"
                size="small"
                type="danger"
                plain
                :icon="CircleClose"
                @click="handleDisable(c)"
              >停用</el-button>
            </div>
          </el-card>
        </el-col>
      </el-row>

      <div class="pagination-wrap">
        <el-pagination
          v-model:current-page="page"
          v-model:page-size="pageSize"
          :total="total"
          :page-sizes="[12, 24, 48]"
          layout="total, sizes, prev, pager, next"
          @current-change="load"
          @size-change="handleSizeChange"
        />
      </div>
    </div>

    <!-- 新建 / 编辑 -->
    <el-dialog v-model="formVisible" :title="editing ? '编辑客户' : '新建客户'" width="520px">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="80px">
        <el-form-item label="客户名称" prop="name">
          <el-input v-model="form.name" placeholder="请输入客户名称" maxlength="64" />
        </el-form-item>
        <el-form-item label="电话">
          <el-input v-model="form.phone" placeholder="电话（可选）" maxlength="32" />
        </el-form-item>
        <el-form-item label="微信">
          <el-input v-model="form.wechat" placeholder="微信（可选）" maxlength="64" />
        </el-form-item>
        <el-form-item label="QQ">
          <el-input v-model="form.qq" placeholder="QQ（可选）" maxlength="32" />
        </el-form-item>
        <el-form-item label="来源">
          <el-input v-model="form.source" placeholder="来源渠道（可选）" maxlength="64" />
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.remark" type="textarea" :rows="3" placeholder="备注（可选）" maxlength="255" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="formVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="submit">确定</el-button>
      </template>
    </el-dialog>

    <!-- 客户详情抽屉 -->
    <el-drawer v-model="detailVisible" :title="`客户详情：${detail?.name || ''}`" size="640px">
      <template v-if="detail">
        <!-- 客户信息卡 -->
        <div class="detail-info-card">
          <div class="customer-avatar" :style="{ background: avatarColor(detail.name) }">{{ detail.name.slice(0, 1) }}</div>
          <div class="detail-info">
            <div class="detail-name">
              {{ detail.name }}
              <el-tag v-if="detail.status === 0" type="danger" size="small" effect="plain">已停用</el-tag>
            </div>
            <div class="detail-contact">
              <span v-if="detail.phone">电话：{{ detail.phone }}</span>
              <span v-if="detail.wechat">微信：{{ detail.wechat }}</span>
              <span v-if="detail.qq">QQ：{{ detail.qq }}</span>
              <span v-if="detail.source">来源：{{ detail.source }}</span>
              <span>创建：{{ formatTime(detail.createdAt) }}</span>
            </div>
            <div v-if="detail.remark" class="detail-remark">备注：{{ detail.remark }}</div>
          </div>
        </div>

        <!-- 名下码 -->
        <div class="detail-title">
          名下激活码（{{ keys.length }}）
          <el-tag size="small" type="info" effect="plain">共 {{ keys.filter((k) => k.status === 1).length }} 个已用</el-tag>
        </div>
        <el-table :data="keys" size="small" stripe max-height="260" v-loading="keysLoading">
          <el-table-column label="激活码" min-width="220">
            <template #default="{ row }">
              <div class="code-cell">
                <span class="code-font">{{ row.code }}</span>
                <el-button link type="primary" :icon="CopyDocument" size="small" @click="copyText(row.code)" />
              </div>
            </template>
          </el-table-column>
          <el-table-column label="状态" width="80" align="center">
            <template #default="{ row }">
              <el-tag :type="KEY_STATUS[row.status]?.type || 'info'" size="small">
                {{ KEY_STATUS[row.status]?.label || row.status }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column label="时长" width="80" align="center">
            <template #default="{ row }">{{ formatDuration(row.durationSec) }}</template>
          </el-table-column>
          <el-table-column label="创建时间" width="140">
            <template #default="{ row }">{{ formatTime(row.createdAt) }}</template>
          </el-table-column>
        </el-table>

        <!-- 名下设备 -->
        <div class="detail-title">名下设备（{{ devices.length }}）</div>
        <el-table :data="devices" size="small" stripe max-height="260" v-loading="devicesLoading">
          <el-table-column label="设备ID" prop="id" width="90" />
          <el-table-column label="指纹" min-width="110">
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
          <el-table-column label="最后在线" width="145">
            <template #default="{ row }">{{ formatTime(row.lastOnlineAt) }}</template>
          </el-table-column>
          <el-table-column label="IP" prop="lastIp" width="110" show-overflow-tooltip />
        </el-table>
      </template>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox, type FormInstance, type FormRules } from 'element-plus'
import {
  Search, Plus, Edit, CircleClose, CopyDocument, Iphone, ChatDotRound,
} from '@element-plus/icons-vue'
import {
  getCustomers, createCustomer, updateCustomer, deleteCustomer, getCustomerKeys,
  type Customer, type CustomerForm,
} from '@/api/customers'
import { getKeyDetail, type LicenseKeyItem, type KeyDevice } from '@/api/keys'
import { KEY_STATUS, copyText, formatTime, formatDuration, isOnline, deviceStatusClass, deviceStatusText } from '@/utils/format'

const list = ref<Customer[]>([])
const page = ref(1)
const pageSize = ref(12)
const total = ref(0)
const loading = ref(false)
const filters = reactive({ search: '', status: '' })

async function load() {
  loading.value = true
  try {
    const res = await getCustomers({
      page: page.value,
      pageSize: pageSize.value,
      search: filters.search,
      status: filters.status,
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

const AVATAR_COLORS = ['#3A7AFE', '#22C55E', '#F59E0B', '#8B5CF6', '#06B6D4', '#EC4899', '#EF4444']

function avatarColor(name: string): string {
  let hash = 0
  for (let i = 0; i < name.length; i++) hash = (hash * 31 + name.charCodeAt(i)) >>> 0
  return AVATAR_COLORS[hash % AVATAR_COLORS.length]
}

// ============ 新建 / 编辑 ============
const formVisible = ref(false)
const editing = ref<Customer | null>(null)
const saving = ref(false)
const formRef = ref<FormInstance>()
const form = reactive<CustomerForm>({ name: '', phone: '', wechat: '', qq: '', source: '', remark: '' })
const rules: FormRules = {
  name: [{ required: true, message: '请输入客户名称', trigger: 'blur' }],
}

function openCreate() {
  editing.value = null
  form.name = ''
  form.phone = ''
  form.wechat = ''
  form.qq = ''
  form.source = ''
  form.remark = ''
  formVisible.value = true
}

function openEdit(c: Customer) {
  editing.value = c
  form.name = c.name
  form.phone = c.phone || ''
  form.wechat = c.wechat || ''
  form.qq = c.qq || ''
  form.source = c.source || ''
  form.remark = c.remark || ''
  formVisible.value = true
}

async function submit() {
  if (!formRef.value) return
  const valid = await formRef.value.validate().catch(() => false)
  if (!valid) return
  saving.value = true
  try {
    const payload: CustomerForm = {
      name: form.name,
      phone: form.phone || undefined,
      wechat: form.wechat || undefined,
      qq: form.qq || undefined,
      source: form.source || undefined,
      remark: form.remark || undefined,
    }
    if (editing.value) {
      await updateCustomer(editing.value.id, payload)
      ElMessage.success('客户信息已更新')
    } else {
      await createCustomer(payload)
      ElMessage.success('客户创建成功')
    }
    formVisible.value = false
    load()
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

async function handleDisable(c: Customer) {
  try {
    await ElMessageBox.confirm(`确定停用客户「${c.name}」吗？停用后不再参与新码分配。`, '停用确认', {
      confirmButtonText: '确认停用',
      cancelButtonText: '取消',
      type: 'warning',
    })
    await deleteCustomer(c.id)
    ElMessage.success('已停用')
    load()
  } catch {
    // 取消或失败
  }
}

// ============ 详情 ============
const detailVisible = ref(false)
const detail = ref<Customer | null>(null)
const keys = ref<LicenseKeyItem[]>([])
const devices = ref<KeyDevice[]>([])
const keysLoading = ref(false)
const devicesLoading = ref(false)

async function openDetail(c: Customer) {
  detail.value = c
  keys.value = []
  devices.value = []
  detailVisible.value = true

  keysLoading.value = true
  try {
    keys.value = await getCustomerKeys(c.id)
  } catch {
    keys.value = []
  } finally {
    keysLoading.value = false
  }

  // 通过各码详情聚合名下设备（并发分片，避免请求过多）
  devicesLoading.value = true
  try {
    const keyIds = keys.value.map((k) => k.id)
    const collected: KeyDevice[] = []
    for (let i = 0; i < keyIds.length; i += 5) {
      const chunk = keyIds.slice(i, i + 5)
      const results = await Promise.allSettled(chunk.map((id) => getKeyDetail(id)))
      results.forEach((r) => {
        if (r.status === 'fulfilled') collected.push(...r.value.devices)
      })
    }
    devices.value = collected
  } catch {
    devices.value = []
  } finally {
    devicesLoading.value = false
  }
}

onMounted(load)
</script>

<style scoped lang="scss">
.toolbar-right {
  margin-left: auto;
}

.customer-card {
  border-radius: var(--card-radius);
  cursor: pointer;
  margin-bottom: 16px;
  transition: transform 0.15s, box-shadow 0.15s;

  &:hover {
    transform: translateY(-2px);
  }
}

.customer-head {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 14px;
}

.customer-name {
  font-size: 15px;
  font-weight: 600;
  color: var(--text-main);
  display: flex;
  align-items: center;
  gap: 6px;
}

.customer-contact {
  font-size: 12px;
  color: var(--text-secondary);
  margin-top: 4px;
  display: flex;
  align-items: center;
  gap: 4px;

  .muted {
    color: #b0b8c4;
  }
}

.customer-stats {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 0;
  border-top: 1px solid #f0f2f7;

  .stat {
    flex: 1;
    text-align: center;

    .num {
      font-size: 18px;
      font-weight: 700;
      color: var(--text-main);

      &.date {
        font-size: 13px;
        font-weight: 500;
        color: var(--text-secondary);
      }
    }

    .lbl {
      font-size: 12px;
      color: var(--text-secondary);
      margin-top: 2px;
    }
  }
}

.customer-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  border-top: 1px solid #f0f2f7;
  padding-top: 10px;
}

.detail-info-card {
  display: flex;
  gap: 14px;
  background: #f7f9fc;
  border-radius: 8px;
  padding: 16px;
  margin-bottom: 16px;

  .detail-info {
    min-width: 0;
  }

  .detail-name {
    font-size: 17px;
    font-weight: 600;
    color: var(--text-main);
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .detail-contact {
    display: flex;
    flex-wrap: wrap;
    gap: 4px 16px;
    font-size: 13px;
    color: var(--text-secondary);
    margin-top: 8px;
  }

  .detail-remark {
    font-size: 13px;
    color: var(--text-secondary);
    margin-top: 6px;
  }
}

.detail-title {
  margin: 18px 0 10px;
  font-weight: 600;
  color: var(--text-main);
  display: flex;
  align-items: center;
  gap: 8px;
}

.code-cell {
  display: flex;
  align-items: center;
  gap: 4px;

  .code-font {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
}
</style>
