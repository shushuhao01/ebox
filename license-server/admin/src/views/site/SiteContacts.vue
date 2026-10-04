<template>
  <div class="site-panel">
    <div class="sub-toolbar">
      <el-button :icon="Refresh" @click="load">刷新</el-button>
      <div class="sub-toolbar-right">
        <el-button type="primary" :icon="Plus" @click="openCreate">新增联系方式</el-button>
      </div>
    </div>

    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="类型" width="110" align="center">
        <template #default="{ row }">
          <el-tag size="small" effect="plain">{{ typeLabel(row.type) }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="名称" width="150" prop="name" />
      <el-table-column label="内容" min-width="200" show-overflow-tooltip>
        <template #default="{ row }">{{ row.value || '-' }}</template>
      </el-table-column>
      <el-table-column label="二维码" width="90" align="center">
        <template #default="{ row }">
          <el-image v-if="row.qrcode" :src="row.qrcode" :preview-src-list="[row.qrcode]" fit="cover" class="qr-thumb" preview-teleported />
          <span v-else>-</span>
        </template>
      </el-table-column>
      <el-table-column label="排序" width="80" align="center" prop="sort" />
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

    <el-dialog v-model="dialogVisible" :title="form.id ? '编辑联系方式' : '新增联系方式'" width="560px">
      <el-form :model="form" label-width="90px">
        <el-row :gutter="12">
          <el-col :span="12">
            <el-form-item label="类型">
              <el-select v-model="form.type" style="width: 100%">
                <el-option label="微信" value="wechat" />
                <el-option label="QQ" value="qq" />
                <el-option label="邮箱" value="email" />
                <el-option label="电话" value="phone" />
                <el-option label="其他" value="other" />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="排序">
              <el-input-number v-model="form.sort" :min="0" :max="9999" />
            </el-form-item>
          </el-col>
        </el-row>
        <el-form-item label="名称" required>
          <el-input v-model="form.name" maxlength="64" placeholder="例如：官方客服、技术支持邮箱" />
        </el-form-item>
        <el-form-item label="内容">
          <el-input v-model="form.value" maxlength="255" placeholder="微信号 / QQ 号 / 邮箱 / 电话" />
        </el-form-item>
        <el-form-item label="二维码">
          <div class="cover-row">
            <el-input v-model="form.qrcode" placeholder="图片地址，可点击右侧上传" />
            <el-upload :show-file-list="false" :http-request="uploadQrcode" accept="image/*">
              <el-button :icon="Upload" :loading="uploading">上传</el-button>
            </el-upload>
          </div>
          <img v-if="form.qrcode" :src="form.qrcode" class="qr-preview" alt="二维码预览" />
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
import { ElMessage, ElMessageBox, type UploadRequestOptions } from 'element-plus'
import { Plus, Refresh, Upload } from '@element-plus/icons-vue'
import {
  getContactList,
  createContact,
  updateContact,
  deleteContact,
  uploadSiteImage,
  type SiteContact,
} from '@/api/site'

const TYPE_MAP: Record<string, string> = {
  wechat: '微信',
  qq: 'QQ',
  email: '邮箱',
  phone: '电话',
  other: '其他',
}
function typeLabel(v: string): string {
  return TYPE_MAP[v] || v
}

const list = ref<SiteContact[]>([])
const loading = ref(false)

async function load() {
  loading.value = true
  try {
    list.value = await getContactList()
  } finally {
    loading.value = false
  }
}

const dialogVisible = ref(false)
const saving = ref(false)
const uploading = ref(false)
const form = reactive({
  id: '',
  type: 'wechat',
  name: '',
  value: '',
  qrcode: '',
  sort: 0,
  enabled: 1,
})

function resetForm() {
  form.id = ''
  form.type = 'wechat'
  form.name = ''
  form.value = ''
  form.qrcode = ''
  form.sort = 0
  form.enabled = 1
}

function openCreate() {
  resetForm()
  dialogVisible.value = true
}

function openEdit(row: SiteContact) {
  resetForm()
  form.id = row.id
  form.type = row.type
  form.name = row.name
  form.value = row.value || ''
  form.qrcode = row.qrcode || ''
  form.sort = row.sort || 0
  form.enabled = row.enabled ? 1 : 0
  dialogVisible.value = true
}

async function uploadQrcode(opt: UploadRequestOptions) {
  uploading.value = true
  try {
    const res = await uploadSiteImage(opt.file as File)
    form.qrcode = res.url
    ElMessage.success('二维码上传成功')
  } catch {
    // 拦截器已提示
  } finally {
    uploading.value = false
  }
}

async function submit() {
  if (!form.name.trim()) {
    ElMessage.warning('请输入名称')
    return
  }
  saving.value = true
  const payload = {
    type: form.type,
    name: form.name.trim(),
    value: form.value,
    qrcode: form.qrcode,
    sort: form.sort,
    enabled: form.enabled,
  }
  try {
    if (form.id) {
      await updateContact(form.id, payload)
      ElMessage.success('已保存')
    } else {
      await createContact(payload)
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

async function handleDelete(row: SiteContact) {
  try {
    await ElMessageBox.confirm(`确定删除「${row.name}」吗？`, '删除确认', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消',
    })
    await deleteContact(row.id)
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

.qr-thumb {
  width: 40px;
  height: 40px;
  border-radius: 4px;
}

.qr-preview {
  display: block;
  margin-top: 10px;
  max-width: 140px;
  max-height: 140px;
  border-radius: 6px;
  border: 1px solid var(--el-border-color);
}

.cover-row {
  display: flex;
  gap: 10px;
  width: 100%;
  align-items: center;
}
</style>
