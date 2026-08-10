<Slide style={{
    width: '1280px',
    height: '720px',
    background: 'linear-gradient(135deg, #172554 0%, #1E3A8A 35%, #2563EB 70%, #0E7490 100%)',
    padding: 0,
    position: 'relative',
    fontFamily: "'Source Han Sans SC', 'Microsoft YaHei', sans-serif",
}}>
    {/* 背景装饰圆 */}
    <Box style={{ position: 'absolute', top: -140, right: -100, width: 460, height: 460, borderRadius: 230, background: 'rgba(6,182,212,0.18)' }} />
    <Box style={{ position: 'absolute', bottom: -180, left: -120, width: 520, height: 520, borderRadius: 260, background: 'rgba(37,99,235,0.22)' }} />
    <Box style={{ position: 'absolute', top: 90, right: 180, width: 14, height: 14, borderRadius: 7, background: 'rgba(255,255,255,0.35)' }} />
    <Box style={{ position: 'absolute', top: 150, right: 120, width: 8, height: 8, borderRadius: 4, background: 'rgba(255,255,255,0.25)' }} />
    <Box style={{ position: 'absolute', bottom: 120, right: 320, width: 10, height: 10, borderRadius: 5, background: 'rgba(6,182,212,0.5)' }} />

    {/* 顶部品牌条 */}
    <Box style={{ position: 'absolute', top: 44, left: 72, flexDirection: 'row', alignItems: 'center', gap: 12 }}>
        <Image src="resources/images/icon_256.png" style={{ width: 34, height: 34, borderRadius: 8 }} />
        <Text style={{ fontSize: 17, color: 'rgba(255,255,255,0.85)', letterSpacing: 2 }}>eBox · 企业微信沙箱多开工具</Text>
    </Box>

    {/* 主标题区（骑线文字，偏左） */}
    <Box style={{ position: 'absolute', left: 72, top: 200, width: 820 }}>
        <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 14, marginBottom: 26 }}>
            <Box style={{ width: 56, height: 6, background: '#06B6D4', borderRadius: 3 }} />
            <Text style={{ fontSize: 18, color: '#7DD3FC', letterSpacing: 6 }}>快速上手 · 完整教学</Text>
        </Box>
        <Text style={{ fontSize: 76, fontWeight: 'bold', color: '#FFFFFF', lineHeight: 1.15, letterSpacing: 2 }}>
            eBox 使用指南
        </Text>
        <Text style={{ fontSize: 24, color: 'rgba(255,255,255,0.82)', lineHeight: 1.6, marginTop: 24 }}>
            一台电脑，同时登录多个账号<br />
            每个账号独立环境运行 —— 配置、缓存、聊天记录完全隔离
        </Text>
        {/* 特性标签 */}
        <Box style={{ flexDirection: 'row', gap: 14, marginTop: 36 }}>
            {['多账号独立环境', '一键启动多开', '数据完全隔离'].map((t, i) => (
                <Box key={i} style={{
                    padding: '10px 22px',
                    borderRadius: 22,
                    background: 'rgba(255,255,255,0.12)',
                    border: '1px solid rgba(255,255,255,0.25)',
                }}>
                    <Text style={{ fontSize: 16, color: '#FFFFFF', letterSpacing: 1 }}>{t}</Text>
                </Box>
            ))}
        </Box>
    </Box>

    {/* 右侧大图标 */}
    <Box style={{
        position: 'absolute', right: 110, top: 240,
        width: 220, height: 220,
        borderRadius: 48,
        background: 'rgba(255,255,255,0.10)',
        border: '1px solid rgba(255,255,255,0.22)',
        boxShadow: '0 24px 60px rgba(0,0,0,0.30)',
        justifyContent: 'center', alignItems: 'center',
    }}>
        <Image src="resources/images/icon_256.png" style={{ width: 168, height: 168, borderRadius: 36 }} />
    </Box>

    {/* 底部信息条 */}
    <Box style={{
        position: 'absolute', left: 72, right: 72, bottom: 44,
        flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center',
        borderTop: '1px solid rgba(255,255,255,0.18)', paddingTop: 20,
    }}>
        <Text style={{ fontSize: 14, color: 'rgba(255,255,255,0.65)', letterSpacing: 1 }}>适用版本 v2.8.x ｜ Windows 平台 ｜ 绿色软件 无需安装</Text>
        <Text style={{ fontSize: 14, color: 'rgba(255,255,255,0.65)', letterSpacing: 1 }}>2026.08</Text>
    </Box>
</Slide>
