<template>
  <div class="site-panel">
    <div class="sub-toolbar">
      <el-button :icon="Refresh" @click="load">刷新</el-button>
      <div class="sub-toolbar-right">
        <el-button type="primary" :icon="Plus" @click="openCreate">新增导航</el-button>
      </div>
    </div>

    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="位置" width="100" align="center">
        <template #default="{ row }">
          <el-tag size="small" effect="plain">{{ row.position === 'footer' ? '页脚' : '顶部' }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="名称" width="140" prop="label" />
      <el-table-column label="链接" min-width="240" show-overflow-tooltip>
        <template #default="{ row }"><span class="code-font">{{ row.url }}</span></template>
      </el-table-column>
      <el-table-column label="打开方式" width="110" align="center">
        <template #default="{ row }">{{ row.target === '_blank' ? '新窗口' : '当前窗口' }}</template>
      </el-table-column>
      <el-table-column label="排序" width="80" align="center" prop="sort" />
      <el-table-column label="显示" width="90" align="center">
        <template #default="{ row }">
          <el-tag size="small" :type="row.visible ? 'success' : 'info'">{{ row.visible ? '显示' : '隐藏' }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="操作" width="130" fixed="right" align="center">
        <template #default="{ row }">
          <el-button link type="primary" size="small" @click="openEdit(row)">编辑</el-button>
          <el-button link type="danger" size="small" @click="handleDelete(row)">删除</el-button>
        </template>
      </el-table-column>
    </el-table>

    <el-dialog v-model="dialogVisible" :title="form.id ? '编辑导航' : '新增导航'" width="520px">
      <el-form :model="form" label-width="90px">
        <el-form-item label="位置">
          <el-radio-group v-model="form.position">
            <el-radio value="top">顶部导航</el-radio>
            <el-radio value="footer">页脚导航</el-radio>
          </el-radio-group>
        </el-form-item>
        <el-form-item label="名称" required>
          <el-input v-model="form.label" maxlength="64" placeholder="例如：功能特性" />
        </el-form-item>
        <el-form-item label="链接" required>
          <el-input v-model="form.url" placeholder="站内路径（/features）或完整地址" />
        </el-form-item>
        <el-row :gutter="12">
          <el-col :span="12">
            <el-form-item label="打开方式">
              <el-select v-model="form.target" style="width: 100%">
                <el-option label="当前窗口" value="_self" />
                <el-option label="新窗口" value="_blank" />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="排序">
              <el-input-number v-model="form.sort" :min="0" :max="9999" />
            </el-form-item>
          </el-col>
        </el-row>
        <el-form-item label="是否显示">
          <el-switch v-model="form.visible" :active-value="1" :inactive-value="0" />
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
import { Plus, Refresh } from '@element-plus/icons-vue'
import { getNavList, createNav, updateNav, deleteNav, type SiteNav } from '@/api/site'

const list = ref<SiteNav[]>([])
const loading = ref(false)

async function load() {
  loading.value = true
  try {
    list.value = await getNavList()
  } finally {
    loading.value = false
  }
}

const dialogVisible = ref(false)
const saving = ref(false)
const form = reactive({
  id: '',
  position: 'top',
  label: '',
  url: '',
  target: '_self',
  sort: 0,
  visible: 1,
})

function resetForm() {
  form.id = ''
  form.position = 'top'
  form.label = ''
  form.url = ''
  form.target = '_self'
  form.sort = 0
  form.visible = 1
}

function openCreate() {
  resetForm()
  dialogVisible.value = true
}

function openEdit(row: SiteNav) {
  resetForm()
  form.id = row.id
  form.position = row.position
  form.label = row.label
  form.url = row.url
  form.target = row.target
  form.sort = row.sort || 0
  form.visible = row.visible ? 1 : 0
  dialogVisible.value = true
}

async function submit() {
  if (!form.label.trim()) {
    ElMessage.warning('请输入导航名称')
    return
  }
  if (!form.url.trim()) {
    ElMessage.warning('请输入链接地址')
    return
  }
  saving.value = true
  const payload = {
    position: form.position,
    label: form.label.trim(),
    url: form.url.trim(),
    target: form.target,
    sort: form.sort,
    visible: form.visible,
  }
  try {
    if (form.id) {
      await updateNav(form.id, payload)
      ElMessage.success('已保存')
    } else {
      await createNav(payload)
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

async function handleDelete(row: SiteNav) {
  try {
    await ElMessageBox.confirm(`确定删除导航「${row.label}」吗？`, '删除确认', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消',
    })
    await deleteNav(row.id)
    ElMessage.success('已删除')
    load()
  } catch {
    // 取消或失败
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
</style>
