<template>
  <div class="site-panel">
    <div class="sub-toolbar">
      <el-button :icon="Refresh" @click="load">刷新</el-button>
      <div class="sub-toolbar-right">
        <el-button type="primary" :icon="Plus" @click="openCreate">新建案例</el-button>
      </div>
    </div>

    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="行业" width="120" prop="industry" />
      <el-table-column label="标题" min-width="200" show-overflow-tooltip>
        <template #default="{ row }">
          <el-button link type="primary" @click="openEdit(row)">{{ row.title }}</el-button>
        </template>
      </el-table-column>
      <el-table-column label="摘要" min-width="220" show-overflow-tooltip prop="summary" />
      <el-table-column label="状态" width="90" align="center">
        <template #default="{ row }">
          <el-tag size="small" :type="row.status === 'published' ? 'success' : 'info'">
            {{ row.status === 'published' ? '已发布' : '草稿' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="排序" width="80" align="center" prop="sort" />
      <el-table-column label="操作" width="130" fixed="right" align="center">
        <template #default="{ row }">
          <el-button link type="primary" size="small" @click="openEdit(row)">编辑</el-button>
          <el-button link type="danger" size="small" @click="handleDelete(row)">删除</el-button>
        </template>
      </el-table-column>
    </el-table>

    <div class="pagination-wrap">
      <el-pagination
        v-model:current-page="page"
        v-model:page-size="pageSize"
        :total="total"
        layout="total, prev, pager, next"
        @current-change="load"
      />
    </div>

    <el-drawer v-model="drawerVisible" :title="form.id ? '编辑案例' : '新建案例'" size="700px">
      <el-form :model="form" label-width="90px">
        <el-form-item label="标题" required>
          <el-input v-model="form.title" maxlength="200" placeholder="请输入案例标题" />
        </el-form-item>
        <el-row :gutter="12">
          <el-col :span="12">
            <el-form-item label="行业">
              <el-input v-model="form.industry" maxlength="64" placeholder="通用" />
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="状态">
              <el-select v-model="form.status" style="width: 100%">
                <el-option label="草稿" value="draft" />
                <el-option label="已发布" value="published" />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>
        <el-form-item label="摘要">
          <el-input v-model="form.summary" type="textarea" :rows="2" maxlength="500" show-word-limit />
        </el-form-item>
        <el-form-item label="客户头像">
          <div class="cover-row">
            <el-input v-model="form.avatar" placeholder="图片地址，可点击右侧上传" />
            <el-upload :show-file-list="false" :http-request="uploadAvatar" accept="image/*">
              <el-button :icon="Upload" :loading="avatarUploading">上传</el-button>
            </el-upload>
          </div>
        </el-form-item>
        <el-form-item label="数据指标">
          <el-input
            v-model="metricsText"
            type="textarea"
            :rows="3"
            placeholder='JSON 数组，例如 [{"label":"部署效率","value":"+300%"}]'
          />
          <div class="field-tip">留空表示无指标；格式为 JSON 数组，每一项含 label 与 value。</div>
        </el-form-item>
        <el-form-item label="案例详情">
          <RichEditor v-model="form.content" />
        </el-form-item>
        <el-form-item label="排序">
          <el-input-number v-model="form.sort" :min="0" :max="9999" />
          <span class="field-tip" style="margin-left: 8px">数值越小越靠前</span>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="drawerVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="submit">保存</el-button>
      </template>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox, type UploadRequestOptions } from 'element-plus'
import { Plus, Refresh, Upload } from '@element-plus/icons-vue'
import {
  getCases,
  createCase,
  updateCase,
  deleteCase,
  uploadSiteImage,
  type SiteCase,
} from '@/api/site'
import RichEditor from '@/components/RichEditor.vue'

const list = ref<SiteCase[]>([])
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const loading = ref(false)

async function load() {
  loading.value = true
  try {
    const res = await getCases({ page: page.value, pageSize: pageSize.value })
    list.value = res.list
    total.value = res.total
  } finally {
    loading.value = false
  }
}

const drawerVisible = ref(false)
const saving = ref(false)
const avatarUploading = ref(false)
const metricsText = ref('')

const form = reactive({
  id: '',
  industry: '通用',
  title: '',
  summary: '',
  content: '',
  avatar: '',
  sort: 0,
  status: 'published',
})

function resetForm() {
  form.id = ''
  form.industry = '通用'
  form.title = ''
  form.summary = ''
  form.content = ''
  form.avatar = ''
  form.sort = 0
  form.status = 'published'
  metricsText.value = ''
}

function openCreate() {
  resetForm()
  drawerVisible.value = true
}

function openEdit(row: SiteCase) {
  resetForm()
  form.id = row.id
  form.industry = row.industry || '通用'
  form.title = row.title
  form.summary = row.summary || ''
  form.content = row.content || ''
  form.avatar = row.avatar || ''
  form.sort = row.sort || 0
  form.status = row.status || 'published'
  metricsText.value = row.metrics || ''
  drawerVisible.value = true
}

async function uploadAvatar(opt: UploadRequestOptions) {
  avatarUploading.value = true
  try {
    const res = await uploadSiteImage(opt.file as File)
    form.avatar = res.url
    ElMessage.success('头像上传成功')
  } catch {
    // 拦截器已提示
  } finally {
    avatarUploading.value = false
  }
}

async function submit() {
  if (!form.title.trim()) {
    ElMessage.warning('请输入案例标题')
    return
  }
  const metrics = metricsText.value.trim()
  if (metrics) {
    try {
      const parsed = JSON.parse(metrics)
      if (!Array.isArray(parsed)) throw new Error('not array')
    } catch {
      ElMessage.error('数据指标必须是合法的 JSON 数组')
      return
    }
  }
  saving.value = true
  const payload = {
    industry: form.industry,
    title: form.title.trim(),
    summary: form.summary,
    content: form.content,
    avatar: form.avatar,
    metrics: metrics || undefined,
    sort: form.sort,
    status: form.status,
  }
  try {
    if (form.id) {
      await updateCase(form.id, payload)
      ElMessage.success('已保存')
    } else {
      await createCase(payload)
      ElMessage.success('已创建')
    }
    drawerVisible.value = false
    load()
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

async function handleDelete(row: SiteCase) {
  try {
    await ElMessageBox.confirm(`确定删除案例「${row.title}」吗？`, '删除确认', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消',
    })
    await deleteCase(row.id)
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

.cover-row {
  display: flex;
  gap: 10px;
  width: 100%;
  align-items: center;
}

.field-tip {
  font-size: 12px;
  color: var(--text-secondary);
  line-height: 1.6;
}
</style>
