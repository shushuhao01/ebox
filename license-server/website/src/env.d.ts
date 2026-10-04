/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** SSG 构建期（Node 环境）请求后端接口的绝对地址 */
  readonly VITE_SSG_API_BASE?: string
}

interface ImportMeta {
  readonly env: ImportMetaEnv
}

declare module '*.vue' {
  import type { DefineComponent } from 'vue'
  const component: DefineComponent<{}, {}, any>
  export default component
}
