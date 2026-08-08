<template>
  <div class="page-container">
    <!-- 工具栏 -->
    <div class="toolbar-card">
      <span class="page-title">码批次</span>
      <div class="toolbar-right">
        <el-button type="primary" :icon="Plus" @click="createVisible = true">新建批次</el-button>
      </div>
    </div>

    <!-- 批次卡片列表 -->
    <div v-loading="loading">
      <el-empty v-if="!list.length && !loading" description="暂无批次" />
      <el-row :gutter="16">
        <el-col v-for="b in list" :key="b.id" :xs="24" :sm="12" :md="8" :lg="6">
          <el-card shadow="hover" class="batch-card" @click="openDetail(b)">
            <div class="batch-head">
              <div class="batch-icon"><el-icon :size="20"><Collection /></el-icon></div>
              <div class="batch-name" :title="b.name">{{ b.name }}</div>
              <el-button
                class="batch-del"
                link
                type="danger"
                size="small"
                :icon="Delete"
                @click.stop="handleDelete(b)"
              >删除</el-button>
            </div>
            <div class="batch-stats">
              <div class="stat">
                <div class="num">{{ b.totalCount ?? 0 }}</div>
                <div class="lbl">码总数</div>
              </div>
              <el-divider direction="vertical" />
              <div class="stat">
                <div class="num used">{{ b.usedCount ?? 0 }}</div>
                <div class="lbl">已用</div>
              </div>
              <el-divider direction="vertical" />
              <div class="stat">
                <div class="num" :style="{ color: progressColor(b) }">{{ usagePercent(b) }}%</div>
                <div class="lbl">使用率</div>
              </div>
            </div>
            <div class="batch-foot">
              <el-progress
                :percentage="usagePercent(b)"
                :stroke-width="6"
                :color="progressColor(b)"
                :show-text="false"
              />
              <div class="meta">创建：{{ formatTime(b.createdAt, 'YYYY-MM-DD') }}</div>
            </div>
          </el-card>
        </el-col>
      </el-row>

      <div class="pagination-wrap">
        <el-pagination
          v-model:current-page="page"
          v-model:page-size="pageSize"
          :total="total"
          layout="total, prev, pager, next"
          @current-change="load"
        />
      </div>
    </div>

    <!-- 新建批次 -->
    <el-dialog v-model="createVisible" title="新建批次" width="460px">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="80px">
        <el-form-item label="批次名" prop="name">
          <el-input v-model="form.name" placeholder="请输入批次名" maxlength="64" />
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.remark" type="textarea" :rows="3" placeholder="备注（可选）" maxlength="255" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="createVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="submit">确定</el-button>
      </template>
    </el-dialog>

    <!-- 批次详情抽屉 -->
    <el-drawer v-model="detailVisible" :title="`批次详情：${detail?.batch.name || ''}`" size="600px">
      <template v-if="detail">
        <el-descriptions :column="2" border size="small">
          <el-descriptions-item label="批次名">{{ detail.batch.name }}</el-descriptions-item>
          <el-descriptions-item label="创建人ID">{{ detail.batch.createdBy }}</el-descriptions-item>
          <el-descriptions-item label="码总数">{{ detail.batch.totalCount ?? detail.keys.length }}</el-descriptions-item>
          <el-descriptions-item label="已用">{{ detail.batch.usedCount ?? 0 }}</el-descriptions-item>
          <el-descriptions-item label="创建时间" :span="2">{{ formatTime(detail.batch.createdAt) }}</el-descriptions-item>
          <el-descriptions-item label="备注" :span="2">{{ detail.batch.remark || '-' }}</el-descriptions-item>
        </el-descriptions>
        <div style="margin-top: 14px; text-align: right">
          <el-button type="danger" plain :icon="Delete" @click="handleDelete(detail.batch)">删除批次</el-button>
        </div>

        <div class="detail-title">批次激活码（{{ detail.keys.length }}）</div>
        <el-table :data="detail.keys" size="small" stripe max-height="460">
          <el-table-column label="激活码" min-width="230">
            <template #default="{ row }">
              <div class="code-cell">
                <span class="code-font">{{ row.code }}</span>
                <el-button link type="primary" :icon="CopyDocument" size="small" @click="copyText(row.code)" />
              </div>
            </template>
          </el-table-column>
          <el-table-column label="状态" width="90" align="center">
            <template #default="{ row }">
              <el-tag :type="KEY_STATUS[row.status]?.type || 'info'" size="small">
                {{ KEY_STATUS[row.status]?.label || row.status }}
              </el-tag>
            </template>
          </el-table-column>
          <el-table-column label="创建时间" width="150">
            <template #default="{ row }">{{ formatTime(row.createdAt) }}</template>
          </el-table-column>
        </el-table>
      </template>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox, type FormInstance, type FormRules } from 'element-plus'
