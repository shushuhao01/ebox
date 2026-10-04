<template>
  <div class="site-panel">
    <!-- 工具栏 -->
    <div class="sub-toolbar">
      <el-select v-if="!fixedCategory" v-model="filterCategory" placeholder="全部分类" clearable style="width: 140px" @change="reload">
        <el-option v-for="c in CATEGORY_OPTIONS" :key="c.value" :label="c.label" :value="c.value" />
      </el-select>
      <el-select v-model="filterStatus" placeholder="全部状态" clearable style="width: 130px" @change="reload">
        <el-option label="草稿" value="draft" />
        <el-option label="已发布" value="published" />
      </el-select>
      <el-button :icon="Refresh" @click="reload">刷新</el-button>
      <div class="sub-toolbar-right">
        <el-button type="primary" :icon="Plus" @click="openCreate">新建{{ fixedCategory ? '公告' : '文章' }}</el-button>
      </div>
    </div>

    <!-- 列表 -->
    <el-table v-loading="loading" :data="list" stripe>
      <el-table-column label="标题" min-width="220" show-overflow-tooltip>
        <template #default="{ row }">
          <el-button link type="primary" @click="openEdit(row)">{{ row.title }}</el-button>
        </template>
      </el-table-column>
      <el-table-column v-if="!fixedCategory" label="分类" width="90" align="center">
        <template #default="{ row }">
          <el-tag size="small" effect="plain">{{ categoryLabel(row.category) }}</el-tag>
        </template>
      </el-table-column>
      <el-table-column label="标识 slug" min-width="160" show-overflow-tooltip>
        <template #default="{ row }"><span class="code-font">{{ row.slug }}</span></template>
      </el-table-column>
      <el-table-column label="状态" width="90" align="center">
        <template #default="{ row }">
          <el-tag size="small" :type="row.status === 'published' ? 'success' : 'info'">
            {{ row.status === 'published' ? '已发布' : '草稿' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="置顶" width="70" align="center">
        <template #default="{ row }">
          <el-icon v-if="row.pinned" color="#F59E0B"><Top /></el-icon>
          <span v-else>-</span>
        </template>
      </el-table-column>
      <el-table-column label="浏览" width="80" align="center" prop="views" />
      <el-table-column label="更新时间" width="160">
        <template #default="{ row }">{{ formatTime(row.updatedAt || row.createdAt) }}</template>
      </el-table-column>
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

    <!-- 编辑抽屉 -->
    <el-drawer v-model="drawerVisible" :title="form.id ? `编辑：${form.title}` : (fixedCategory ? '新建公告' : '新建文章')" size="760px">
      <el-form :model="form" label-width="90px">
        <el-form-item label="标题" required>
          <el-input v-model="form.title" maxlength="200" placeholder="请输入标题" />
        </el-form-item>
        <el-row :gutter="12">
          <el-col :span="12">
            <el-form-item label="分类">
              <el-select v-model="form.category" :disabled="fixedCategory" style="width: 100%">
                <el-option v-for="c in CATEGORY_OPTIONS" :key="c.value" :label="c.label" :value="c.value" />
              </el-select>
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
        <el-form-item label="标识 slug">
          <el-input v-model="form.slug" maxlength="160" placeholder="留空自动生成，用于文章链接" />
        </el-form-item>
        <el-form-item label="封面图">
          <div class="cover-row">
            <el-input v-model="form.cover" placeholder="图片地址，可点击右侧上传" />
            <el-upload :show-file-list="false" :http-request="uploadCover" accept="image/*">
              <el-button :icon="Upload" :loading="coverUploading">上传</el-button>
            </el-upload>
          </div>
          <img v-if="form.cover" :src="form.cover" class="cover-preview" alt="封面预览" />
        </el-form-item>
        <el-form-item label="摘要">
          <el-input v-model="form.summary" type="textarea" :rows="2" maxlength="500" show-word-limit placeholder="列表页展示的简短描述" />
        </el-form-item>
        <el-form-item label="正文">
          <RichEditor v-model="form.content" />
        </el-form-item>
        <el-form-item label="置顶">
          <el-switch v-model="form.pinned" :active-value="1" :inactive-value="0" />
        </el-form-item>
        <el-form-item label="SEO 标题">
          <el-input v-model="form.seoTitle" maxlength="200" placeholder="可选" />
        </el-form-item>
        <el-form-item label="SEO 描述">
          <el-input v-model="form.seoDesc" type="textarea" :rows="2" maxlength="500" placeholder="可选" />
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
import { Plus, Refresh, Upload, Top } from '@element-plus/icons-vue'
import {
  getArticles,
  getArticle,
  createArticle,
  updateArticle,
  deleteArticle,
  uploadSiteImage,
  type SiteArticle,
} from '@/api/site'
import { formatTime } from '@/utils/format'
import RichEditor from '@/components/RichEditor.vue'

const props = withDefaults(
  defineProps<{ categories?: string; fixedCategory?: boolean }>(),
  { categories: '', fixedCategory: false },
)

const CATEGORY_OPTIONS = [
  { value: 'tutorial', label: '教程' },
  { value: 'science', label: '科普' },
  { value: 'update', label: '更新' },
  { value: 'notice', label: '公告' },
]

function categoryLabel(v: string): string {
  return CATEGORY_OPTIONS.find((c) => c.value === v)?.label || v
}

const list = ref<SiteArticle[]>([])
const page = ref(1)
const pageSize = ref(20)
const total = ref(0)
const loading = ref(false)
const filterCategory = ref('')
const filterStatus = ref('')

async function load() {
  loading.value = true
  try {
    const category = filterCategory.value || props.categories
    const res = await getArticles({
      page: page.value,
      pageSize: pageSize.value,
      category: category || undefined,
      status: filterStatus.value || undefined,
    })
    list.value = res.list
    total.value = res.total
  } finally {
    loading.value = false
  }
}

function reload() {
  page.value = 1
  load()
}

// ============ 编辑表单 ============
const drawerVisible = ref(false)
const saving = ref(false)
const coverUploading = ref(false)

const form = reactive({
  id: '',
  title: '',
  slug: '',
  category: 'tutorial',
  cover: '',
  summary: '',
  content: '',
  contentType: 'html',
  status: 'draft',
  pinned: 0 as number,
  seoTitle: '',
  seoDesc: '',
})

function resetForm() {
  form.id = ''
  form.title = ''
  form.slug = ''
  form.category = props.fixedCategory ? 'notice' : 'tutorial'
  form.cover = ''
  form.summary = ''
  form.content = ''
  form.contentType = 'html'
  form.status = 'draft'
  form.pinned = 0
  form.seoTitle = ''
  form.seoDesc = ''
}

function openCreate() {
  resetForm()
  drawerVisible.value = true
}

async function openEdit(row: SiteArticle) {
  resetForm()
  drawerVisible.value = true
  try {
    const full = await getArticle(row.id)
    form.id = full.id
    form.title = full.title
    form.slug = full.slug
    form.category = full.category
    form.cover = full.cover || ''
    form.summary = full.summary || ''
    form.content = full.content || ''
    form.contentType = full.contentType || 'html'
    form.status = full.status
    form.pinned = full.pinned ? 1 : 0
    form.seoTitle = full.seoTitle || ''
    form.seoDesc = full.seoDesc || ''
  } catch {
    drawerVisible.value = false
  }
}

async function uploadCover(opt: UploadRequestOptions) {
  coverUploading.value = true
  try {
    const res = await uploadSiteImage(opt.file as File)
    form.cover = res.url
    ElMessage.success('封面上传成功')
  } catch {
    // 拦截器已提示
  } finally {
    coverUploading.value = false
  }
}

async function submit() {
  if (!form.title.trim()) {
    ElMessage.warning('请输入标题')
    return
  }
  saving.value = true
  const payload = {
    title: form.title.trim(),
    slug: form.slug.trim(),
    category: form.category,
    cover: form.cover,
    summary: form.summary,
    content: form.content,
    contentType: form.contentType,
    status: form.status,
    pinned: form.pinned,
    seoTitle: form.seoTitle,
    seoDesc: form.seoDesc,
  }
  try {
    if (form.id) {
      await updateArticle(form.id, payload)
      ElMessage.success('已保存')
    } else {
      await createArticle(payload)
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

async function handleDelete(row: SiteArticle) {
  try {
    await ElMessageBox.confirm(`确定删除「${row.title}」吗？`, '删除确认', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消',
    })
    await deleteArticle(row.id)
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

.cover-preview {
  display: block;
  margin-top: 10px;
  max-width: 220px;
  max-height: 120px;
  border-radius: 6px;
  border: 1px solid var(--el-border-color);
}
</style>
