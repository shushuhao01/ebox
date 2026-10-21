<template>
  <span class="time-cell">
    <span class="time-date">{{ parts.date }}</span>
    <span class="time-time">{{ timeText }}</span>
  </span>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { timeParts } from '@/utils/format'

const props = defineProps<{ value: string | number | Date | null | undefined }>()

const parts = computed(() => timeParts(props.value))
// 无时间时用不换行空格占位，保证每行都是固定两行高度，避免行高参差
const timeText = computed(() => parts.value.time || '\u00A0')
</script>

<style scoped lang="scss">
.time-cell {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  line-height: 1.35;
}

.time-date,
.time-time {
  display: block;
  white-space: nowrap;
  font-variant-numeric: tabular-nums;
}

.time-time {
  font-size: 12px;
  color: var(--text-secondary);
}
</style>
