<template>
  <div v-if="visible" class="announce">
    <div class="container announce-inner">
      <span class="announce-dot"></span>
      <a
        v-if="announcement.url"
        class="announce-text announce-link"
        :href="announcement.url"
        target="_blank"
        rel="noopener"
      >
        {{ announcement.text }}
      </a>
      <span v-else class="announce-text">{{ announcement.text }}</span>
      <button class="announce-close" type="button" aria-label="关闭" @click="close">×</button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
import { useSite } from '@/composables/useSite'

const { announcement } = useSite()
const closed = ref(false)

const visible = computed(() => {
  if (closed.value) return false
  return announcement.value.enabled && !!announcement.value.text
})

function close() {
  closed.value = true
}
</script>

<style scoped>
.announce {
  background: linear-gradient(90deg, var(--primary-color), var(--primary-dark));
  color: #fff;
  font-size: 14px;
}

.announce-inner {
  display: flex;
  align-items: center;
  gap: 10px;
  min-height: 38px;
  padding-top: 6px;
  padding-bottom: 6px;
}

.announce-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: #fff;
  flex: none;
}

.announce-text {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.announce-link {
  text-decoration: underline;
}

.announce-close {
  background: none;
  border: none;
  color: #fff;
  font-size: 20px;
  line-height: 1;
  cursor: pointer;
  opacity: 0.85;
  padding: 0 4px;
}

.announce-close:hover {
  opacity: 1;
}
</style>
