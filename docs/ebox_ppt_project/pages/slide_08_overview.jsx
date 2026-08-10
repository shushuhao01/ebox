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
            <Text style={{ fontSize: 34, fontWeight: 'bold', color: '#0F172A' }}>界面总览</Text>
            <Text style={{ fontSize: 15, color: '#64748B', marginTop: 4 }}>一屏看懂主界面五大区域 ｜ 左侧管环境，右侧看详情</Text>
        </Box>
    </Box>

    {/* B 区 内容：上大图 + 下方图例卡 */}
    <Box style={{ height: 540, justifyContent: 'space-between' }}>
        {/* 标注截图 */}
        <Box style={{ flexDirection: 'row', justifyContent: 'center' }}>
            <Box style={{
                position: 'relative', width: 533, height: 400,
                borderRadius: 12, border: '1px solid #E2E8F0',
                boxShadow: '0 8px 24px rgba(15,23,42,0.10)',
            }}>
                <Image src="resources/images/ebox_env.png" style={{ width: 533, height: 400, objectFit: 'cover', borderRadius: 12 }} />
                {/* 编号徽章（按 533/1024 缩放定位） */}
                {[
                    { n: '1', x: 161, y: 31 },
                    { n: '2', x: 18, y: 57 },
                    { n: '3', x: 18, y: 107 },
                    { n: '4', x: 161, y: 131 },
                    { n: '5', x: 161, y: 166 },
                ].map((b, i) => (
                    <Box key={i} style={{
                        position: 'absolute', left: b.x, top: b.y,
                        width: 30, height: 30, borderRadius: 15,
                        background: '#2563EB', border: '2px solid #FFFFFF',
                        boxShadow: '0 2px 8px rgba(37,99,235,0.5)',
                        justifyContent: 'center', alignItems: 'center', zIndex: 2,
                    }}>
                        <Text style={{ fontSize: 15, fontWeight: 'bold', color: '#FFFFFF' }}>{b.n}</Text>
                    </Box>
                ))}
            </Box>
        </Box>

        {/* 下方图例 */}
        <Box style={{ flexDirection: 'row', gap: 14, height: 116 }}>
            {[
                { n: '1', t: '顶部资源看板', d: 'CPU / 内存 / GPU / 显存 / 磁盘实时曲线' },
                { n: '2', t: '启动新进程', d: '选择应用 exe，一键在新环境中启动' },
                { n: '3', t: '环境卡片列表', d: '启动 / 改名 / 关闭，在线状态一目了然' },
                { n: '4', t: '提示横幅', d: '新手操作引导提示，可随时收起' },
                { n: '5', t: '环境详情区', d: '进程记录、环境信息、环境日志' },
            ].map((it, i) => (
                <Box key={i} style={{
                    flex: 1, background: i === 0 ? '#EFF6FF' : '#F8FAFC',
                    border: '1px solid #E2E8F0', borderRadius: 14, padding: '14px 14px',
                    justifyContent: 'space-between',
                }}>
                    <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
                        <Box style={{
                            width: 24, height: 24, borderRadius: 12, background: '#2563EB',
                            justifyContent: 'center', alignItems: 'center',
                        }}>
                            <Text style={{ fontSize: 13, fontWeight: 'bold', color: '#FFFFFF' }}>{it.n}</Text>
                        </Box>
                        <Text style={{ fontSize: 15, fontWeight: 'bold', color: '#0F172A' }}>{it.t}</Text>
                    </Box>
                    <Text style={{ fontSize: 12, color: '#64748B', lineHeight: 1.5 }}>{it.d}</Text>
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
        <Text style={{ fontSize: 14, color: '#94A3B8' }}>08 / 19</Text>
    </Box>
</Slide>
