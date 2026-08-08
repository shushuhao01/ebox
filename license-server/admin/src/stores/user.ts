import { defineStore } from 'pinia'
import { computed, ref } from 'vue'
import { getProfile, type UserInfo } from '@/api/auth'

const TOKEN_KEY = 'ebox_token'
const USER_KEY = 'ebox_user'

function readStoredUser(): UserInfo | null {
  try {
    return JSON.parse(localStorage.getItem(USER_KEY) || 'null')
  } catch {
    return null
  }
}

export const useUserStore = defineStore('user', () => {
  const token = ref<string>(localStorage.getItem(TOKEN_KEY) || '')
  const user = ref<UserInfo | null>(readStoredUser())

  const isLogin = computed(() => !!token.value)
  const isSuperAdmin = computed(() => user.value?.role === 1)
  const displayName = computed(() => user.value?.nickname || user.value?.username || '管理员')

  function setToken(t: string) {
    token.value = t
    localStorage.setItem(TOKEN_KEY, t)
  }

  function setUser(u: UserInfo) {
    user.value = u
    localStorage.setItem(USER_KEY, JSON.stringify(u))
  }

  async function fetchProfile() {
    const profile = await getProfile()
    setUser(profile)
    return profile
  }

  function logout() {
    token.value = ''
    user.value = null
    localStorage.removeItem(TOKEN_KEY)
    localStorage.removeItem(USER_KEY)
  }

  return { token, user, isLogin, isSuperAdmin, displayName, setToken, setUser, fetchProfile, logout }
})
