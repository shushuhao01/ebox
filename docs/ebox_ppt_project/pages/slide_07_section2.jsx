<Slide style={{
    width: '1280px',
    height: '720px',
    background: 'linear-gradient(135deg, #172554 0%, #1E3A8A 40%, #0E7490 100%)',
    padding: 0,
    position: 'relative',
    fontFamily: "'Source Han Sans SC', 'Microsoft YaHei', sans-serif",
}}>
    <Box style={{ position: 'absolute', top: -140, right: -100, width: 460, height: 460, borderRadius: 230, background: 'rgba(37,99,235,0.20)' }} />
    <Box style={{ position: 'absolute', bottom: -120, left: -100, width: 380, height: 380, borderRadius: 190, background: 'rgba(6,182,212,0.14)' }} />

    {/* 巨型章节号 */}
    <Text style={{
        position: 'absolute', right: 90, top: 120,
        fontSize: 200, fontWeight: 'bold', lineHeight: 1,
        color: 'rgba(255,255,255,0.10)',
    }}>02</Text>

    <Box style={{ position: 'absolute', left: 90, top: 250, width: 800 }}>
        <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 14, marginBottom: 22 }}>
            <Box style={{ width: 48, height: 6, background: '#06B6D4', borderRadius: 3 }} />
            <Text style={{ fontSize: 17, color: '#7DD3FC', letterSpacing: 5 }}>CHAPTER 02</Text>
        </Box>
        <Text style={{ fontSize: 64, fontWeight: 'bold', color: '#FFFFFF', letterSpacing: 4 }}>环境多开实战</Text>
        <Text style={{ fontSize: 20, color: 'rgba(255,255,255,0.78)', marginTop: 22, lineHeight: 1.7 }}>
            界面总览 ｜ 启动新进程 ｜ 改名 ｜ 环境信息 ｜ 进程与日志
        </Text>
        <Box style={{ flexDirection: 'row', gap: 12, marginTop: 30 }}>
            {['一键多开', '独立环境', '日志排查'].map((t, i) => (
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
