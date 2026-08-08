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
              <div class="form-label">在线判定阈值</div>
              <el-input-number v-model="policy.online_threshold_minutes" :min="1" :max="1440" />
              <span class="hint">设备最后心跳距当前时间不超过该分钟数，即视为在线（分钟）</span>
            </div>

            <el-divider />

            <div class="form-item">
              <div class="form-label">公告（Notice）</div>
              <el-input
                v-model="policy.notice"
                type="textarea"
                :rows="3"
                maxlength="500"
                show-word-limit
                placeholder="客户端展示的公告内容（可选）"
                style="width: 480px; max-width: 100%"
              />
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
import { onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox, type FormInstance, type FormRules } from 'element-plus'
import { Check, RefreshLeft, Lock, Delete } from '@element-plus/icons-vue'
import { getConfig, updateConfig, cleanLogs, type SystemConfig } from '@/api/config'
import { changePassword } from '@/api/users'
import { useUserStore } from '@/stores/user'

const activeTab = ref('policy')

// ============ 授权策略 ============
const policy = reactive<SystemConfig>({
  heartbeat_interval_hours: '6',
  offline_grace_days: '7',
  force_online_activate: '0',
  notice: '',
  online_threshold_minutes: '30',
  log_retention_days: '1',
  log_clean_time: '03:00',
})

const forceOnline = ref(false)
const saving = ref(false)
const cleaning = ref(false)

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
      notice: policy.notice,
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
}
</style>
