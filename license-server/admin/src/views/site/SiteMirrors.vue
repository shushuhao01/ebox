<template>
  <div class="site-panel">
    <el-alert
      title="在官网「下载」页展示的下载地址，支持直链、GitHub Release、网盘等多种来源。"
      type="info"
      :closable="false"
      show-icon
      class="panel-alert"
    />

    <div class="sub-toolbar">
      <el-button :icon="Refresh" @click="load">刷新</el-button>
      <div class="sub-toolbar-right">
        <el-button type="primary" :icon="Plus" @click="openCreate">新增下载地址</el-button>
      </div>
    </div>

    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="名称" min-width="160" show-overflow-tooltip prop="name" />
      <el-table-column label="类型" width="120" align="center">
        <template #default="{ row }">
          <el-tag size="small" :type="typeTag(row.type)" effect="plain">{{ typeLabel(row.type) }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="地址" min-width="260" show-overflow-tooltip>
        <template #default="{ row }">
          <span class="code-font">{{ row.url }}</span>
        </template>
      </el-table-column>
      <el-table-column label="排序" width="80" align="center" prop="sort" />
      <el-table-column label="启用" width="90" align="center">
        <template #default="{ row }">
          <el-tag size="small" :type="row.enabled ? 'success' : 'info'">
            {{ row.enabled ? '已启用' : '已停用' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="操作" width="170" fixed="right" align="center">
        <template #default="{ row }">
          <el-button link type="primary" size="small" @click="copy(row.url)">复制</el-button>
          <el-button link type="primary" size="small" @click="openEdit(row)">编辑</el-button>
          <el-button link type="danger" size="small" @click="handleDelete(row)">删除</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog v-model="dialogVisible" :title="form.id ? '编辑下载地址' : '新增下载地址'" width="560px">
      <el-form :model="form" label-width="90px">
        <el-form-item label="名称" required>
          <el-input v-model="form.name" maxlength="64" placeholder="例如：官方直链、百度网盘、GitHub" />
        </el-form-item>
        <el-form-item label="类型">
          <el-select v-model="form.type" style="width: 100%">
            <el-option label="官方直链" value="mirror" />
            <el-option label="网盘" value="netdisk" />
            <el-option label="GitHub" value="github" />
            <el-option label="其他" value="other" />
          </el-select>
        </el-form-item>
        <el-form-item label="下载地址" required>
          <el-input v-model="form.url" placeholder="https://...（含网盘分享链接）" />
        </el-form-item>
        <el-row :gutter="12">
          <el-col :span="12">
            <el-form-item label="排序">
              <el-input-number v-model="form.sort" :min="0" :max="9999" />
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="启用">
              <el-switch v-model="form.enabled" :active-value="1" :inactive-value="0" />
            </el-form-item>
          </el-col>
        </el-row>
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
import { Plus, Refresh } from '@element-plus/icons-vue'
import {
  getMirrorList,
  createMirror,
  updateMirror,
  deleteMirror,
  type SiteMirror,
} from '@/api/site'
import { copyText } from '@/utils/format'

const TYPE_MAP: Record<string, { label: string; tag: 'primary' | 'success' | 'warning' | 'info' }> = {
  mirror: { label: '官方直链', tag: 'primary' },
  netdisk: { label: '网盘', tag: 'success' },
  github: { label: 'GitHub', tag: 'info' },
  other: { label: '其他', tag: 'warning' },
}

function typeLabel(v: string): string {
  return TYPE_MAP[v]?.label || v
}
function typeTag(v: string): 'primary' | 'success' | 'warning' | 'info' {
  return TYPE_MAP[v]?.tag || 'info'
}

const list = ref<SiteMirror[]>([])
const loading = ref(false)

async function load() {
  loading.value = true
  try {
    list.value = await getMirrorList()
  } finally {
    loading.value = false
  }
}

const dialogVisible = ref(false)
const saving = ref(false)
const form = reactive({
  id: '',
  name: '',
  url: '',
  type: 'mirror',
  sort: 0,
  enabled: 1,
})

function resetForm() {
  form.id = ''
  form.name = ''
  form.url = ''
  form.type = 'mirror'
  form.sort = 0
  form.enabled = 1
}

function openCreate() {
  resetForm()
  dialogVisible.value = true
}

function openEdit(row: SiteMirror) {
  resetForm()
  form.id = row.id
  form.name = row.name
  form.url = row.url
  form.type = row.type
  form.sort = row.sort || 0
  form.enabled = row.enabled ? 1 : 0
  dialogVisible.value = true
}

async function submit() {
  if (!form.name.trim()) {
    ElMessage.warning('请输入名称')
    return
  }
  if (!form.url.trim()) {
    ElMessage.warning('请输入下载地址')
    return
  }
  saving.value = true
  const payload = {
    name: form.name.trim(),
    url: form.url.trim(),
    type: form.type,
    sort: form.sort,
    enabled: form.enabled,
  }
  try {
    if (form.id) {
      await updateMirror(form.id, payload)
      ElMessage.success('已保存')
    } else {
      await createMirror(payload)
      ElMessage.success('已新增')
    }
    dialogVisible.value = false
    load()
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

async function handleDelete(row: SiteMirror) {
  try {
    await ElMessageBox.confirm(`确定删除下载地址「${row.name}」吗？`, '删除确认', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消',
    })
    await deleteMirror(row.id)
    ElMessage.success('已删除')
    load()
  } catch {
    // 取消或失败
  }
}

function copy(url: string) {
  copyText(url, '下载地址已复制')
}

onMounted(load)
</script>

<style scoped lang="scss">
.panel-alert {
  margin-bottom: 14px;
}

.sub-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 14px;

  .sub-toolbar-right {
    margin-left: auto;
  }
}
</style>
