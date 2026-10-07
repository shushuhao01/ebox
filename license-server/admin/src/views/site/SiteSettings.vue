<template>
  <div v-loading="loading" class="site-panel">
    <el-form :model="form" label-width="130px" class="panel-form">
      <div class="section-title">基本信息</div>
      <el-row :gutter="16">
        <el-col :md="12">
          <el-form-item label="站点名称">
            <el-input v-model="form.site_name" maxlength="64" placeholder="eBox" />
          </el-form-item>
        </el-col>
        <el-col :md="12">
          <el-form-item label="品牌主色">
            <el-color-picker v-model="form.primary_color" />
            <el-input v-model="form.primary_color" class="color-input" placeholder="#3A7AFE" />
          </el-form-item>
        </el-col>
        <el-col :md="12">
          <el-form-item label="Logo 地址">
            <el-input v-model="form.site_logo" placeholder="/logo.png 或图片完整地址" />
          </el-form-item>
        </el-col>
        <el-col :md="12">
          <el-form-item label="Favicon 地址">
            <el-input v-model="form.site_favicon" placeholder="/favicon.ico" />
          </el-form-item>
        </el-col>
        <el-col :span="24">
          <el-form-item label="站点描述">
            <el-input v-model="form.site_description" maxlength="255" />
          </el-form-item>
        </el-col>
        <el-col :span="24">
          <el-form-item label="关键词">
            <el-input v-model="form.site_keywords" maxlength="255" placeholder="多个关键词用英文逗号分隔" />
          </el-form-item>
        </el-col>
      </el-row>

      <div class="section-title">首页文案</div>
      <el-form-item label="主标题">
        <el-input v-model="form.home_hero_title" maxlength="100" />
      </el-form-item>
      <el-form-item label="副标题">
        <el-input v-model="form.home_hero_subtitle" maxlength="200" />
      </el-form-item>

      <div class="section-title">首页配图</div>
      <el-form-item label="轮播配图">
        <div class="hero-images">
          <div v-for="(url, idx) in heroImages" :key="url" class="hero-image-item">
            <img :src="url" class="hero-image-thumb" />
            <div class="hero-image-actions">
              <el-button link size="small" :disabled="idx === 0" @click="moveImage(idx, -1)">上移</el-button>
              <el-button link size="small" :disabled="idx === heroImages.length - 1" @click="moveImage(idx, 1)">下移</el-button>
              <el-button link type="danger" size="small" @click="removeImage(idx)">删除</el-button>
            </div>
          </div>
          <el-upload
            class="hero-image-upload"
            :show-file-list="false"
            :http-request="handleUpload"
            accept="image/png,image/jpeg,image/webp,image/gif"
          >
            <div class="hero-image-add" v-loading="uploading">
              <span v-if="!uploading">+</span>
            </div>
          </el-upload>
        </div>
        <div class="field-tip">
          展示在官网首页标题右侧，支持多张自动轮播，点击图片可放大查看。建议放应用截图或宣传海报，单张不超过 10MB。
        </div>
      </el-form-item>

      <div class="section-title">购买 / 仓库</div>
      <el-form-item label="购买地址">
        <el-input v-model="form.purchase_url" placeholder="点击购买跳转的外部地址（独立发卡站）" />
      </el-form-item>
      <el-form-item label="开源仓库">
        <el-input v-model="form.github_url" placeholder="可选，GitHub 等仓库地址" />
      </el-form-item>

      <div class="section-title">页脚合规信息</div>
      <el-row :gutter="16">
        <el-col :md="12">
          <el-form-item label="ICP 备案号">
            <el-input v-model="form.icp" placeholder="例如：京ICP备00000000号" />
          </el-form-item>
        </el-col>
        <el-col :md="12">
          <el-form-item label="公安备案号">
            <el-input v-model="form.police_icp" placeholder="可选" />
          </el-form-item>
        </el-col>
        <el-col :span="24">
          <el-form-item label="版权信息">
            <el-input v-model="form.copyright" maxlength="200" />
          </el-form-item>
        </el-col>
      </el-row>

      <div class="section-title">SEO</div>
      <el-form-item label="SEO 标题">
        <el-input v-model="form.seo_title" maxlength="200" />
      </el-form-item>
      <el-form-item label="SEO 描述">
        <el-input v-model="form.seo_desc" type="textarea" :rows="2" maxlength="500" show-word-limit />
      </el-form-item>

      <div class="section-title">统计代码</div>
      <el-form-item label="统计脚本">
        <el-input
          v-model="form.statistics_code"
          type="textarea"
          :rows="4"
          placeholder="粘贴第三方统计（如百度统计 / 51LA）的 script 代码"
        />
        <div class="field-tip">该代码会原样插入官网前台页面，请仅填写可信来源的代码。</div>
      </el-form-item>
    </el-form>

    <div class="panel-footer">
      <el-button type="primary" :loading="saving" @click="save">保存设置</el-button>
      <el-button @click="load">重置</el-button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage } from 'element-plus'
