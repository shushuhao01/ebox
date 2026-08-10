<Slide style={{
    width: '1280px',
    height: '720px',
    background: '#FFFFFF',
    padding: '20px 64px',
    fontFamily: "'Source Han Sans SC', 'Microsoft YaHei', sans-serif",
}}>
    {/* A 区 标题块 */}
    <Box style={{ height: 100, flexDirection: 'row', alignItems: 'center', gap: 18 }}>
        <Box style={{ width: 8, height: 46, background: 'linear-gradient(180deg, #2563EB, #06B6D4)', borderRadius: 4 }} />
        <Box>
            <Text style={{ fontSize: 34, fontWeight: 'bold', color: '#0F172A' }}>下载与安装</Text>
            <Text style={{ fontSize: 15, color: '#64748B', marginTop: 4 }}>官方渠道获取 ｜ 绿色软件 · 解压即用 · 无需安装</Text>
        </Box>
    </Box>

    {/* B 区 内容 */}
    <Box style={{ height: 540, flexDirection: 'row', gap: 28 }}>
        {/* 左侧标题栏 */}
        <Box style={{
            width: 300, height: 540,
            borderRadius: 20,
            background: 'linear-gradient(165deg, #1E3A8A 0%, #2563EB 65%, #06B6D4 100%)',
            padding: 34,
            justifyContent: 'space-between',
        }}>
            <Box>
                <Box style={{
                    width: 72, height: 72, borderRadius: 18,
                    background: 'rgba(255,255,255,0.14)', border: '1px solid rgba(255,255,255,0.22)',
                    justifyContent: 'center', alignItems: 'center',
                }}>
                    <FAIcon name="download" style={{ fill: '#FFFFFF', width: 34, height: 34 }} />
                </Box>
                <Text style={{ fontSize: 26, fontWeight: 'bold', color: '#FFFFFF', marginTop: 24, lineHeight: 1.4 }}>
                    一分钟完成<br />获取与部署
                </Text>
            </Box>
            <Box>
                <Text style={{ fontSize: 15, color: 'rgba(255,255,255,0.85)', lineHeight: 1.9 }}>
                    ① 从网盘下载压缩包<br />
                    ② 解压得到 eBox 主程序<br />
                    ③ 双击即可运行
                </Text>
                <Box style={{ marginTop: 18, padding: '10px 16px', borderRadius: 12, background: 'rgba(255,255,255,0.12)' }}>
                    <Text style={{ fontSize: 13, color: '#BAE6FD', lineHeight: 1.6 }}>支持 Windows 系统，无需管理员权限，不写系统注册表。</Text>
                </Box>
            </Box>
        </Box>

        {/* 右侧内容 */}
        <Box style={{ flex: 1, height: 540, justifyContent: 'space-between' }}>
            {/* 下载渠道卡 1 */}
            <Box style={{
                flexDirection: 'row', alignItems: 'center', gap: 20,
                background: '#EFF6FF', border: '1px solid #BFDBFE',
                borderRadius: 16, padding: '20px 26px', height: 118,
            }}>
                <Box style={{
                    width: 56, height: 56, borderRadius: 14, background: '#2563EB',
                    justifyContent: 'center', alignItems: 'center',
                }}>
                    <FAIcon name="cloud-download-alt" style={{ fill: '#FFFFFF', width: 26, height: 26 }} />
                </Box>
                <Box style={{ flex: 1 }}>
                    <Text style={{ fontSize: 19, fontWeight: 'bold', color: '#0F172A' }}>百度网盘 <span style={{ fontSize: 13, color: '#2563EB', fontWeight: 'normal' }}>推荐渠道</span></Text>
                    <Text style={{ fontSize: 14, color: '#475569', marginTop: 6 }}>pan.baidu.com/s/1DL79AwuozFPLeWVSZHWsIw</Text>
                </Box>
                <Box style={{ padding: '10px 18px', borderRadius: 10, background: '#FFFFFF', border: '1px solid #BFDBFE', alignItems: 'center' }}>
                    <Text style={{ fontSize: 12, color: '#64748B' }}>提取码</Text>
                    <Text style={{ fontSize: 20, fontWeight: 'bold', color: '#2563EB' }}>y54h</Text>
                </Box>
            </Box>

            {/* 下载渠道卡 2 */}
            <Box style={{
                flexDirection: 'row', alignItems: 'center', gap: 20,
                background: '#F0FDFA', border: '1px solid #99F6E4',
                borderRadius: 16, padding: '20px 26px', height: 118,
            }}>
                <Box style={{
                    width: 56, height: 56, borderRadius: 14, background: '#06B6D4',
                    justifyContent: 'center', alignItems: 'center',
                }}>
                    <FAIcon name="cloud-download-alt" style={{ fill: '#FFFFFF', width: 26, height: 26 }} />
                </Box>
                <Box style={{ flex: 1 }}>
                    <Text style={{ fontSize: 19, fontWeight: 'bold', color: '#0F172A' }}>蓝奏云 <span style={{ fontSize: 13, color: '#06B6D4', fontWeight: 'normal' }}>备用渠道</span></Text>
                    <Text style={{ fontSize: 14, color: '#475569', marginTop: 6 }}>wwbvf.lanzouu.com/b00tcpfz0d</Text>
                </Box>
                <Box style={{ padding: '10px 18px', borderRadius: 10, background: '#FFFFFF', border: '1px solid #99F6E4', alignItems: 'center' }}>
                    <Text style={{ fontSize: 12, color: '#64748B' }}>密码</Text>
                    <Text style={{ fontSize: 20, fontWeight: 'bold', color: '#06B6D4' }}>96m1</Text>
                </Box>
            </Box>

            {/* 安装说明 */}
            <Box style={{
                flexDirection: 'row', gap: 16, alignItems: 'center',
                background: '#F8FAFC', border: '1px solid #E2E8F0',
                borderRadius: 16, padding: '18px 26px', height: 118,
            }}>
                <FAIcon name="check-circle" style={{ fill: '#2563EB', width: 30, height: 30 }} />
                <Box style={{ flex: 1 }}>
                    <Text style={{ fontSize: 17, fontWeight: 'bold', color: '#0F172A' }}>绿色免安装</Text>
                    <Text style={{ fontSize: 14, color: '#475569', lineHeight: 1.65, marginTop: 5 }}>
                        解压后建议把程序放到固定位置（如 D 盘软件目录）；首次双击运行如有系统安全提示，选择<span style={{ fontWeight: 'bold', color: '#2563EB' }}>「仍要运行」</span>即可正常使用。
                    </Text>
                </Box>
            </Box>

            {/* 警示条 */}
            <Box style={{
                flexDirection: 'row', gap: 16, alignItems: 'center',
                background: '#FFFBEB', border: '1px solid #FCD34D',
                borderRadius: 16, padding: '18px 26px', height: 118,
            }}>
                <FAIcon name="exclamation-triangle" style={{ fill: '#F59E0B', width: 32, height: 32 }} />
                <Box style={{ flex: 1 }}>
                    <Text style={{ fontSize: 17, fontWeight: 'bold', color: '#92400E' }}>重要：环境数据目录不可手动删除 / 移动</Text>
                    <Text style={{ fontSize: 14, color: '#78350F', lineHeight: 1.65, marginTop: 5 }}>
                        软件运行后会自动生成环境数据目录（默认 D 盘或空间最大的盘），存放各账号的登录状态与聊天数据。手动删除或移动将导致<span style={{ fontWeight: 'bold' }}>所有账号数据丢失</span>。
                    </Text>
                </Box>
            </Box>
        </Box>
    </Box>

    {/* C 区 页脚 */}
    <Box style={{ height: 40, flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}>
        <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
            <Image src="resources/images/icon_256.png" style={{ width: 20, height: 20, borderRadius: 5 }} />
            <Text style={{ fontSize: 14, color: '#94A3B8' }}>eBox 使用指南</Text>
        </Box>
        <Text style={{ fontSize: 14, color: '#94A3B8' }}>04 / 19</Text>
    </Box>
</Slide>
