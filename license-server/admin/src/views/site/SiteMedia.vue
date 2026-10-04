<template>
  <div class="site-panel">
    <div class="sub-toolbar">
      <el-button :icon="Refresh" @click="load">刷新</el-button>
      <div class="sub-toolbar-right">
        <el-upload :show-file-list="false" :http-request="doUpload" accept="image/*">
          <el-button type="primary" :icon="Upload" :loading="uploading">上传图片</el-button>
        </el-upload>
      </div>
    </div>
    <div class="upload-tip">支持 jpg / png / webp / gif / svg，单张不超过 10MB。上传后可复制地址用于文章、封面或设置。</div>

    <div v-loading="loading" class="media-grid">
      <el-empty v-if="!list.length && !loading" description="暂无图片" />
      <div v-for="m in list" :key="m.id" class="media-item">
        <el-image :src="m.path" fit="cover" class="media-img" :preview-src-list="[m.path]" preview-teleported />
        <div class="media-meta">
          <div class="media-name" :title="m.filename">{{ m.filename }}</div>
          <div class="media-size">{{ formatSize(m.size) }}</div>
        </div>
        <div class="media-actions">
          <el-button link type="primary" size="small" @click="copy(m.path)">复制地址</el-button>
          <el-button link type="danger" size="small" @click="handleDelete(m)">删除</el-button>
        </div>
      </div>
    </div>

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
</template>

<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage, ElMessageBox, type UploadRequestOptions } from 'element-plus'
import { Refresh, Upload } from '@element-plus/icons-vue'
import { getMediaList, deleteMedia, uploadSiteImage, type SiteMedia } from '@/api/site'
import { copyText } from '@/utils/format'

const list = ref<SiteMedia[]>([])
const page = ref(1)
const pageSize = ref(24)
const total = ref(0)
const loading = ref(false)
const uploading = ref(false)

async function load() {
  loading.value = true
  try {
    const res = await getMediaList({ page: page.value, pageSize: pageSize.value })
    list.value = res.list
    total.value = res.total
  } finally {
    loading.value = false
  }
}

async function doUpload(opt: UploadRequestOptions) {
  uploading.value = true
  try {
    await uploadSiteImage(opt.file as File)
    ElMessage.success('上传成功')
    page.value = 1
    load()
  } catch {
    // 拦截器已提示
  } finally {
    uploading.value = false
  }
}

async function handleDelete(m: SiteMedia) {
  try {
    await ElMessageBox.confirm(`确定删除图片「${m.filename}」吗？`, '删除确认', {
      type: 'warning',
      confirmButtonText: '删除',
      cancelButtonText: '取消',
    })
    await deleteMedia(m.id)
    ElMessage.success('已删除')
    load()
  } catch {
    // 取消或失败
  }
}

function copy(url: string) {
  copyText(url, '图片地址已复制')
}

function formatSize(size: number): string {
  if (!size) return '-'
  if (size < 1024) return `${size} B`
  if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`
  return `${(size / 1024 / 1024).toFixed(2)} MB`
}

onMounted(load)
</script>

<style scoped lang="scss">
.sub-toolbar {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 8px;

  .sub-toolbar-right {
    margin-left: auto;
  }
}

.upload-tip {
  font-size: 12px;
  color: var(--text-secondary);
  margin-bottom: 14px;
}

.media-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
  gap: 14px;
  min-height: 120px;
}

.media-item {
  border: 1px solid var(--el-border-color);
  border-radius: 8px;
  overflow: hidden;
  background: #fff;

  .media-img {
    width: 100%;
    height: 120px;
    display: block;
  }

  .media-meta {
    padding: 6px 8px;

    .media-name {
      font-size: 12px;
      color: var(--text-main);
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }

    .media-size {
      font-size: 12px;
      color: var(--text-secondary);
    }
  }

  .media-actions {
    display: flex;
    justify-content: space-between;
    padding: 0 6px 6px;
  }
}
</style>