import type { UploadRequestOptions } from 'element-plus'
import { getSiteSettings, updateSiteSettings, uploadSiteImage } from '@/api/site'

const loading = ref(false)
const saving = ref(false)
const uploading = ref(false)

const form = reactive({
  site_name: '',
  site_logo: '',
  site_favicon: '',
  primary_color: '#3A7AFE',
  site_description: '',
  site_keywords: '',
  home_hero_title: '',
  home_hero_subtitle: '',
  home_hero_images: '[]',
  purchase_url: '',
  github_url: '',
  icp: '',
  police_icp: '',
  copyright: '',
  seo_title: '',
  seo_desc: '',
  statistics_code: '',
})

/** 首页轮播配图列表（保存时序列化到 form.home_hero_images） */
const heroImages = ref<string[]>([])

function parseImages(raw: string): string[] {
  if (!raw) return []
  try {
    const arr = JSON.parse(raw)
    if (Array.isArray(arr)) return arr.map((u) => String(u)).filter(Boolean)
  } catch {
    // 忽略非法 JSON
  }
  return []
}

async function load() {
  loading.value = true
  try {
    const data = await getSiteSettings()
    for (const key of Object.keys(form) as (keyof typeof form)[]) {
      form[key] = data[key] ?? form[key]
    }
    heroImages.value = parseImages(form.home_hero_images)
  } finally {
    loading.value = false
  }
}

async function handleUpload(options: UploadRequestOptions) {
  uploading.value = true
  try {
    const res = await uploadSiteImage(options.file as File)
    heroImages.value.push(res.url)
    ElMessage.success('图片上传成功')
  } catch {
    // 拦截器已提示
  } finally {
    uploading.value = false
  }
}

function moveImage(index: number, offset: number) {
  const target = index + offset
  if (target < 0 || target >= heroImages.value.length) return
  const list = heroImages.value
  ;[list[index], list[target]] = [list[target], list[index]]
}

function removeImage(index: number) {
  heroImages.value.splice(index, 1)
}

async function save() {
  saving.value = true
  try {
    form.home_hero_images = JSON.stringify(heroImages.value)
    await updateSiteSettings({ ...form })
    ElMessage.success('站点设置已保存')
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

onMounted(load)
</script>

<style scoped lang="scss">
.site-panel {
  max-width: 900px;
}

.section-title {
  margin: 8px 0 14px;
  padding-left: 8px;
  font-size: 15px;
  font-weight: 600;
  color: var(--text-main);
  border-left: 3px solid var(--primary-color);
}

.color-input {
  width: 130px;
  margin-left: 10px;
}

.field-tip {
  font-size: 12px;
  color: var(--text-secondary);
  line-height: 1.6;
}

.hero-images {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
}

.hero-image-item,
.hero-image-add {
  width: 132px;
  height: 99px;
  border-radius: 8px;
  overflow: hidden;
}

.hero-image-item {
  position: relative;
  border: 1px solid var(--border-color, #dcdfe6);
  background: #f5f7fa;
}

.hero-image-thumb {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.hero-image-actions {
  position: absolute;
  left: 0;
  right: 0;
  bottom: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 2px;
  background: rgba(0, 0, 0, 0.55);
}

.hero-image-actions :deep(.el-button) {
  color: #fff;
  --el-button-hover-link-text-color: #fff;
}

.hero-image-add {
  display: flex;
  align-items: center;
  justify-content: center;
  border: 1px dashed var(--border-color, #dcdfe6);
  color: var(--text-secondary);
  font-size: 26px;
  cursor: pointer;
  background: #fafbfc;
  transition: border-color 0.2s ease;
}

.hero-image-add:hover {
  border-color: var(--primary-color);
  color: var(--primary-color);
}

.panel-footer {
  padding: 8px 0 4px;
  position: sticky;
  bottom: 0;
  background: #fff;
}
</style>
