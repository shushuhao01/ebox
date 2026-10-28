<template>
  <div class="page-container">
    <el-card shadow="never" class="settings-card">
      <el-tabs v-model="activeTab">
        <!-- 授权策略 -->
        <el-tab-pane label="授权策略" name="policy">
          <div class="tab-body">
            <div class="form-item">
              <div class="form-label">
                心跳间隔 <span class="hint">（1 ~ 24 小时，当前 {{ policy.heartbeat_interval_hours }} 小时）</span>
              </div>
              <el-slider
                v-model="policy.heartbeat_interval_hours"
                :min="1"
                :max="24"
                :marks="{ 1: '1h', 6: '6h', 12: '12h', 24: '24h' }"
                style="width: 480px; max-width: 100%"
              />
            </div>

            <el-divider />

            <div class="form-item">
              <div class="form-label">强制在线激活</div>
              <el-switch
                v-model="forceOnline"
                active-text="开启"
                inactive-text="关闭"
                inline-prompt
              />
              <span class="hint">开启后，激活时必须在线校验</span>
            </div>

            <el-divider />

            <div class="form-item">
              <div class="form-label">心跳漏报宽限</div>
              <el-input-number v-model="policy.online_threshold_minutes" :min="1" :max="1440" />
              <span class="hint">
                设备最后心跳距今不超过「在线窗口」即视为在线；在线窗口 = 心跳间隔 + 宽限 = 当前 {{ onlineWindowMin }} 分钟（分钟）
              </span>
            </div>

            <el-divider />

            <div class="form-item">
              <div class="form-label">
                系统公告
                <span class="hint">客户端首页公告栏；保存草稿不会推送，需「立即发送」或设置定时发送</span>
              </div>

              <div class="notice-status">
                <template v-if="publishedNotice">
                  <el-tag type="success" size="small" effect="dark">已发布</el-tag>
                  <span class="notice-status-text">《{{ publishedNotice.title || '无标题' }}》{{ formatTime(publishedNotice.publishedAt) }}</span>
                </template>
                <el-tag v-else type="info" size="small">未发布</el-tag>

                <template v-if="pendingNotice">
                  <el-tag type="warning" size="small" effect="dark">待定时</el-tag>
                  <span class="notice-status-text">{{ formatTime(pendingNotice.publishAt) }} 自动发送</span>
                  <el-button link type="warning" @click="handleCancelSchedule">取消定时</el-button>
                </template>
              </div>

              <el-input
                v-model="noticeForm.title"
                maxlength="80"
                show-word-limit
                placeholder="公告标题（客户端公告栏显示，最长 2 行）"
                style="width: 480px; max-width: 100%; margin-bottom: 10px"
              />

              <el-input
                v-model="noticeForm.content"
                type="textarea"
                :rows="5"
                maxlength="2000"
                show-word-limit
                placeholder="公告详情（可选）：填写后客户端点击公告栏弹出详情窗口"
                style="width: 480px; max-width: 100%"
              />

              <div class="notice-options">
                <span class="notice-option-label">强提醒</span>
                <el-switch v-model="noticeForm.strong" active-text="开启" inactive-text="关闭" inline-prompt />
                <span class="hint">开启后公告栏以深红底白字高亮，提醒用户注意</span>
              </div>

              <div class="notice-options">
                <span class="notice-option-label">跳转链接</span>
                <el-input
                  v-model="noticeForm.link"
                  maxlength="500"
                  placeholder="https://（可选，配置后客户端详情窗口显示「查看详情」）"
                  style="width: 360px; max-width: 100%"
                />
              </div>

              <div class="notice-options">
                <span class="notice-option-label">发送方式</span>
                <el-radio-group v-model="sendMode">
                  <el-radio-button label="now">立即发送</el-radio-button>
                  <el-radio-button label="schedule">定时发送</el-radio-button>
                </el-radio-group>
                <el-date-picker
                  v-if="sendMode === 'schedule'"
                  v-model="sendAt"
                  type="datetime"
                  placeholder="选择发送时间"
                  format="YYYY-MM-DD HH:mm:ss"
                  value-format="YYYY-MM-DD HH:mm:ss"
                  style="width: 200px"
                />
              </div>

              <div class="notice-preview-wrap">
                <span class="notice-option-label">效果预览</span>
                <div class="notice-preview" :class="{ strong: noticeForm.strong }">
                  <span class="np-tag">公告</span>
                  <span class="np-text">{{ previewTitle }}</span>
                  <span v-if="noticeForm.content || noticeForm.link" class="np-action">
                    {{ noticeForm.content ? '查看详情' : '查看' }}
                  </span>
                </div>
              </div>

              <div class="notice-actions">
                <el-button :loading="noticeSaving" @click="handleSaveDraft">保存草稿</el-button>
                <el-button
                  v-if="sendMode === 'now'"
                  type="primary"
                  :icon="Promotion"
                  :loading="noticeSending"
                  @click="handleSendNotice"
                >
                  立即发送
                </el-button>
                <el-button v-else type="primary" :icon="Timer" :loading="noticeSending" @click="handleScheduleNotice">
                  定时发送
                </el-button>
                <el-button
                  type="danger"
                  plain
                  :disabled="!publishedNotice && !pendingNotice"
                  :loading="noticeWithdrawing"
                  @click="handleWithdraw"
                >
                  撤销公告
                </el-button>
              </div>
            </div>

            <el-divider />

            <div class="form-item">
              <div class="form-label">
                日志自动清理 <span class="hint">操作日志与心跳日志超过保留天数后自动删除，避免日志堆积占用磁盘</span>
              </div>
              <div class="clean-row">
                <span class="clean-label">保留</span>
                <el-input-number v-model="policy.log_retention_days" :min="1" :max="365" style="width: 120px" />
                <span class="clean-label">天</span>
                <span class="clean-label">每日</span>
                <el-time-picker
                  v-model="policy.log_clean_time"
                  format="HH:mm"
                  value-format="HH:mm"
                  placeholder="清理时间"
                  style="width: 120px"
                />
                <span class="clean-label">自动清理过期日志</span>
                <el-button :icon="Delete" :loading="cleaning" @click="handleCleanLogs">立即清理</el-button>
              </div>
            </div>

            <el-divider />

            <div class="form-item">
              <el-button type="primary" :icon="Check" :loading="saving" @click="savePolicy">保存设置</el-button>
              <el-button :icon="RefreshLeft" @click="loadPolicy">重置</el-button>
            </div>
          </div>
        </el-tab-pane>

        <!-- 账号安全 -->
        <el-tab-pane label="账号安全" name="account">
          <div class="tab-body">
            <el-form ref="pwdFormRef" :model="pwdForm" :rules="pwdRules" label-width="90px" style="max-width: 420px">
              <el-form-item label="旧密码" prop="oldPassword">
                <el-input v-model="pwdForm.oldPassword" type="password" show-password placeholder="请输入原密码" />
              </el-form-item>
              <el-form-item label="新密码" prop="newPassword">
                <el-input v-model="pwdForm.newPassword" type="password" show-password placeholder="至少 6 位" />
              </el-form-item>
              <el-form-item label="确认密码" prop="confirm">
                <el-input v-model="pwdForm.confirm" type="password" show-password placeholder="再次输入新密码" />
              </el-form-item>
              <el-form-item>
                <el-button type="primary" :icon="Lock" :loading="pwdSaving" @click="submitPassword">修改密码</el-button>
              </el-form-item>
            </el-form>
          </div>
        </el-tab-pane>
      </el-tabs>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox, type FormInstance, type FormRules } from 'element-plus'
