<template>
  <div class="site-panel">
    <!-- 维护模式 / 访问口令 -->
    <div class="section-title">访问状态</div>
    <el-form :model="secForm" label-width="120px" v-loading="secLoading">
      <el-form-item label="维护模式">
        <el-switch v-model="secForm.maintenance_mode" active-value="1" inactive-value="0" />
        <span class="field-tip" style="margin-left: 10px">开启后官网前台显示维护提示，仅管理后台可正常访问。</span>
      </el-form-item>
      <el-form-item label="访问口令">
        <el-input v-model="secForm.access_password" maxlength="64" show-password placeholder="留空表示不启用口令访问" style="max-width: 320px" />
        <div class="field-tip">预留的整站访问口令（可选）。</div>
      </el-form-item>
      <el-form-item>
        <el-button type="primary" :loading="secSaving" @click="saveSecurity">保存</el-button>
      </el-form-item>
    </el-form>

    <!-- 访问规则 -->
    <div class="section-title rules-title">
      访问规则
      <el-button type="primary" size="small" :icon="Plus" class="rules-add" @click="openCreate">新增规则</el-button>
    </div>
    <div class="upload-tip">IP 支持精确地址或 CIDR（如 192.168.1.0/24）；UA 支持关键字或正则。白名单启用后仅白名单 IP 可访问官网接口。</div>

    <el-table v-loading="ruleLoading" :data="rules" stripe>
      <el-table-column label="类型" width="130" align="center">
        <template #default="{ row }">
          <el-tag size="small" :type="ruleTag(row.type)" effect="plain">{{ ruleLabel(row.type) }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="匹配内容" min-width="200" show-overflow-tooltip>
        <template #default="{ row }"><span class="code-font">{{ row.pattern }}</span></template>
      </el-table-column>
      <el-table-column label="备注" min-width="160" show-overflow-tooltip>
        <template #default="{ row }">{{ row.note || '-' }}</template>
      </el-table-column>
      <el-table-column label="启用" width="90" align="center">
        <template #default="{ row }">
          <el-tag size="small" :type="row.enabled ? 'success' : 'info'">{{ row.enabled ? '启用' : '停用' }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="操作" width="130" fixed="right" align="center">
        <template #default="{ row }">
          <el-button link type="primary" size="small" @click="openEdit(row)">编辑</el-button>
          <el-button link type="danger" size="small" @click="handleDelete(row)">删除</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog v-model="dialogVisible" :title="form.id ? '编辑规则' : '新增规则'" width="520px">
      <el-form :model="form" label-width="90px">
        <el-form-item label="规则类型">
          <el-select v-model="form.type" style="width: 100%">
            <el-option label="IP 黑名单" value="ip_black" />
            <el-option label="IP 白名单" value="ip_white" />
            <el-option label="UA 拦截" value="ua" />
          </el-select>
        </el-form-item>
        <el-form-item label="匹配内容" required>
          <el-input v-model="form.pattern" maxlength="255" placeholder="IP / CIDR / UA 关键字或正则" />
        </el-form-item>
        <el-form-item label="备注">
          <el-input v-model="form.note" maxlength="255" placeholder="可选" />
        </el-form-item>
        <el-form-item label="是否启用">
          <el-switch v-model="form.enabled" :active-value="1" :inactive-value="0" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="dialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="submit">保存</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Plus } from '@element-plus/icons-vue'
import {
  getSiteSecurity,
  updateSiteSecurity,
  getAccessRules,
  createAccessRule,
  updateAccessRule,
  deleteAccessRule,
  type SiteAccessRule,
} from '@/api/site'

// ============ 安全状态 ============
const secLoading = ref(false)
const secSaving = ref(false)
const secForm = reactive({ maintenance_mode: '0', access_password: '' })

async function loadSecurity() {
  secLoading.value = true
  try {
    const data = await getSiteSecurity()
    secForm.maintenance_mode = data.maintenance_mode || '0'
    secForm.access_password = data.access_password || ''
  } finally {
    secLoading.value = false
  }
}

async function saveSecurity() {
  secSaving.value = true
  try {
    await updateSiteSecurity({ ...secForm })
    ElMessage.success('已保存')
  } catch {
    // 拦截器已提示
  } finally {
    secSaving.value = false
  }
}

// ============ 访问规则 ============
const RULE_MAP: Record<string, { label: string; tag: 'danger' | 'success' | 'warning' }> = {
  ip_black: { label: 'IP 黑名单', tag: 'danger' },
  ip_white: { label: 'IP 白名单', tag: 'success' },
  ua: { label: 'UA 拦截', tag: 'warning' },
}
function ruleLabel(v: string): string {
  return RULE_MAP[v]?.label || v
}
function ruleTag(v: string): 'danger' | 'success' | 'warning' {
  return RULE_MAP[v]?.tag || 'warning'
}

const rules = ref<SiteAccessRule[]>([])
const ruleLoading = ref(false)

async function loadRules() {
  ruleLoading.value = true
  try {
    rules.value = await getAccessRules()
  } finally {
    ruleLoading.value = false
  }
}

const dialogVisible = ref(false)
const saving = ref(false)
const form = reactive({ id: '', type: 'ip_black', pattern: '', note: '', enabled: 1 })

function resetForm() {
  form.id = ''
  form.type = 'ip_black'
  form.pattern = ''
  form.note = ''
  form.enabled = 1
}

function openCreate() {
  resetForm()
  dialogVisible.value = true
}

function openEdit(row: SiteAccessRule) {
  resetForm()
  form.id = row.id
  form.type = row.type
  form.pattern = row.pattern
  form.note = row.note || ''
  form.enabled = row.enabled ? 1 : 0
  dialogVisible.value = true
}

async function submit() {
  if (!form.pattern.trim()) {
    ElMessage.warning('请输入匹配内容')
    return
  }
  saving.value = true
  const payload = {
    type: form.type,
    pattern: form.pattern.trim(),
    note: form.note,
    enabled: form.enabled,
  }
  try {
    if (form.id) {
      await updateAccessRule(form.id, payload)
      ElMessage.success('已保存')
    } else {
      await createAccessRule(payload)
      ElMessage.success('已新增')
    }
    dialogVisible.value = false
    loadRules()
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

async function handleDelete(row: SiteAccessRule) {
  try {
    await ElMessageBox.confirm(`确定删除规则「${row.pattern}」吗？`, '删除确认', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消',
    })
    await deleteAccessRule(row.id)
    ElMessage.success('已删除')
    loadRules()
  } catch {
    // 取消或失败
  }
}

onMounted(() => {
  loadSecurity()
  loadRules()
})
</script>

<style scoped lang="scss">
.section-title {
  margin: 4px 0 14px;
  padding-left: 8px;
  font-size: 15px;
  font-weight: 600;
  color: var(--text-main);
  border-left: 3px solid var(--primary-color);
  display: flex;
  align-items: center;
}

.rules-title {
  margin-top: 24px;
}

.rules-add {
  margin-left: auto;
}

.field-tip {
  font-size: 12px;
  color: var(--text-secondary);
  line-height: 1.6;
}

.upload-tip {
  font-size: 12px;
  color: var(--text-secondary);
  margin-bottom: 12px;
}
</style>
