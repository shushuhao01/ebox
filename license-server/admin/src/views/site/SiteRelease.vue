<template>
  <div v-loading="loading" class="site-panel">
    <el-alert
      title="官网「下载」页的主下载按钮。上传安装包后，官网直下使用此处配置的版本号 / 更新日志 / 文件；未上传时自动回退到 GitHub Release 生成的 update.json。"
      type="info"
      :closable="false"
      show-icon
      class="panel-alert"
    />

    <div class="section-title">当前安装包</div>
    <div v-if="form.release_file_url" class="pkg-card">
      <div class="pkg-name">{{ form.release_file_name || form.release_file_url }}</div>
      <ul class="pkg-meta">
        <li>
          <span>文件大小</span>{{ formatSize(Number(form.release_file_size)) }}
        </li>
        <li>
          <span>下载路径</span>
          <code class="pkg-path">{{ form.release_file_url }}</code>
          <el-button link type="primary" size="small" @click="copy(form.release_file_url)">复制</el-button>
        </li>
        <li>
          <span>SHA-256</span>
          <code class="pkg-path">{{ form.release_file_sha256 || '-' }}</code>
          <el-button
            v-if="form.release_file_sha256"
            link
            type="primary"
            size="small"
            @click="copy(form.release_file_sha256)"
          >复制</el-button>
        </li>
      </ul>
    </div>
    <el-empty v-else :image-size="70" description="尚未上传安装包，官网将回退使用 GitHub Release" />

    <div class="upload-row">
      <el-upload :show-file-list="false" :http-request="doUpload" accept=".exe,.zip">
        <el-button type="primary" :icon="Upload" :loading="uploading">上传安装包（.exe / .zip）</el-button>
      </el-upload>
      <span class="upload-tip">上传后立即成为官网主下载文件，并自动替换旧安装包。</span>
    </div>

    <div class="section-title">版本信息</div>
    <el-form :model="form" label-width="110px" class="panel-form">
      <el-form-item label="版本号">
        <el-input v-model="form.release_version" maxlength="64" placeholder="例如：3.1.4" style="max-width: 320px" />
      </el-form-item>
      <el-form-item label="更新日期">
        <el-date-picker
          v-model="form.release_date"
          type="date"
          value-format="YYYY-MM-DD"
          placeholder="选择日期"
          style="width: 220px"
        />
      </el-form-item>
      <el-form-item label="更新日志">
        <el-input
          v-model="changelogText"
          type="textarea"
          :rows="6"
          placeholder="每行一条，例如：&#10;新增：支持 xxx&#10;优化：xxx 体验&#10;修复：xxx 问题"
        />
        <div class="field-tip">每行一条，保存后按顺序展示在官网「更新日志」。</div>
      </el-form-item>
    </el-form>

    <div class="panel-footer">
      <el-button type="primary" :loading="saving" @click="save">保存版本信息</el-button>
      <el-button @click="load">重置</el-button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage, type UploadRequestOptions } from 'element-plus'
import { Upload } from '@element-plus/icons-vue'
import {
  getReleaseConfig,
  saveReleaseConfig,
  uploadReleaseFile,
  type ReleaseConfig,
} from '@/api/site'
import { copyText } from '@/utils/format'

const loading = ref(false)
const saving = ref(false)
const uploading = ref(false)
const changelogText = ref('')

const form = reactive<ReleaseConfig>({
  release_version: '',
  release_date: '',
  release_changelog: '[]',
  release_file_url: '',
  release_file_name: '',
  release_file_size: '0',
  release_file_sha256: '',
})

function parseChangelog(v: string): string[] {
  if (!v) return []
  try {
    const parsed = JSON.parse(v)
    if (Array.isArray(parsed)) return parsed.map((x) => String(x))
  } catch {
    // 非 JSON，按行拆分
  }
  return v.split('\n').map((s) => s.trim()).filter(Boolean)
}

function fill(data: ReleaseConfig) {
  Object.assign(form, data)
  changelogText.value = parseChangelog(data.release_changelog).join('\n')
}

async function load() {
  loading.value = true
  try {
    fill(await getReleaseConfig())
  } finally {
    loading.value = false
  }
}

async function save() {
  saving.value = true
  try {
    const changelog = changelogText.value.split('\n').map((s) => s.trim()).filter(Boolean)
    const data = await saveReleaseConfig({
      release_version: form.release_version,
      release_date: form.release_date,
      release_changelog: JSON.stringify(changelog),
    })
    fill(data)
    ElMessage.success('已保存')
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

async function doUpload(opt: UploadRequestOptions) {
  uploading.value = true
  try {
    const data = await uploadReleaseFile(opt.file as File)
    fill(data)
    ElMessage.success('安装包已上传')
  } catch {
    // 拦截器已提示
  } finally {
    uploading.value = false
  }
}

function formatSize(bytes: number): string {
  if (!bytes || bytes <= 0) return '-'
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(2)} MB`
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`
}

function copy(text: string) {
  copyText(text, '已复制')
}

onMounted(load)
</script>

<style scoped lang="scss">
.site-panel {
  max-width: 900px;
}

.panel-alert {
  margin-bottom: 18px;
}

.section-title {
  margin: 14px 0 14px;
  padding-left: 8px;
  font-size: 15px;
  font-weight: 600;
  color: var(--text-main);
  border-left: 3px solid var(--primary-color);
}

.pkg-card {
  border: 1px solid var(--el-border-color);
  border-radius: 8px;
  padding: 14px 16px;
  background: var(--el-fill-color-lighter);

  .pkg-name {
    font-weight: 600;
    color: var(--text-main);
    word-break: break-all;
  }
}

.pkg-meta {
  margin-top: 10px;
  display: flex;
  flex-direction: column;
  gap: 8px;

  li {
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 13px;
    color: var(--text-main);
    flex-wrap: wrap;
  }

  li > span {
    min-width: 72px;
    color: var(--text-secondary);
  }
}

.pkg-path {
  font-family: 'JetBrains Mono', Consolas, monospace;
  font-size: 12px;
  color: var(--text-secondary);
  word-break: break-all;
}

.upload-row {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-top: 14px;
}

.upload-tip {
  font-size: 12px;
  color: var(--text-secondary);
}

.field-tip {
  font-size: 12px;
  color: var(--text-secondary);
  line-height: 1.6;
}

.panel-footer {
  padding: 16px 0 4px;
  position: sticky;
  bottom: 0;
  background: #fff;
}
</style>