import { Check, RefreshLeft, Lock, Delete, Promotion, Timer } from '@element-plus/icons-vue'
import {
  getConfig,
  updateConfig,
  cleanLogs,
  sendNotice,
  scheduleNotice,
  cancelScheduledNotice,
  withdrawNotice,
  type SystemConfig,
  type NoticePayload,
  type PendingNotice,
  type NoticeState,
} from '@/api/config'
import { changePassword } from '@/api/users'
import { useUserStore } from '@/stores/user'

const activeTab = ref('policy')

// ============ 授权策略 ============
const policy = reactive<SystemConfig>({
  heartbeat_interval_hours: '6',
  offline_grace_days: '7',
  force_online_activate: '0',
  notice: '',
  notice_title: '',
  notice_content: '',
  notice_strong: '0',
  notice_link: '',
  notice_published: null,
  notice_pending_task: null,
  online_threshold_minutes: '30',
  log_retention_days: '1',
  log_clean_time: '03:00',
})

const forceOnline = ref(false)
const saving = ref(false)
const cleaning = ref(false)

// ============ 系统公告 ============
const noticeForm = reactive({ title: '', content: '', strong: false, link: '' })
const sendMode = ref<'now' | 'schedule'>('now')
const sendAt = ref('')
const publishedNotice = ref<NoticePayload | null>(null)
const pendingNotice = ref<PendingNotice | null>(null)
const noticeSaving = ref(false)
const noticeSending = ref(false)
const noticeWithdrawing = ref(false)

