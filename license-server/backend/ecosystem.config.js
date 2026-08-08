// PM2 配置文件 - eBox 授权服务平台生产环境
// 进程名与部署域名保持一致，方便 pm2 restart/start 管理
module.exports = {
  apps: [
    {
      name: 'abc222.cn-backend',
      script: 'dist/app.js',
      cwd: __dirname,
      instances: 1,
      exec_mode: 'fork',
      autorestart: true,
      max_memory_restart: '300M',
      env: {
        NODE_ENV: 'production',
      },
      error_file: './logs/pm2-error.log',
      out_file: './logs/pm2-out.log',
      log_date_format: 'YYYY-MM-DD HH:mm:ss',
      max_restarts: 10,
      min_uptime: '10s',
      kill_timeout: 5000,
      restart_delay: 4000,
    },
  ],
};
