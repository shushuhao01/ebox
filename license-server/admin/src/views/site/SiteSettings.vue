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

      <div class="section-title">搜索引擎主动推送</div>
      <el-form-item label="站点主域">
        <el-input v-model="form.site_origin" placeholder="https://www.abc222.cn" />
        <div class="field-tip">用于生成 sitemap 与推送的绝对地址，需与官网正式主域一致（不带末尾斜杠）。</div>
      </el-form-item>
      <el-form-item label="自动推送">
        <el-switch v-model="pushEnabled" active-text="新建/发布文章时自动推送" inactive-text="关闭" />
      </el-form-item>
      <el-form-item label="百度推送 Token">
        <el-input v-model="form.baidu_push_token" placeholder="只填 token 本身，如 lyJwLQxAFqZkFMCX" />
        <div class="field-tip">
          在百度搜索资源平台「普通收录 → API 提交」获取 token。<strong>只填 token 本身</strong>（不要连接口地址一起粘贴），
          且需与「站点主域」在百度验证的站点完全一致（www 与非 www 是两个站点，token 不通用）。
        </div>
      </el-form-item>
      <el-form-item label="IndexNow Key">
        <el-input v-model="form.indexnow_key" placeholder="16~128 位十六进制字符串" />
        <div class="field-tip">
          用于 Bing / Yandex 等 IndexNow 协议校验，密钥文件随官网静态资源一起发布（<code>website/public/&lt;key&gt;.txt</code>）。
          修改后需重新构建并部署官网才会生效。
        </div>
      </el-form-item>
      <el-form-item label="站点地图">
        <el-space>
          <el-button :loading="pushing" @click="pushAll">一键推送全站</el-button>
          <el-button link type="primary" @click="openSitemap">查看 sitemap.xml</el-button>
        </el-space>
        <div class="field-tip">
          提交 sitemap 到百度/必应站长平台，或在新增一批文章后手动触发一次全站推送。
        </div>
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
import { ElMessage, ElNotification } from 'element-plus'
import type { UploadRequestOptions } from 'element-plus'
import { getSiteSettings, updateSiteSettings, uploadSiteImage, pushSeoAll } from '@/api/site'

const loading = ref(false)
const saving = ref(false)
const uploading = ref(false)
const pushing = ref(false)

/** 是否开启新建/发布文章时自动推送（存储为字符串 '1'/'0'） */
const pushEnabled = ref(true)

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
  site_origin: '',
  baidu_push_token: '',
  indexnow_key: '',
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
    pushEnabled.value = (data.seo_push_enabled ?? '1') !== '0'
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
    await updateSiteSettings({ ...form, seo_push_enabled: pushEnabled.value ? '1' : '0' })
    ElMessage.success('站点设置已保存')
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

/** 一键推送全站 URL 到百度 / IndexNow */
async function pushAll() {
  pushing.value = true
  try {
    const r = await pushSeoAll()
    const label: Record<string, string> = { ok: '成功', fail: '失败', skipped: '未配置' }
    const detail = (status: string, msg?: string) =>
      status === 'fail' && msg ? `${label[status]}（${msg}）` : label[status] ?? status
    const text = `已推送 ${r.total} 条：百度 ${detail(r.baidu, r.baiduMessage)}，IndexNow ${detail(r.indexnow, r.indexnowMessage)}`
    if (r.baidu === 'fail' || r.indexnow === 'fail') {
      ElNotification({ title: '推送结果', message: text, type: 'warning', duration: 8000, showClose: true })
    } else {
      ElMessage.success(text)
    }
  } catch {
    // 拦截器已提示
  } finally {
    pushing.value = false
  }
}

/** 新窗口查看 sitemap.xml */
function openSitemap() {
  const origin = (form.site_origin || 'https://www.abc222.cn').replace(/\/+$/, '')
  window.open(`${origin}/sitemap.xml`, '_blank')
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
