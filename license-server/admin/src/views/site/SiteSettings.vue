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

      <div class="section-title">顶部公告条</div>
      <el-form-item label="启用公告">
        <el-switch v-model="form.announcement_enabled" active-value="1" inactive-value="0" />
      </el-form-item>
      <el-form-item label="公告内容">
        <el-input v-model="form.announcement_text" maxlength="200" placeholder="例如：新版本 v2.0 已发布" />
      </el-form-item>
      <el-form-item label="公告链接">
        <el-input v-model="form.announcement_url" placeholder="可选，点击公告跳转的地址" />
      </el-form-item>

      <div class="section-title">首页文案</div>
      <el-form-item label="主标题">
        <el-input v-model="form.home_hero_title" maxlength="100" />
      </el-form-item>
      <el-form-item label="副标题">
        <el-input v-model="form.home_hero_subtitle" maxlength="200" />
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
import { getSiteSettings, updateSiteSettings } from '@/api/site'

const loading = ref(false)
const saving = ref(false)

const form = reactive({
  site_name: '',
  site_logo: '',
  site_favicon: '',
  primary_color: '#3A7AFE',
  site_description: '',
  site_keywords: '',
  announcement_enabled: '0',
  announcement_text: '',
  announcement_url: '',
  home_hero_title: '',
  home_hero_subtitle: '',
  purchase_url: '',
  github_url: '',
  icp: '',
  police_icp: '',
  copyright: '',
  seo_title: '',
  seo_desc: '',
  statistics_code: '',
})

async function load() {
  loading.value = true
  try {
    const data = await getSiteSettings()
    for (const key of Object.keys(form) as (keyof typeof form)[]) {
      form[key] = data[key] ?? form[key]
    }
  } finally {
    loading.value = false
  }
}

async function save() {
  saving.value = true
  try {
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

.panel-footer {
  padding: 8px 0 4px;
  position: sticky;
  bottom: 0;
  background: #fff;
}
</style>
