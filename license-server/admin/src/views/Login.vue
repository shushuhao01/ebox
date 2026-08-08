<template>
  <div class="login-page">
    <!-- 背景装饰 -->
    <div class="bg-glow glow-1"></div>
    <div class="bg-glow glow-2"></div>
    <div class="bg-grid"></div>

    <div class="login-card">
      <div class="login-logo">
        <div class="logo-badge">
          <el-icon :size="26" color="#fff"><Key /></el-icon>
        </div>
        <h1 class="login-title">eBox 授权服务平台</h1>
        <p class="login-sub">License Management Console</p>
      </div>

      <el-form ref="formRef" :model="form" :rules="rules" size="large" @submit.prevent="onSubmit">
        <el-form-item prop="username">
          <el-input
            v-model="form.username"
            placeholder="请输入账号"
            :prefix-icon="User"
            clearable
            @keyup.enter="onSubmit"
          />
        </el-form-item>
        <el-form-item prop="password">
          <el-input
            v-model="form.password"
            type="password"
            placeholder="请输入密码"
            :prefix-icon="Lock"
            show-password
            @keyup.enter="onSubmit"
          />
        </el-form-item>
        <el-button type="primary" class="login-btn" :loading="loading" @click="onSubmit">
          登 录
        </el-button>
      </el-form>

      <div class="login-tip">
        <el-icon :size="13"><InfoFilled /></el-icon>
        <span>管理后台仅限授权运营人员使用</span>
      </div>
    </div>

    <div class="login-footer">eBox 授权服务平台 · 管理端</div>
  </div>
</template>

<script setup lang="ts">
import { reactive, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { ElMessage, type FormInstance, type FormRules } from 'element-plus'
import { User, Lock, Key, InfoFilled } from '@element-plus/icons-vue'
import { login } from '@/api/auth'
import { useUserStore } from '@/stores/user'

const router = useRouter()
const route = useRoute()
const store = useUserStore()

const formRef = ref<FormInstance>()
const loading = ref(false)

const form = reactive({
  username: '',
  password: '',
})

const rules: FormRules = {
  username: [{ required: true, message: '请输入账号', trigger: 'blur' }],
  password: [{ required: true, message: '请输入密码', trigger: 'blur' }],
}

async function onSubmit() {
  if (!formRef.value) return
  const valid = await formRef.value.validate().catch(() => false)
  if (!valid) return

  loading.value = true
  try {
    const res = await login({ username: form.username, password: form.password })
    store.setToken(res.token)
    store.setUser(res.user)
    ElMessage.success('登录成功')
    const redirect = (route.query.redirect as string) || '/dashboard'
    router.replace(redirect)
  } catch {
    // 错误提示已由拦截器处理
  } finally {
    loading.value = false
  }
}
</script>

<style scoped lang="scss">
.login-page {
  height: 100%;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  position: relative;
  overflow: hidden;
  background:
    radial-gradient(ellipse at 20% 15%, rgba(72, 118, 255, 0.35), transparent 55%),
    radial-gradient(ellipse at 85% 85%, rgba(0, 180, 216, 0.28), transparent 55%),
    linear-gradient(160deg, #0b1a3f 0%, #14275c 45%, #0e214d 100%);
}

// 光斑
.bg-glow {
  position: absolute;
  border-radius: 50%;
  filter: blur(80px);
  opacity: 0.55;
  pointer-events: none;
}
.glow-1 {
  width: 380px;
  height: 380px;
  left: -120px;
  top: -120px;
  background: #3a7afe;
}
.glow-2 {
  width: 460px;
  height: 460px;
  right: -160px;
  bottom: -160px;
  background: #00b6d4;
}

// 网格纹理
.bg-grid {
  position: absolute;
  inset: 0;
  background-image:
    linear-gradient(rgba(255, 255, 255, 0.05) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255, 255, 255, 0.05) 1px, transparent 1px);
  background-size: 42px 42px;
  mask-image: radial-gradient(ellipse at 50% 45%, #000 30%, transparent 75%);
  pointer-events: none;
}

.login-card {
  position: relative;
  z-index: 1;
  width: 400px;
  background: rgba(255, 255, 255, 0.94);
  border: 1px solid rgba(255, 255, 255, 0.6);
  border-radius: 16px;
  box-shadow: 0 20px 60px rgba(2, 10, 40, 0.45);
  padding: 42px 38px 30px;
  backdrop-filter: blur(8px);
  animation: fadeUp 0.45s ease-out;
}

@keyframes fadeUp {
  from {
    opacity: 0;
    transform: translateY(18px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.login-logo {
  text-align: center;
  margin-bottom: 28px;

  .logo-badge {
    width: 60px;
    height: 60px;
    margin: 0 auto 14px;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: 18px;
    background: linear-gradient(135deg, #3a7afe 0%, #6a5cff 100%);
    box-shadow: 0 10px 24px rgba(58, 122, 254, 0.45);
  }

  .login-title {
    font-size: 22px;
    font-weight: 700;
    color: #1b2a4a;
    margin: 0 0 4px;
    letter-spacing: 1px;
  }

  .login-sub {
    font-size: 13px;
    color: #8896b3;
    margin: 0;
    letter-spacing: 0.5px;
  }
}

.login-btn {
  width: 100%;
  margin-top: 6px;
  font-size: 15px;
  letter-spacing: 8px;
  font-weight: 600;
  height: 44px;
  background: linear-gradient(135deg, #3a7afe 0%, #6a5cff 100%);
  border: none;
  box-shadow: 0 8px 20px rgba(58, 122, 254, 0.35);

  &:hover {
    background: linear-gradient(135deg, #2f6ef5 0%, #5d4ff8 100%);
    box-shadow: 0 10px 24px rgba(58, 122, 254, 0.45);
  }
}

.login-tip {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 5px;
  margin-top: 20px;
  font-size: 12px;
  color: #a3afc8;

  :deep(.el-icon) {
    color: #8fa3ff;
  }
}

.login-footer {
  position: relative;
  z-index: 1;
  margin-top: 26px;
  font-size: 13px;
  color: rgba(255, 255, 255, 0.55);
  letter-spacing: 0.5px;
}
</style>
