<template>
  <div class="page-container">
    <!-- 工具栏 -->
    <div class="toolbar-card">
      <span class="page-title">管理员账号</span>
      <div class="toolbar-right">
        <el-button type="primary" :icon="Plus" @click="openCreate">新建管理员</el-button>
      </div>
    </div>

    <!-- 列表 -->
    <div class="table-card">
      <el-table :data="list" v-loading="loading" stripe>
        <el-table-column label="ID" prop="id" width="90" />
        <el-table-column label="用户名" prop="username" min-width="140" />
        <el-table-column label="昵称" min-width="120">
          <template #default="{ row }">{{ row.nickname || '-' }}</template>
        </el-table-column>
        <el-table-column label="角色" width="130" align="center">
          <template #default="{ row }">
            <el-tag :type="row.role === 1 ? 'danger' : 'info'" size="small">
              {{ row.role === 1 ? '超级管理员' : '管理员' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="状态" width="90" align="center">
          <template #default="{ row }">
            <el-tag :type="row.status === 1 ? 'success' : 'danger'" size="small" effect="plain">
              {{ row.status === 1 ? '启用' : '停用' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column label="最后登录" width="165">
          <template #default="{ row }">{{ formatTime(row.lastLoginAt) }}</template>
        </el-table-column>
        <el-table-column label="创建时间" width="165">
          <template #default="{ row }">{{ formatTime(row.createdAt) }}</template>
        </el-table-column>
        <el-table-column label="操作" width="110" fixed="right" align="center">
          <template #default="{ row }">
            <el-button
              v-if="row.id !== store.user?.id"
              link
              :type="row.status === 1 ? 'danger' : 'success'"
              size="small"
              @click="handleToggle(row)"
            >{{ row.status === 1 ? '停用' : '启用' }}</el-button>
            <span v-else class="text-secondary">当前账号</span>
          </template>
        </el-table-column>
        <template #empty>
          <el-empty description="暂无管理员" :image-size="80" />
        </template>
      </el-table>
    </div>

    <!-- 新建管理员 -->
    <el-dialog v-model="createVisible" title="新建管理员" width="460px">
      <el-form ref="formRef" :model="form" :rules="rules" label-width="80px">
        <el-form-item label="用户名" prop="username">
          <el-input v-model="form.username" placeholder="登录账号" maxlength="64" />
        </el-form-item>
        <el-form-item label="密码" prop="password">
          <el-input v-model="form.password" type="password" show-password placeholder="至少 6 位" maxlength="128" />
        </el-form-item>
        <el-form-item label="昵称">
          <el-input v-model="form.nickname" placeholder="昵称（可选）" maxlength="64" />
        </el-form-item>
        <el-form-item label="角色">
          <el-radio-group v-model="form.role">
            <el-radio :value="0">管理员</el-radio>
            <el-radio :value="1">超级管理员</el-radio>
          </el-radio-group>
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="createVisible = false">取消</el-button>
        <el-button type="primary" :loading="saving" @click="submit">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { ElMessage, ElMessageBox, type FormInstance, type FormRules } from 'element-plus'
import { Plus } from '@element-plus/icons-vue'
import { getUsers, createUser, setUserStatus, type AdminUser } from '@/api/users'
import { useUserStore } from '@/stores/user'
import { formatTime } from '@/utils/format'

const store = useUserStore()

const list = ref<AdminUser[]>([])
const loading = ref(false)

async function load() {
  loading.value = true
  try {
    list.value = await getUsers()
  } finally {
    loading.value = false
  }
}

// ============ 新建 ============
const createVisible = ref(false)
const saving = ref(false)
const formRef = ref<FormInstance>()
const form = reactive({ username: '', password: '', nickname: '', role: 0 as 0 | 1 })
const rules: FormRules = {
  username: [{ required: true, message: '请输入用户名', trigger: 'blur' }],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 6, message: '密码至少 6 位', trigger: 'blur' },
  ],
}

function openCreate() {
  form.username = ''
  form.password = ''
  form.nickname = ''
  form.role = 0
  createVisible.value = true
}

async function submit() {
  if (!formRef.value) return
  const valid = await formRef.value.validate().catch(() => false)
  if (!valid) return
  saving.value = true
  try {
    await createUser({
      username: form.username,
      password: form.password,
      nickname: form.nickname || undefined,
      role: form.role,
    })
    ElMessage.success('管理员创建成功')
    createVisible.value = false
    load()
  } catch {
    // 拦截器已提示
  } finally {
    saving.value = false
  }
}

// ============ 停用 / 启用 ============
async function handleToggle(row: AdminUser) {
  const enable = row.status === 0
  try {
    await ElMessageBox.confirm(
      enable
        ? `确定启用管理员「${row.username}」吗？`
        : `确定停用管理员「${row.username}」吗？停用后该账号将无法登录。`,
      enable ? '启用确认' : '停用确认',
      {
        confirmButtonText: enable ? '确认启用' : '确认停用',
        cancelButtonText: '取消',
        type: enable ? 'success' : 'warning',
      }
    )
    await setUserStatus(row.id, enable ? 1 : 0)
    ElMessage.success(enable ? '已启用' : '已停用')
    await load()
  } catch {
    // 取消或失败（拦截器已提示）
  }
}

onMounted(load)
</script>

<style scoped lang="scss">
.toolbar-right {
  margin-left: auto;
}

.text-secondary {
  color: var(--text-secondary);
}
</style>