// 预览标题：标题优先，无标题时取正文首个非空行（与客户端横幅一致）
const previewTitle = computed(
  () =>
    noticeForm.title.trim() ||
    noticeForm.content.split(/\r?\n/).find((s) => s.trim())?.trim() ||
    '公告标题',
)

function formatTime(ts: number): string {
  if (!ts) return '—'
  const d = new Date(ts)
  const p = (n: number) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`
}

function applyNoticeState(state: NoticeState) {
  publishedNotice.value = state.notice_published ?? null
  pendingNotice.value = state.notice_pending_task ?? null
}

// 与后端 getOnlineWindowMinutes 保持一致：在线窗口 = 心跳间隔*60 + 宽限
const onlineWindowMin = computed(
  () => (Number(policy.heartbeat_interval_hours) || 6) * 60 + (Number(policy.online_threshold_minutes) || 30),
)

async function loadPolicy() {
  try {
    const data = await getConfig()
    policy.heartbeat_interval_hours = data.heartbeat_interval_hours ?? '6'
    policy.offline_grace_days = data.offline_grace_days ?? '7'
    policy.force_online_activate = data.force_online_activate ?? '0'
    policy.notice = data.notice ?? ''
    policy.online_threshold_minutes = data.online_threshold_minutes ?? '30'
    policy.log_retention_days = data.log_retention_days ?? '1'
    policy.log_clean_time = data.log_clean_time ?? '03:00'
    forceOnline.value = policy.force_online_activate === '1'

    noticeForm.title = data.notice_title ?? ''
    noticeForm.content = data.notice_content ?? ''
    noticeForm.strong = data.notice_strong === '1'
    noticeForm.link = data.notice_link ?? ''
    applyNoticeState(data)
    // 老数据迁移：只有已发布公告（历史 notice）时，回填到草稿便于继续编辑
    if (!noticeForm.title && !noticeForm.content && data.notice_published) {
      noticeForm.title = data.notice_published.title
      noticeForm.content = data.notice_published.content
      noticeForm.strong = data.notice_published.strong
      noticeForm.link = data.notice_published.link
    }
  } catch {
    // 拦截器已提示
  }
}

async function savePolicy() {
  saving.value = true
  try {
    await updateConfig({
      heartbeat_interval_hours: String(policy.heartbeat_interval_hours),
      force_online_activate: forceOnline.value ? '1' : '0',
      online_threshold_minutes: String(policy.online_threshold_minutes),
      log_retention_days: String(policy.log_retention_days),
      log_clean_time: policy.log_clean_time,
    })
    ElMessage.success('系统设置已保存')
    await loadPolicy()
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

function buildNoticeBody() {
  return {
    title: noticeForm.title.trim(),
    content: noticeForm.content,
    strong: noticeForm.strong,
    link: noticeForm.link.trim(),
  }
}

function noticeContentEmpty(): boolean {
  return !noticeForm.title.trim() && !noticeForm.content.trim()
}

async function handleSaveDraft() {
  noticeSaving.value = true
  try {
    await updateConfig({
      notice_title: noticeForm.title,
      notice_content: noticeForm.content,
      notice_strong: noticeForm.strong ? '1' : '0',
      notice_link: noticeForm.link.trim(),
    })
    ElMessage.success('公告草稿已保存（未推送到客户端）')
  } catch {
    // 拦截器已提示
  } finally {
    noticeSaving.value = false
  }
}

async function handleSendNotice() {
  if (noticeContentEmpty()) {
    ElMessage.warning('请填写公告标题或内容')
    return
  }
  noticeSending.value = true
  try {
    applyNoticeState(await sendNotice(buildNoticeBody()))
    ElMessage.success('公告已发送，在线客户端将立即更新')
  } catch {
    // 拦截器已提示
  } finally {
    noticeSending.value = false
  }
}

async function handleScheduleNotice() {
  if (noticeContentEmpty()) {
    ElMessage.warning('请填写公告标题或内容')
    return
  }
  if (!sendAt.value) {
    ElMessage.warning('请选择发送时间')
    return
  }
  const ts = new Date(sendAt.value.replace(/-/g, '/')).getTime()
  if (!ts || ts <= Date.now()) {
    ElMessage.warning('发送时间需晚于当前时间')
    return
  }
  noticeSending.value = true
  try {
    applyNoticeState(await scheduleNotice({ ...buildNoticeBody(), sendAt: new Date(ts).toISOString() }))
    ElMessage.success(`已设置定时发送：${formatTime(ts)}`)
  } catch {
    // 拦截器已提示
  } finally {
    noticeSending.value = false
  }
}

async function handleCancelSchedule() {
  try {
    await ElMessageBox.confirm('确定取消已设置的定时发送吗？', '取消定时发送', {
      type: 'warning',
      confirmButtonText: '取消定时',
      cancelButtonText: '返回',
    })
  } catch {
    return
  }
  try {
    applyNoticeState(await cancelScheduledNotice())
    ElMessage.success('已取消定时发送')
  } catch {
    // 拦截器已提示
  }
}

async function handleWithdraw() {
  try {
    await ElMessageBox.confirm('撤销后所有客户端将立即隐藏公告栏，确定撤销吗？', '撤销公告', {
      type: 'warning',
      confirmButtonText: '撤销',
      cancelButtonText: '取消',
    })
  } catch {
    return
  }
  noticeWithdrawing.value = true
  try {
    applyNoticeState(await withdrawNotice())
    ElMessage.success('公告已撤销')
  } catch {
    // 拦截器已提示
  } finally {
    noticeWithdrawing.value = false
  }
}

// ============ 操作日志立即清理 ============
async function handleCleanLogs() {
  const days = Number(policy.log_retention_days) || 1
  try {
    await ElMessageBox.confirm(`将删除超过 ${days} 天的所有操作日志，确定立即清理吗？`, '清理过期日志', {
      type: 'warning',
      confirmButtonText: '立即清理',
      cancelButtonText: '取消',
    })
  } catch {
    return // 用户取消
  }
  cleaning.value = true
  try {
    const res = await cleanLogs()
    ElMessage.success(`已清理 ${res.operationLogs} 条操作日志、${res.heartbeats} 条心跳日志`)
  } catch {
    // 拦截器已提示
  } finally {
    cleaning.value = false
  }
}

// ============ 修改密码 ============
const store = useUserStore()
const pwdFormRef = ref<FormInstance>()
const pwdSaving = ref(false)
const pwdForm = reactive({ oldPassword: '', newPassword: '', confirm: '' })

const pwdRules: FormRules = {
  oldPassword: [{ required: true, message: '请输入原密码', trigger: 'blur' }],
  newPassword: [
    { required: true, message: '请输入新密码', trigger: 'blur' },
    { min: 6, message: '新密码至少 6 位', trigger: 'blur' },
  ],
  confirm: [
    { required: true, message: '请再次输入新密码', trigger: 'blur' },
    {
      validator: (_r, v, cb) => {
        if (v !== pwdForm.newPassword) cb(new Error('两次输入的密码不一致'))
        else cb()
      },
      trigger: 'blur',
    },
  ],
}

async function submitPassword() {
  if (!pwdFormRef.value) return
  const valid = await pwdFormRef.value.validate().catch(() => false)
  if (!valid) return
  pwdSaving.value = true
  try {
    await changePassword({ oldPassword: pwdForm.oldPassword, newPassword: pwdForm.newPassword })
    ElMessage.success('密码修改成功，请重新登录')
    pwdForm.oldPassword = ''
    pwdForm.newPassword = ''
    pwdForm.confirm = ''
    store.logout()
    // 跳转登录
    window.location.href = '/login'
  } catch {
    // 拦截器已提示
  } finally {
    pwdSaving.value = false
  }
}

onMounted(loadPolicy)
</script>

<style scoped lang="scss">
.settings-card {
  border-radius: var(--card-radius);
  box-shadow: var(--card-shadow);
  min-height: 480px;

  :deep(.el-tabs__header) {
    margin-bottom: 0;
    padding: 0 20px;
    border-bottom: 1px solid #f0f2f7;
  }
}

.tab-body {
  padding: 24px 32px 32px;
}

.form-item {
  .form-label {
    font-size: 14px;
    font-weight: 600;
    color: var(--text-main);
    margin-bottom: 12px;
  }

  .hint {
    font-size: 12px;
    font-weight: 400;
    color: var(--text-secondary);
    margin-left: 10px;
  }

  .clean-row {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;

    .clean-label {
      font-size: 13px;
      color: var(--text-main);
    }
  }

  .notice-status {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-wrap: wrap;
    margin-bottom: 12px;

    .notice-status-text {
      font-size: 12px;
      color: var(--text-secondary);
    }
  }

  .notice-options {
    display: flex;
    align-items: center;
    gap: 10px;
    flex-wrap: wrap;
    margin-top: 12px;
  }

  .notice-option-label {
    font-size: 13px;
    font-weight: 600;
    color: var(--text-main);
    min-width: 60px;
  }

  .notice-preview-wrap {
    margin-top: 16px;
  }

  .notice-preview {
    display: flex;
    align-items: center;
    gap: 10px;
    box-sizing: border-box;
    width: 480px;
    max-width: 100%;
    margin-top: 8px;
    padding: 10px 14px;
    border: 1px solid #ffcc80;
    border-radius: 8px;
    background: #fff8e6;
    color: #d32f2f;

    .np-tag {
      flex: none;
      padding: 1px 8px;
      border: 1px solid rgba(211, 47, 47, 0.35);
      border-radius: 10px;
      font-size: 12px;
      color: #d32f2f;
    }

    .np-text {
      flex: 1;
      display: -webkit-box;
      overflow: hidden;
      -webkit-box-orient: vertical;
      -webkit-line-clamp: 2;
      font-size: 13px;
      line-height: 20px;
      word-break: break-all;
    }

    .np-action {
      flex: none;
      font-size: 12px;
      opacity: 0.85;
    }

    // 强提醒：与客户端一致的深红底 + 白字加粗
    &.strong {
      border-color: #8e1b12;
      background: #8e1b12;
      color: #ffffff;

      .np-tag {
        border-color: rgba(255, 255, 255, 0.4);
        background: rgba(255, 255, 255, 0.16);
        color: #ffffff;
      }

      .np-text {
        font-weight: 700;
      }
    }
  }

  .notice-actions {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    margin-top: 16px;
  }
}
</style>
