<template>
  <div v-if="visible" class="announce">
    <div class="container announce-inner">
      <span class="announce-dot"></span>
      <RouterLink class="announce-text announce-link" :to="announcement!.detailUrl">
        {{ announcement!.text }}
      </RouterLink>
      <a
        v-if="announcement!.linkUrl"
        class="announce-external"
        :href="announcement!.linkUrl"
        target="_blank"
        rel="noopener"
      >
        查看链接 ↗
      </a>
      <button class="announce-close" type="button" aria-label="关闭" @click="close">×</button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
import { useSite } from '@/composables/useSite'

const { announcement } = useSite()
const closed = ref(false)

const visible = computed(() => !closed.value && !!announcement.value)

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

.announce-external {
  flex: none;
  color: #fff;
  font-size: 13px;
  padding: 2px 10px;
  border: 1px solid rgba(255, 255, 255, 0.6);
  border-radius: 12px;
  text-decoration: none;
  white-space: nowrap;
}

.announce-external:hover {
  background: rgba(255, 255, 255, 0.15);
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
