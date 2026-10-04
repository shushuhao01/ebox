<template>
  <div class="rich-editor" :class="{ disabled }">
    <div class="re-toolbar">
      <button type="button" title="标题1" @mousedown.prevent="exec('formatBlock', 'H1')">H1</button>
      <button type="button" title="标题2" @mousedown.prevent="exec('formatBlock', 'H2')">H2</button>
      <button type="button" title="标题3" @mousedown.prevent="exec('formatBlock', 'H3')">H3</button>
      <button type="button" title="正文" @mousedown.prevent="exec('formatBlock', 'P')">正文</button>
      <span class="re-sep"></span>
      <button type="button" title="加粗" @mousedown.prevent="exec('bold')"><b>B</b></button>
      <button type="button" title="斜体" @mousedown.prevent="exec('italic')"><i>I</i></button>
      <button type="button" title="下划线" @mousedown.prevent="exec('underline')"><u>U</u></button>
      <button type="button" title="删除线" @mousedown.prevent="exec('strikeThrough')"><s>S</s></button>
      <span class="re-sep"></span>
      <button type="button" title="无序列表" @mousedown.prevent="exec('insertUnorderedList')">• 列表</button>
      <button type="button" title="有序列表" @mousedown.prevent="exec('insertOrderedList')">1. 列表</button>
      <button type="button" title="引用" @mousedown.prevent="exec('formatBlock', 'BLOCKQUOTE')">引用</button>
      <button type="button" title="代码块" @mousedown.prevent="exec('formatBlock', 'PRE')">代码</button>
      <span class="re-sep"></span>
      <button type="button" title="左对齐" @mousedown.prevent="exec('justifyLeft')">左</button>
      <button type="button" title="居中" @mousedown.prevent="exec('justifyCenter')">中</button>
      <button type="button" title="右对齐" @mousedown.prevent="exec('justifyRight')">右</button>
      <span class="re-sep"></span>
      <button type="button" title="插入链接" @mousedown.prevent="insertLink">链接</button>
      <button type="button" title="上传图片" @mousedown.prevent="pickImage">
        {{ uploading ? '上传中…' : '图片' }}
      </button>
      <button type="button" title="清除格式" @mousedown.prevent="exec('removeFormat')">清除</button>
      <input ref="fileInput" type="file" accept="image/*" style="display: none" @change="onPickImage" />
    </div>
    <div
      ref="editable"
      class="re-body"
      :contenteditable="!disabled"
      @input="emitContent"
      @blur="emitContent"
      @paste="onPaste"
    ></div>
    <div class="re-tip">支持标题 / 加粗 / 斜体 / 列表 / 引用 / 表格 / 链接 / 对齐；可上传图片，或直接粘贴图片（自动上传）</div>
  </div>
</template>

