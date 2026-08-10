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
            <Text style={{ fontSize: 34, fontWeight: 'bold', color: '#0F172A' }}>首次运行与激活</Text>
            <Text style={{ fontSize: 15, color: '#64748B', marginTop: 4 }}>双击运行 ｜ 输入激活码 ｜ 完成授权进入主界面</Text>
        </Box>
    </Box>

    {/* B 区 内容：非对称双栏 60:40 */}
    <Box style={{ height: 540, flexDirection: 'row', gap: 30 }}>
        {/* 左侧 激活四步流程 */}
        <Box style={{
            width: '58%', height: 540,
            background: '#F8FAFC', border: '1px solid #E2E8F0', borderRadius: 20,
            padding: '30px 36px', justifyContent: 'space-between',
        }}>
            <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 12 }}>
                <FAIcon name="key" style={{ fill: '#2563EB', width: 24, height: 24 }} />
                <Text style={{ fontSize: 21, fontWeight: 'bold', color: '#0F172A' }}>激活流程 · 四步完成</Text>
            </Box>
            {[
                { no: '1', title: '双击运行 eBox', desc: '首次运行如有系统安全提示，选择「仍要运行」；随后自动弹出激活窗口。' },
                { no: '2', title: '复制激活码', desc: '购买激活码后复制到剪贴板，回到激活窗口粘贴到输入框。' },
                { no: '3', title: '点击【激活】', desc: '校验通过后窗口自动关闭，即完成授权，无需重启软件。' },
                { no: '4', title: '进入主界面', desc: '标题栏显示版本、更新时间与到期时间，可以开始创建环境多开。' },
            ].map((s, i) => (
                <Box key={i} style={{ flexDirection: 'row', gap: 18, alignItems: 'flex-start' }}>
                    <Box style={{ alignItems: 'center' }}>
                        <Box style={{
                            width: 40, height: 40, borderRadius: 20,
                            background: 'linear-gradient(135deg, #2563EB, #06B6D4)',
                            justifyContent: 'center', alignItems: 'center',
                        }}>
                            <Text style={{ fontSize: 18, fontWeight: 'bold', color: '#FFFFFF' }}>{s.no}</Text>
                        </Box>
                        {i < 3 && <Box style={{ width: 2, height: 42, background: '#BFDBFE', marginTop: 4 }} />}
                    </Box>
                    <Box style={{ flex: 1, paddingTop: 4 }}>
                        <Text style={{ fontSize: 18, fontWeight: 'bold', color: '#0F172A' }}>{s.title}</Text>
                        <Text style={{ fontSize: 14, color: '#64748B', lineHeight: 1.55, marginTop: 4 }}>{s.desc}</Text>
                    </Box>
                </Box>
            ))}
        </Box>

        {/* 右侧 购买与说明 */}
        <Box style={{ flex: 1, height: 540, justifyContent: 'space-between' }}>
            <Box style={{
                borderRadius: 20, padding: '26px 28px', height: 300,
                background: 'linear-gradient(160deg, #1E3A8A 0%, #2563EB 70%, #06B6D4 100%)',
                justifyContent: 'space-between',
            }}>
                <Box>
                    <Text style={{ fontSize: 20, fontWeight: 'bold', color: '#FFFFFF' }}>还没有激活码？</Text>
                    <Text style={{ fontSize: 14, color: 'rgba(255,255,255,0.85)', lineHeight: 1.8, marginTop: 12 }}>
                        在激活窗口点击【购买激活码】跳转购买页面，或联系客服购买；购买后回到激活窗口输入激活码即可。
                    </Text>
                </Box>
                <Box style={{
                    flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between',
                    background: 'rgba(255,255,255,0.14)', border: '1px solid rgba(255,255,255,0.25)',
                    borderRadius: 12, padding: '12px 18px',
                }}>
                    <Box>
                        <Text style={{ fontSize: 12, color: '#BAE6FD' }}>官方购买地址</Text>
                        <Text style={{ fontSize: 19, fontWeight: 'bold', color: '#FFFFFF', marginTop: 2 }}>noepay.cn</Text>
                    </Box>
                    <FAIcon name="shopping-cart" style={{ fill: '#FFFFFF', width: 26, height: 26 }} />
                </Box>
            </Box>

            <Box style={{
                borderRadius: 20, padding: '24px 28px', height: 216,
                background: '#EFF6FF', border: '1px solid #BFDBFE',
                justifyContent: 'space-between',
            }}>
                <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
                    <FAIcon name="info-circle" style={{ fill: '#2563EB', width: 22, height: 22 }} />
                    <Text style={{ fontSize: 17, fontWeight: 'bold', color: '#1E3A8A' }}>激活码说明</Text>
                </Box>
                <Text style={{ fontSize: 14, color: '#334155', lineHeight: 1.8 }}>
                    · 一码一机：激活码与本机指纹绑定<br />
                    · 在线授权：离线宽限期内可正常使用<br />
                    · 到期前标题栏会持续显示到期时间<br />
                    · 请从官方渠道获取，谨防盗版程序
                </Text>
            </Box>
        </Box>
    </Box>

    {/* C 区 页脚 */}
    <Box style={{ height: 40, flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}>
        <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 8 }}>
            <Image src="resources/images/icon_256.png" style={{ width: 20, height: 20, borderRadius: 5 }} />
            <Text style={{ fontSize: 14, color: '#94A3B8' }}>eBox 使用指南</Text>
        </Box>
        <Text style={{ fontSize: 14, color: '#94A3B8' }}>05 / 19</Text>
    </Box>
</Slide>
