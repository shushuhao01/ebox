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
            <Text style={{ fontSize: 34, fontWeight: 'bold', color: '#0F172A' }}>目录</Text>
            <Text style={{ fontSize: 15, color: '#64748B', marginTop: 4 }}>CONTENTS ｜ 三个章节，从零基础到熟练使用</Text>
        </Box>
    </Box>

    {/* B 区 内容 */}
    <Box style={{ height: 540, flexDirection: 'row', gap: 44, alignItems: 'center' }}>
        {/* 左侧引导块 */}
        <Box style={{
            width: 300, height: 420,
            borderRadius: 20,
            background: 'linear-gradient(160deg, #1E3A8A 0%, #2563EB 60%, #06B6D4 100%)',
            padding: 36,
            justifyContent: 'space-between',
        }}>
            <Box>
                <Text style={{ fontSize: 15, color: 'rgba(255,255,255,0.75)', letterSpacing: 3 }}>GUIDE MAP</Text>
                <Text style={{ fontSize: 40, fontWeight: 'bold', color: '#FFFFFF', lineHeight: 1.3, marginTop: 14 }}>
                    学习<br />路径
                </Text>
            </Box>
            <Box>
                <Text style={{ fontSize: 15, color: 'rgba(255,255,255,0.85)', lineHeight: 1.8 }}>
                    按章节顺序阅读约需 15 分钟；也可直接跳到对应章节查阅具体操作。
                </Text>
                <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 10, marginTop: 20 }}>
                    <Image src="resources/images/icon_256.png" style={{ width: 30, height: 30, borderRadius: 7 }} />
                    <Text style={{ fontSize: 14, color: 'rgba(255,255,255,0.8)' }}>eBox v2.8.x</Text>
                </Box>
            </Box>
        </Box>

        {/* 右侧章节条目 */}
        <Box style={{ flex: 1, height: 420, justifyContent: 'space-between' }}>
            {[
                { no: '01', title: '快速上手', desc: '下载渠道 ｜ 首次运行与激活 ｜ 授权信息解读', pages: 'P04 - P06' },
                { no: '02', title: '环境多开实战', desc: '界面总览 ｜ 启动新进程 ｜ 改名 ｜ 环境信息 ｜ 进程与日志', pages: 'P07 - P12' },
                { no: '03', title: '维护与售后', desc: '清理缓存 ｜ 结束进程与删除环境 ｜ 续费解绑 ｜ 更新升级 ｜ 常见问题', pages: 'P13 - P18' },
            ].map((c, i) => (
                <Box key={i} style={{
                    flexDirection: 'row', alignItems: 'center', gap: 26,
                    background: i === 0 ? '#EFF6FF' : '#F8FAFC',
                    border: '1px solid #E2E8F0',
                    borderRadius: 16, padding: '24px 30px',
                }}>
                    <Text style={{
                        fontSize: 52, fontWeight: 'bold', lineHeight: 1,
                        backgroundImage: 'linear-gradient(135deg, #2563EB 0%, #06B6D4 100%)',
                        backgroundClip: 'text', color: 'transparent',
                    }}>{c.no}</Text>
                    <Box style={{ flex: 1 }}>
                        <Text style={{ fontSize: 23, fontWeight: 'bold', color: '#0F172A' }}>{c.title}</Text>
                        <Text style={{ fontSize: 14, color: '#64748B', marginTop: 8, lineHeight: 1.5 }}>{c.desc}</Text>
                    </Box>
                    <Box style={{
                        padding: '6px 14px', borderRadius: 14,
                        background: '#FFFFFF', border: '1px solid #E2E8F0',
                    }}>
                        <Text style={{ fontSize: 13, color: '#2563EB', fontWeight: 'bold' }}>{c.pages}</Text>
                    </Box>
                </Box>
            ))}
        </Box>
    </Box>

    {/* C 区 页脚 */}
    <Box style={{ height: 40, flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}>
        <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
            <Image src="resources/images/icon_256.png" style={{ width: 20, height: 20, borderRadius: 5 }} />
            <Text style={{ fontSize: 14, color: '#94A3B8' }}>eBox 使用指南</Text>
        </Box>
        <Text style={{ fontSize: 14, color: '#94A3B8' }}>02 / 19</Text>
    </Box>
</Slide>
