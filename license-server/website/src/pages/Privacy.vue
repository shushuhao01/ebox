<template>
  <div class="privacy">
    <section class="page-hero">
      <div class="container">
        <h1 class="page-title">隐私政策</h1>
        <p class="page-sub">我们重视并保护您的个人信息与隐私安全。</p>
      </div>
    </section>

    <section class="section">
      <div class="container legal-wrap">
        <p class="legal-updated">更新日期：{{ updatedAt }}</p>

        <h2>一、我们收集的信息</h2>
        <p>
          为了向您提供 {{ siteName }} 的相关服务，我们可能收集必要的软件运行信息与服务使用信息，例如软件版本、
          运行状态与访问日志等。我们不会收集您在所创建环境中产生的任何业务数据内容。
        </p>

        <h2>二、信息的使用</h2>
        <ul>
          <li>用于软件的安装、激活与版本更新等基础功能。</li>
          <li>用于排查故障、优化产品体验与保障服务稳定。</li>
          <li>用于响应您的咨询与支持请求。</li>
        </ul>

        <h2>三、信息的存储与保护</h2>
        <p>
          我们会采取合理的技术与管理措施保护所收集的信息，防止其被未经授权地访问、披露或篡改。相关信息仅在实现
          上述目的所必需的期限内保留。
        </p>

        <h2>四、信息共享</h2>
        <p>
          除法律法规要求或为向您提供服务所必需外，我们不会向第三方出售或出租您的个人信息。确需共享时，我们会
          要求相关方遵守相应的保密义务。
        </p>

        <h2>五、您的权利</h2>
        <p>
          您有权了解、更正或删除与我们服务相关的个人信息。如需行使上述权利，可通过<RouterLink to="/contact">联系我们</RouterLink>
          页面与我们取得联系。
        </p>

        <h2>六、政策更新</h2>
        <p>
          我们可能适时更新本隐私政策，更新后的内容将在本页面公布。建议您定期查阅以了解最新的隐私保护措施。
        </p>
      </div>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useSite } from '@/composables/useSite'
import { usePageHead } from '@/composables/usePageHead'

const { siteName } = useSite()
usePageHead('隐私政策', 'eBox 隐私政策与信息保护说明。')

const updatedAt = computed(() => new Date().toISOString().slice(0, 10))
</script>

<style scoped>
.page-hero {
  padding: 64px 0 20px;
  background: linear-gradient(160deg, #eef4ff 0%, #ffffff 70%);
  text-align: center;
}

.page-title {
  font-size: 40px;
  letter-spacing: -1px;
}

.page-sub {
  margin: 16px auto 0;
  max-width: 640px;
  color: var(--text-2);
  font-size: 17px;
}

.legal-wrap {
  max-width: 800px;
  color: var(--text-2);
  font-size: 15px;
  line-height: 1.9;
}

.legal-updated {
  color: var(--text-3);
  font-size: 14px;
  margin-bottom: 24px;
}

.legal-wrap h2 {
  margin: 32px 0 12px;
  font-size: 19px;
  color: var(--text-1);
}

.legal-wrap ul {
  padding-left: 22px;
  list-style: disc;
  margin: 12px 0;
}

.legal-wrap li {
  margin-bottom: 8px;
}

.legal-wrap a {
  color: var(--primary-color);
  font-weight: 600;
}

@media (max-width: 768px) {
  .page-title {
    font-size: 30px;
  }
}
</style>
