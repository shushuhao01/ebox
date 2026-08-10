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
            <Text style={{ fontSize: 34, fontWeight: 'bold', color: '#0F172A' }}>授权信息解读</Text>
            <Text style={{ fontSize: 15, color: '#64748B', marginTop: 4 }}>入口：主界面右上角【授权】按钮，或系统托盘图标 →「授权信息」</Text>
        </Box>
    </Box>

    {/* B 区 内容：左大图 + 右侧文字 */}
    <Box style={{ height: 540, flexDirection: 'row', gap: 30, alignItems: 'center' }}>
        {/* 左侧标注截图 */}
        <Box style={{
            position: 'relative', width: 620, height: 465,
            borderRadius: 12, border: '1px solid #E2E8F0',
            boxShadow: '0 8px 24px rgba(15,23,42,0.10)',
        }}>
            <Image src="resources/images/ebox_license.png" style={{ width: 620, height: 465, objectFit: 'cover', borderRadius: 12 }} />
            {/* 编号徽章（按 620/1024 缩放定位） */}
            {[
                { n: '1', x: 163, y: 194 },
                { n: '2', x: 163, y: 272 },
                { n: '3', x: 424, y: 295 },
                { n: '4', x: 439, y: 148 },
                { n: '5', x: 251, y: 328 },
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

        {/* 右侧图例 */}
        <Box style={{ flex: 1, height: 465, justifyContent: 'space-between' }}>
            {[
                { n: '1', t: '激活状态与到期时间', d: '已激活 / 在线授权；到期后无法启动和新增环境，但可正常查看界面。' },
                { n: '2', t: '本机指纹', d: '设备的唯一标识，联系客服时报上指纹可快速定位账号与问题。' },
                { n: '3', t: '解绑本机', d: '更换电脑时使用；每月有解绑次数上限（如本月 0/3 次）。' },
                { n: '4', t: '复制激活码', d: '一键复制当前激活码，方便自行保存或发送给客服核对。' },
                { n: '5', t: '续期与购买入口', d: '【重新激活/续期】输入新码延长授权；【购买激活码】跳转官方购买页。' },
            ].map((it, i) => (
                <Box key={i} style={{
                    flexDirection: 'row', gap: 14, alignItems: 'flex-start',
                    background: i === 0 ? '#EFF6FF' : '#F8FAFC',
                    border: '1px solid #E2E8F0', borderRadius: 12, padding: '10px 16px',
                }}>
                    <Box style={{
                        width: 28, height: 28, borderRadius: 14, background: '#2563EB',
                        justifyContent: 'center', alignItems: 'center', marginTop: 2,
                    }}>
                        <Text style={{ fontSize: 14, fontWeight: 'bold', color: '#FFFFFF' }}>{it.n}</Text>
                    </Box>
                    <Box style={{ flex: 1 }}>
                        <Text style={{ fontSize: 16, fontWeight: 'bold', color: '#0F172A' }}>{it.t}</Text>
                        <Text style={{ fontSize: 13, color: '#64748B', lineHeight: 1.5, marginTop: 3 }}>{it.d}</Text>
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
        <Text style={{ fontSize: 14, color: '#94A3B8' }}>06 / 19</Text>
    </Box>
</Slide>
