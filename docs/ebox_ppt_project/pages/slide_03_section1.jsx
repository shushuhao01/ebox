<Slide style={{
    width: '1280px',
    height: '720px',
    background: 'linear-gradient(135deg, #172554 0%, #1E3A8A 40%, #2563EB 100%)',
    padding: 0,
    position: 'relative',
    fontFamily: "'Source Han Sans SC', 'Microsoft YaHei', sans-serif",
}}>
    <Box style={{ position: 'absolute', bottom: -160, right: -120, width: 480, height: 480, borderRadius: 240, background: 'rgba(6,182,212,0.16)' }} />
    <Box style={{ position: 'absolute', top: -100, left: -80, width: 340, height: 340, borderRadius: 170, background: 'rgba(255,255,255,0.06)' }} />

    {/* 巨型章节号 */}
    <Text style={{
        position: 'absolute', right: 90, top: 120,
        fontSize: 200, fontWeight: 'bold', lineHeight: 1,
        color: 'rgba(255,255,255,0.10)',
    }}>01</Text>

    <Box style={{ position: 'absolute', left: 90, top: 250, width: 760 }}>
        <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 14, marginBottom: 22 }}>
            <Box style={{ width: 48, height: 6, background: '#06B6D4', borderRadius: 3 }} />
            <Text style={{ fontSize: 17, color: '#7DD3FC', letterSpacing: 5 }}>CHAPTER 01</Text>
        </Box>
        <Text style={{ fontSize: 64, fontWeight: 'bold', color: '#FFFFFF', letterSpacing: 4 }}>快速上手</Text>
        <Text style={{ fontSize: 20, color: 'rgba(255,255,255,0.78)', marginTop: 22, lineHeight: 1.7 }}>
            下载与安装 ｜ 首次运行与激活 ｜ 授权信息解读
        </Text>
        <Box style={{ flexDirection: 'row', gap: 12, marginTop: 30 }}>
            {['下载渠道', '运行应用', '激活授权'].map((t, i) => (
                <Box key={i} style={{
                    flexDirection: 'row', alignItems: 'center', gap: 8,
                    padding: '8px 18px', borderRadius: 18,
                    background: 'rgba(255,255,255,0.10)', border: '1px solid rgba(255,255,255,0.2)',
                }}>
                    <Text style={{ fontSize: 14, color: '#06B6D4', fontWeight: 'bold' }}>{'0' + (i + 1)}</Text>
                    <Text style={{ fontSize: 14, color: '#FFFFFF' }}>{t}</Text>
                </Box>
            ))}
        </Box>
    </Box>

    <Box style={{ position: 'absolute', left: 90, bottom: 52, flexDirection: 'row', alignItems: 'center', gap: 10 }}>
        <Image src="resources/images/icon_256.png" style={{ width: 24, height: 24, borderRadius: 6 }} />
        <Text style={{ fontSize: 14, color: 'rgba(255,255,255,0.55)' }}>eBox 使用指南</Text>
    </Box>
</Slide>