import { Plus, Collection, CopyDocument, Delete } from '@element-plus/icons-vue'
import { getBatches, createBatch, getBatchDetail, deleteBatch, type KeyBatch } from '@/api/batches'
import type { LicenseKeyItem } from '@/api/keys'
import { KEY_STATUS, copyText, formatTime } from '@/utils/format'

const list = ref<KeyBatch[]>([])
const page = ref(1)
const pageSize = ref(12)
const total = ref(0)
const loading = ref(false)

async function load() {
  loading.value = true
  try {
    const res = await getBatches({ page: page.value, pageSize: pageSize.value })
    list.value = res.list
    total.value = res.total
  } finally {
    loading.value = false
  }
}

function usagePercent(b: KeyBatch): number {
  const t = b.totalCount ?? 0
  if (!t) return 0
  return Math.min(100, Math.round(((b.usedCount ?? 0) / t) * 100))
}

function progressColor(b: KeyBatch): string {
  const p = usagePercent(b)
  if (p >= 80) return '#EF4444'
  if (p >= 50) return '#F59E0B'
  return '#3A7AFE'
}

// ============ 新建 ============
const createVisible = ref(false)
const saving = ref(false)
const formRef = ref<FormInstance>()
const form = reactive({ name: '', remark: '' })
const rules: FormRules = {
  name: [{ required: true, message: '请输入批次名', trigger: 'blur' }],
}

async function submit() {
  if (!formRef.value) return
  const valid = await formRef.value.validate().catch(() => false)
  if (!valid) return
  saving.value = true
  try {
    await createBatch({ name: form.name, remark: form.remark || undefined })
    ElMessage.success('批次创建成功')
    createVisible.value = false
    form.name = ''
    form.remark = ''
    page.value = 1
    load()
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

// ============ 详情 ============
const detailVisible = ref(false)
const detail = ref<{ batch: KeyBatch; keys: LicenseKeyItem[] } | null>(null)

async function openDetail(b: KeyBatch) {
  detailVisible.value = true
  detail.value = null
  try {
    detail.value = await getBatchDetail(b.id)
  } catch {
    detailVisible.value = false
  }
}

// ============ 删除 ============
async function handleDelete(b: KeyBatch) {
  try {
    await ElMessageBox.confirm(
      `确定删除批次 <b>${b.name}</b> 吗？<br/>该批次下 ${b.totalCount ?? 0} 个激活码<u>不会删除</u>，仅解除批次归属。`,
      '删除批次',
      {
        confirmButtonText: '确认删除',
        cancelButtonText: '取消',
        type: 'warning',
        dangerouslyUseHTMLString: true,
        confirmButtonClass: 'el-button--danger',
      }
    )
    await deleteBatch(b.id)
    ElMessage.success('批次已删除')
    detailVisible.value = false
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
}

.batch-card {
  border-radius: var(--card-radius);
  cursor: pointer;
  margin-bottom: 16px;
  transition: transform 0.15s, box-shadow 0.15s;

  &:hover {
    transform: translateY(-2px);
  }
}

.batch-head {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 14px;

  .batch-icon {
    width: 38px;
    height: 38px;
    border-radius: 8px;
    background: rgba(58, 122, 254, 0.12);
    color: var(--primary-color);
    display: flex;
    align-items: center;
    justify-content: center;
  }

  .batch-name {
    font-size: 15px;
    font-weight: 600;
    color: var(--text-main);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    flex: 1;
    min-width: 0;
  }

  .batch-del {
    flex-shrink: 0;
  }
}

.batch-stats {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 14px;

  .stat {
    flex: 1;
    text-align: center;

    .num {
      font-size: 20px;
      font-weight: 700;
      color: var(--text-main);

      &.used {
        color: var(--success-color);
      }
    }

    .lbl {
      font-size: 12px;
      color: var(--text-secondary);
      margin-top: 2px;
    }
  }
}

.batch-foot {
  .meta {
    font-size: 12px;
    color: var(--text-secondary);
    margin-top: 8px;
  }
}

.detail-title {
  margin: 18px 0 10px;
  font-weight: 600;
  color: var(--text-main);
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
