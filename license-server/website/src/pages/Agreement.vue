<template>
  <div class="agreement">
    <section class="page-hero">
      <div class="container">
        <h1 class="page-title">用户协议</h1>
        <p class="page-sub">请在使用 {{ siteName }} 前仔细阅读本协议。</p>
      </div>
    </section>

    <section class="section">
      <div class="container legal-wrap">
        <p class="legal-updated">更新日期：{{ updatedAt }}</p>

        <h2>一、协议范围</h2>
        <p>
          本协议是您与 {{ siteName }} 之间就软件下载、安装及使用等事项所订立的约定。您在下载、安装或使用
          {{ siteName }} 时，即表示您已阅读并同意本协议的全部内容。
        </p>

        <h2>二、软件使用</h2>
        <p>
          {{ siteName }} 是一款帮助用户在同一台设备上创建相互独立运行环境的工具软件。您应合法、合规地使用本软件，
          并自行对所创建环境中的行为及内容负责。
        </p>
        <ul>
          <li>不得利用本软件从事任何违反法律法规的活动。</li>
          <li>不得对本软件进行反向工程、破解或用于任何侵权用途。</li>
          <li>不得传播、转售或以其他方式提供给未经授权的第三方。</li>
        </ul>

        <h2>三、授权与更新</h2>
        <p>
          本软件可能包含免费功能与付费功能。付费功能需通过官方公布的正规渠道获取授权后使用。我们可能会不定期发布
          更新以修复问题或改进功能，建议您及时更新到最新版本。
        </p>

        <h2>四、免责声明</h2>
        <p>
          在法律允许的范围内，因不可抗力、网络故障、第三方服务异常或您自身操作不当等原因造成的损失，我们不承担相应责任。
          请您在使用前自行评估并做好重要数据的备份。
        </p>

        <h2>五、协议变更</h2>
        <p>
          我们可能根据实际情况对本协议进行修订，修订后的协议将在本页面公布。您继续使用本软件即视为接受修订后的协议。
        </p>

        <h2>六、联系我们</h2>
        <p>
          如您对本协议有任何疑问，可通过<RouterLink to="/contact">联系我们</RouterLink>页面公布的方式与我们取得联系。
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
usePageHead('用户协议', 'eBox 用户协议与使用条款。')

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