<script setup lang="ts">
import { onMounted, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { uploadSiteImage } from '@/api/site'

const props = withDefaults(
  defineProps<{ modelValue?: string; disabled?: boolean }>(),
  { modelValue: '', disabled: false },
)
const emit = defineEmits<{ (e: 'update:modelValue', v: string): void }>()

const editable = ref<HTMLDivElement | null>(null)
const fileInput = ref<HTMLInputElement | null>(null)
const uploading = ref(false)

function emitContent() {
  if (!editable.value) return
  const html = editable.value.innerHTML
  if (html !== props.modelValue) emit('update:modelValue', html)
}

function exec(command: string, value?: string) {
  if (props.disabled) return
  editable.value?.focus()
  // execCommand 已标记废弃但浏览器仍普遍支持；此处无需第三方编辑器依赖
  document.execCommand(command, false, value)
  emitContent()
}

function insertLink() {
  if (props.disabled) return
  const url = window.prompt('请输入链接地址', 'https://')
  if (!url) return
  exec('createLink', url)
}

function pickImage() {
  if (props.disabled) return
  fileInput.value?.click()
}

async function onPickImage(e: Event) {
  const input = e.target as HTMLInputElement
  const file = input.files?.[0]
  input.value = ''
  if (!file) return
  if (file.size > 10 * 1024 * 1024) {
    ElMessage.error('图片大小不能超过 10MB')
    return
  }
  uploading.value = true
  try {
    const res = await uploadSiteImage(file)
    editable.value?.focus()
    document.execCommand('insertHTML', false, `<img src="${res.url}" style="max-width:100%;height:auto;" />`)
    emitContent()
    ElMessage.success('图片已插入')
  } catch {
    // 拦截器已提示
  } finally {
    uploading.value = false
  }
}

/** 匹配 src 为 base64 内联图的 <img> 标签 */
const DATA_IMG_RE = /<img\b[^>]*\bsrc\s*=\s*["'](data:image\/[^"']+)["'][^>]*>/gi

/** 将 base64 图片 DataURL 转为 File，便于走上传接口 */
function dataUrlToFile(dataUrl: string): File | null {
  const m = /^data:([^;,]+);base64,(.*)$/is.exec(dataUrl.trim())
  if (!m) return null
  try {
    const bin = atob(m[2])
    const bytes = new Uint8Array(bin.length)
    for (let i = 0; i < bin.length; i += 1) bytes[i] = bin.charCodeAt(i)
    const type = m[1] || 'image/png'
    const ext = (type.split('/')[1] || 'png').replace('+xml', '')
    return new File([bytes], `pasted-${Date.now()}.${ext}`, { type })
  } catch {
    return null
  }
}

/** 上传单张图片，失败返回 null（拦截器已提示） */
async function uploadImageFile(file: File): Promise<string | null> {
  if (file.size > 10 * 1024 * 1024) {
    ElMessage.error('图片大小不能超过 10MB')
    return null
  }
  try {
    const res = await uploadSiteImage(file)
    return res.url
  } catch {
    return null
  }
}

/**
 * 拦截粘贴：把剪贴板里的图片（截图 / 复制的图片 / 富文本中的 base64 内联图）
 * 统一走上传接口转为短地址再插入，避免 base64 撑爆请求体。
 */
async function onPaste(e: ClipboardEvent) {
  if (props.disabled) return
  const dt = e.clipboardData
  if (!dt) return

  const html = dt.getData('text/html') || ''
  // 剪贴板中的图片文件（截图 / 复制图片）
  const fileList: File[] = []
  for (const item of Array.from(dt.items || [])) {
    if (item.kind === 'file' && item.type.startsWith('image/')) {
      const f = item.getAsFile()
      if (f) fileList.push(f)
    }
  }
  // 富文本里内联的 base64 图片
  const inlineDataUrls: string[] = []
  if (html) {
    DATA_IMG_RE.lastIndex = 0
    let m: RegExpExecArray | null
    while ((m = DATA_IMG_RE.exec(html)) !== null) inlineDataUrls.push(m[1])
  }
  // 无图片则走浏览器默认粘贴
  if (!fileList.length && !inlineDataUrls.length) return

  e.preventDefault()
  uploading.value = true
  try {
    const toImg = (url: string) => `<img src="${url}" style="max-width:100%;height:auto;" />`

    // 上传剪贴板图片文件
    const fileUrls: string[] = []
    for (const f of fileList) {
      const url = await uploadImageFile(f)
      if (url) fileUrls.push(url)
    }
    // 上传 base64 内联图，建立 原地址 → 新地址 映射（同一张图只传一次）
    const urlMap = new Map<string, string>()
    for (const dataUrl of inlineDataUrls) {
      if (urlMap.has(dataUrl)) continue
      const f = dataUrlToFile(dataUrl)
      const url = f ? await uploadImageFile(f) : null
      if (url) urlMap.set(dataUrl, url)
    }
    if (!fileUrls.length && !urlMap.size) return

    // 截图等纯图片：直接插入；含富文本：保留原文并把内联图替换为上传后的地址
    let out = fileUrls.map(toImg).join('')
    if (html && urlMap.size) {
      out += html.replace(DATA_IMG_RE, (_all, dataUrl: string) => {
        const url = urlMap.get(dataUrl)
        return url ? toImg(url) : ''
      })
    }
    editable.value?.focus()
    document.execCommand('insertHTML', false, out)
    emitContent()
    ElMessage.success('图片已上传并插入')
  } finally {
    uploading.value = false
  }
}

watch(
  () => props.modelValue,
  (val) => {
    if (editable.value && editable.value.innerHTML !== (val || '')) {
      editable.value.innerHTML = val || ''
    }
  },
)

onMounted(() => {
  if (editable.value) editable.value.innerHTML = props.modelValue || ''
})
</script>

<style scoped lang="scss">
.rich-editor {
  border: 1px solid var(--el-border-color);
  border-radius: 6px;
  overflow: hidden;

  &.disabled {
    opacity: 0.7;
  }
}

.re-toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  padding: 6px 8px;
  background: #f7f8fa;
  border-bottom: 1px solid var(--el-border-color);

  button {
    min-width: 30px;
    height: 28px;
    padding: 0 8px;
    font-size: 13px;
    line-height: 1;
    color: var(--text-main, #303133);
    background: #fff;
    border: 1px solid var(--el-border-color);
    border-radius: 4px;
    cursor: pointer;

    &:hover {
      color: var(--primary-color, #3a7afe);
      border-color: var(--primary-color, #3a7afe);
    }
  }

  .re-sep {
    width: 1px;
    height: 20px;
    margin: 4px 2px;
    background: var(--el-border-color);
  }
}

.re-body {
  min-height: 280px;
  max-height: 520px;
  overflow-y: auto;
  padding: 12px 14px;
  font-size: 14px;
  line-height: 1.7;
  color: var(--text-main, #303133);
  outline: none;

  &:empty::before {
    content: '请输入正文…';
    color: #a8abb2;
  }

  :deep(img) {
    max-width: 100%;
    height: auto;
  }

  :deep(blockquote) {
    margin: 8px 0;
    padding: 4px 12px;
    color: #606266;
    border-left: 3px solid var(--primary-color, #3a7afe);
    background: #f7f8fa;
  }

  :deep(pre) {
    padding: 10px 12px;
    background: #f5f5f5;
    border-radius: 4px;
    overflow-x: auto;
  }

  :deep(table) {
    border-collapse: collapse;
    width: 100%;

    td,
    th {
      border: 1px solid var(--el-border-color);
      padding: 6px 8px;
    }
  }
}

.re-tip {
  padding: 4px 10px 6px;
  font-size: 12px;
  color: var(--text-secondary, #909399);
  background: #f7f8fa;
  border-top: 1px solid var(--el-border-color);
}
</style>
