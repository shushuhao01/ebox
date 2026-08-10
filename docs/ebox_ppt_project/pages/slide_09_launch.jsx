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
            <Text style={{ fontSize: 34, fontWeight: 'bold', color: '#0F172A' }}>启动新进程</Text>
            <Text style={{ fontSize: 15, color: '#64748B', marginTop: 4 }}>两种启动方式 ｜ 每个环境各启动一个企业微信，即可同时登录多个账号</Text>
        </Box>
    </Box>

    {/* B 区 内容：非对称双栏 60:40 */}
    <Box style={{ height: 540, flexDirection: 'row', gap: 28 }}>
        {/* 左栏：方法一步骤流 */}
        <Box style={{
            width: '58%', height: 540,
            background: '#F8FAFC', border: '1px solid #E2E8F0', borderRadius: 20,
            padding: '28px 34px', justifyContent: 'space-between',
        }}>
            <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 12 }}>
                <Box style={{ padding: '5px 14px', borderRadius: 12, background: '#2563EB' }}>
                    <Text style={{ fontSize: 14, fontWeight: 'bold', color: '#FFFFFF' }}>方法一 · 按钮启动</Text>
                </Box>
                <Text style={{ fontSize: 14, color: '#64748B' }}>最常用，适合首次创建环境</Text>
            </Box>
            {[
                { no: '1', title: '点击【启动新进程】', desc: '主界面左上角蓝色按钮；或点击某个环境卡片上的【启动】，在指定环境中启动。' },
                { no: '2', title: '选择企业微信 exe', desc: '在文件选择窗口中找到企业微信可执行文件，如 D:\\RJ\\WXWork\\WXWork.exe。' },
                { no: '3', title: '等待首次加载', desc: '首次启动会显示「首次启动加载中，请稍候…」，加载完成后企业微信自动打开。' },
                { no: '4', title: '重复操作实现多开', desc: '在不同环境中各启动一个企业微信，即可同时登录多个账号，互不干扰。' },
            ].map((s, i) => (
                <Box key={i} style={{ flexDirection: 'row', gap: 16, alignItems: 'flex-start' }}>
                    <Box style={{ alignItems: 'center' }}>
                        <Box style={{
                            width: 38, height: 38, borderRadius: 19,
                            background: 'linear-gradient(135deg, #2563EB, #06B6D4)',
                            justifyContent: 'center', alignItems: 'center',
                        }}>
                            <Text style={{ fontSize: 17, fontWeight: 'bold', color: '#FFFFFF' }}>{s.no}</Text>
                        </Box>
                        {i < 3 && <Box style={{ width: 2, height: 38, background: '#BFDBFE', marginTop: 4 }} />}
                    </Box>
                    <Box style={{ flex: 1, paddingTop: 3 }}>
                        <Text style={{ fontSize: 17, fontWeight: 'bold', color: '#0F172A' }}>{s.title}</Text>
                        <Text style={{ fontSize: 13, color: '#64748B', lineHeight: 1.55, marginTop: 3 }}>{s.desc}</Text>
                    </Box>
                </Box>
            ))}
        </Box>

        {/* 右栏：方法二 + 提示 */}
        <Box style={{ flex: 1, height: 540, justifyContent: 'space-between' }}>
            <Box style={{
                borderRadius: 20, padding: '26px 28px', height: 310,
                background: 'linear-gradient(160deg, #0E7490 0%, #06B6D4 100%)',
                justifyContent: 'space-between',
            }}>
                <Box>
                    <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
                        <FAIcon name="hand-pointer" style={{ fill: '#FFFFFF', width: 24, height: 24 }} />
                        <Text style={{ fontSize: 19, fontWeight: 'bold', color: '#FFFFFF' }}>方法二 · 直接拖拽</Text>
                    </Box>
                    <Text style={{ fontSize: 14, color: 'rgba(255,255,255,0.9)', lineHeight: 1.85, marginTop: 14 }}>
                        把企业微信的 <span style={{ fontWeight: 'bold' }}>exe 或快捷方式</span>直接拖到 eBox 窗口中，软件会自动选择或新建一个合适的环境运行。
                    </Text>
                </Box>
                <Box style={{
                    background: 'rgba(255,255,255,0.14)', border: '1px solid rgba(255,255,255,0.25)',
                    borderRadius: 12, padding: '12px 16px',
                }}>
                    <Text style={{ fontSize: 13, color: '#CFFAFE', lineHeight: 1.7 }}>
                        exe 文件：自动选择没有重名进程的环境运行<br />
                        快捷方式等：选择空环境或新建环境运行
                    </Text>
                </Box>
            </Box>

            <Box style={{
                borderRadius: 20, padding: '22px 28px', height: 206,
                background: '#EFF6FF', border: '1px solid #BFDBFE',
                justifyContent: 'space-between',
            }}>
                <Box style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
                    <FAIcon name="shield-alt" style={{ fill: '#2563EB', width: 22, height: 22 }} />
                    <Text style={{ fontSize: 17, fontWeight: 'bold', color: '#1E3A8A' }}>多开隔离原理</Text>
                </Box>
                <Text style={{ fontSize: 14, color: '#334155', lineHeight: 1.8 }}>
                    每个环境拥有独立的配置、缓存与聊天记录，账号之间完全隔离；环境数量取决于电脑内存与磁盘空间，可通过顶部看板实时观察资源占用。
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
        <Text style={{ fontSize: 14, color: '#94A3B8' }}>09 / 19</Text>
    </Box>
</Slide>
