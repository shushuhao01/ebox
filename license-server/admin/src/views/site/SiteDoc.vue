<template>
  <div v-loading="loading" class="site-panel">
    <el-alert
      title="使用手册地址填写飞书文档链接后，官网「使用手册」入口点击即可跳转。"
      type="info"
      :closable="false"
      show-icon
      class="panel-alert"
    />

    <el-form :model="form" label-width="130px" class="panel-form">
      <el-form-item label="手册标题">
        <el-input v-model="form.doc_title" maxlength="64" placeholder="例如：使用手册" />
      </el-form-item>
      <el-form-item label="手册地址">
        <el-input v-model="form.doc_url" placeholder="粘贴飞书文档链接，如 https://xxx.feishu.cn/docx/xxxx" />
      </el-form-item>
      <el-form-item label="打开方式">
        <el-radio-group v-model="form.doc_target">
          <el-radio value="_blank">新窗口打开</el-radio>
          <el-radio value="_self">当前窗口打开</el-radio>
        </el-radio-group>
      </el-form-item>
      <el-form-item label="入口预览">
        <el-button v-if="form.doc_url" type="primary" plain :icon="Link" @click="openDoc">
          {{ form.doc_title || '使用手册' }}
        </el-button>
        <span v-else class="field-tip">尚未配置手册地址，官网暂不展示该入口。</span>
      </el-form-item>
    </el-form>

    <div class="panel-footer">
      <el-button type="primary" :loading="saving" @click="save">保存</el-button>
      <el-button @click="load">重置</el-button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { Link } from '@element-plus/icons-vue'
import { getSiteSettings, updateSiteSettings } from '@/api/site'

const loading = ref(false)
const saving = ref(false)

const form = reactive({
  doc_title: '使用手册',
  doc_url: '',
  doc_target: '_blank',
})

async function load() {
  loading.value = true
  try {
    const data = await getSiteSettings()
    form.doc_title = data.doc_title || '使用手册'
    form.doc_url = data.doc_url || ''
    form.doc_target = data.doc_target || '_blank'
  } finally {
    loading.value = false
  }
}

async function save() {
  saving.value = true
  try {
    await updateSiteSettings({ ...form })
    ElMessage.success('使用手册配置已保存')
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

function openDoc() {
  if (!form.doc_url) return
  window.open(form.doc_url, form.doc_target || '_blank')
}

onMounted(load)
</script>

<style scoped lang="scss">
.site-panel {
  max-width: 760px;
}

.panel-alert {
  margin-bottom: 18px;
}

.field-tip {
  font-size: 13px;
  color: var(--text-secondary);
}

.panel-footer {
  padding: 8px 0 4px;
}
</style>
