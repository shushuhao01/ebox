<template>
  <div class="notfound">
    <div class="code">404</div>
    <p class="tip">抱歉，您访问的页面不存在</p>
    <el-button type="primary" @click="$router.replace('/dashboard')">
      <el-icon><HomeFilled /></el-icon>&nbsp;返回首页
    </el-button>
  </div>
</template>

<script setup lang="ts">
</script>

<style scoped lang="scss">
.notfound {
  height: 100%;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 16px;

  .code {
    font-size: 96px;
    font-weight: 700;
    color: var(--primary-color);
    line-height: 1;
    text-shadow: 0 4px 20px rgba(58, 122, 254, 0.25);
  }

  .tip {
    color: var(--text-secondary);
    font-size: 15px;
    margin: 0;
  }
}
</style>
