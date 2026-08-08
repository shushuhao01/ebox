import * as echarts from 'echarts'

export type EChartsOption = echarts.EChartsCoreOption

/** 初始化图表（自动注册窗口 resize） */
export function initChart(el: HTMLElement, option: EChartsOption): echarts.ECharts {
  const chart = echarts.init(el)
  chart.setOption(option)
  return chart
}

/** 更新图表配置 */
export function setChartOption(chart: echarts.ECharts | null, option: EChartsOption) {
  if (!chart) return
  chart.setOption(option, true)
}

/** 销毁图表（组件卸载时调用） */
export function disposeChart(chart: echarts.ECharts | null) {
  if (chart) {
    chart.dispose()
  }
}

/** 常用调色板 */
export const PALETTE = ['#3A7AFE', '#22C55E', '#F59E0B', '#EF4444', '#8B5CF6', '#06B6D4', '#EC4899', '#84CC16']

/** 通用图例/坐标轴文字颜色 */
export const AXIS_TEXT = '#6B7280'

/** 生成默认折线/柱状系列颜色 */
export const LINE_COLORS = ['#3A7AFE', '#22C55E']
