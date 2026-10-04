-- ============================================================
-- eBox 授权服务平台 数据库初始化脚本
-- 目标库：license_server（utf8mb4）
-- 适用：MySQL 8.x；宝塔 phpMyAdmin 直接导入，或命令行执行
-- mysql -u root -p < schema.sql
-- ============================================================

CREATE DATABASE IF NOT EXISTS `license_server` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE `license_server`;

-- 后台管理员
CREATE TABLE IF NOT EXISTS `users` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `username` VARCHAR(64) NOT NULL UNIQUE,
  `password_hash` VARCHAR(128) NOT NULL,
  `nickname` VARCHAR(64) NULL,
  `role` TINYINT NOT NULL DEFAULT 1,             -- 1=超管 0=普通管理员
  `status` TINYINT NOT NULL DEFAULT 1,           -- 1=启用 0=停用
  `last_login_at` DATETIME NULL,
  `last_login_ip` VARCHAR(64) NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME NULL ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 激活码主表
CREATE TABLE IF NOT EXISTS `license_keys` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `code` VARCHAR(200) NOT NULL UNIQUE,           -- 2BOX-... 全文
  `code_fp` CHAR(64) NOT NULL UNIQUE,            -- code 的 SHA256 十六进制(64位)，加速查询
  `type` TINYINT NOT NULL DEFAULT 1,             -- 1=库存(时长制) 2=换机(绝对到期)
  `duration_sec` BIGINT NOT NULL DEFAULT 0,      -- 时长制有效秒数，0=永久
  `expire_at` BIGINT NULL,                       -- 换机码绝对到期时间戳(Unix秒)
  `bound` TINYINT NOT NULL DEFAULT 1,            -- 1=绑定 0=通用
  `unbind_max` SMALLINT NOT NULL DEFAULT 3,      -- -1=不限 0=禁止 n=每月n次
  `status` TINYINT NOT NULL DEFAULT 0,           -- 0=未用 1=已用 2=作废 3=过期 4=已换机 5=已删除(软删，客户端视同作废锁定)
  `batch_id` BIGINT NULL,
  `customer_id` BIGINT NULL,
  `remark` VARCHAR(255) NULL,
  `created_by` BIGINT NOT NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `used_at` DATETIME NULL,
  `revoked_at` DATETIME NULL,
  `revoked_by` BIGINT NULL,
  `revoked_reason` VARCHAR(255) NULL,
  `convert_from_key_id` BIGINT NULL,             -- 换机码来源码（解绑换机生成时记录）
  KEY `idx_status` (`status`),
  KEY `idx_customer` (`customer_id`),
  KEY `idx_batch` (`batch_id`),
  KEY `idx_created` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 客户档案
CREATE TABLE IF NOT EXISTS `customers` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `name` VARCHAR(64) NOT NULL,
  `phone` VARCHAR(32) NULL,
  `wechat` VARCHAR(64) NULL,
  `qq` VARCHAR(32) NULL,
  `source` VARCHAR(64) NULL,                     -- 来源渠道
  `remark` VARCHAR(255) NULL,
  `status` TINYINT NOT NULL DEFAULT 1,           -- 1=正常 0=停用
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME NULL ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 码↔客户关联
CREATE TABLE IF NOT EXISTS `key_customer` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `key_id` BIGINT NOT NULL,
  `customer_id` BIGINT NOT NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY `uk_key` (`key_id`),
  KEY `idx_customer` (`customer_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 设备表
CREATE TABLE IF NOT EXISTS `devices` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `key_id` BIGINT NOT NULL,
  `machine_fp` CHAR(16) NOT NULL,                -- 机器指纹(hex 16位)
  `status` TINYINT NOT NULL DEFAULT 1,           -- 1=在绑 0=已解绑 2=被踢
  `first_activate_at` DATETIME NULL,
  `last_online_at` DATETIME NULL,                -- 最近一次心跳
  `last_heartbeat_at` DATETIME NULL,
  `last_ip` VARCHAR(64) NULL,
  `os_info` VARCHAR(128) NULL,
  `app_version` VARCHAR(32) NULL,
  `unbind_at` DATETIME NULL,
  UNIQUE KEY `uk_key_fp` (`key_id`, `machine_fp`),
  KEY `idx_status` (`status`),
  KEY `idx_last_online` (`last_online_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 心跳与动作流水
CREATE TABLE IF NOT EXISTS `heartbeats` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `key_id` BIGINT NOT NULL,
  `device_id` BIGINT NULL,
  `action` TINYINT NOT NULL,                     -- 1=激活 2=心跳 3=解绑 4=被踢
  `client_time` BIGINT NOT NULL,                 -- 客户端时间戳
  `ip` VARCHAR(64) NULL,
  `app_version` VARCHAR(32) NULL,
  `detail` VARCHAR(255) NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY `idx_key` (`key_id`),
  KEY `idx_created` (`created_at`),
  KEY `idx_action` (`action`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 解绑记录（按月统计）
CREATE TABLE IF NOT EXISTS `unbind_logs` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `key_id` BIGINT NOT NULL,
  `device_id` BIGINT NOT NULL,
  `month` CHAR(6) NOT NULL,                      -- yyyyMM
  `new_key_id` BIGINT NULL,                      -- 换机码
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY `idx_key_month` (`key_id`, `month`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 作废日志
CREATE TABLE IF NOT EXISTS `revoke_logs` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `key_id` BIGINT NOT NULL,
  `operator_id` BIGINT NOT NULL,
  `reason` VARCHAR(255) NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY `idx_key` (`key_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 管理操作日志
CREATE TABLE IF NOT EXISTS `operation_logs` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `user_id` BIGINT NOT NULL,
  `action` VARCHAR(64) NOT NULL,                 -- 生成/作废/恢复/删除客户/设置...
  `target` VARCHAR(160) NULL,
  `detail` VARCHAR(255) NULL,
  `ip` VARCHAR(64) NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  KEY `idx_created` (`created_at`),
  KEY `idx_user` (`user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 系统设置
CREATE TABLE IF NOT EXISTS `system_config` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `cfg_key` VARCHAR(64) NOT NULL UNIQUE,
  `cfg_value` VARCHAR(255) NOT NULL,
  `remark` VARCHAR(255) NULL,
  `updated_at` DATETIME NULL ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 初始配置：心跳间隔6h / 离线宽限7天 / 激活不强制在线 / 公告为空
INSERT INTO `system_config` (`cfg_key`, `cfg_value`, `remark`) VALUES
  ('heartbeat_interval_hours', '6',   '客户端心跳间隔（小时，1-24）'),
  ('offline_grace_days',        '7',   '离线宽限天数（1-30）'),
  ('force_online_activate',     '0',   '是否强制在线激活（1=是 0=否）'),
  ('notice',                    '',    '站点公告（客户端激活时展示）'),
  ('online_threshold_minutes',  '30',  '心跳漏报宽限（分钟）：在线窗口 = 心跳间隔 + 宽限')
ON DUPLICATE KEY UPDATE `cfg_value` = `cfg_value`;

-- 生成批次
CREATE TABLE IF NOT EXISTS `key_batches` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `name` VARCHAR(64) NOT NULL,
  `remark` VARCHAR(255) NULL,
  `created_by` BIGINT NOT NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 初始超管（用户名 admin / 密码 admin123，首次登录后请立即修改）
-- 密码哈希由 npm run init:admin 生成，避免硬编码；如需手动执行：
-- INSERT INTO users (username, password_hash, nickname, role) VALUES ('admin', '<bcrypt>', '管理员', 1);

-- ============================================================
-- 官网（site_*）：与授权表物理隔离
-- ============================================================

-- 官网站点设置（KV）
CREATE TABLE IF NOT EXISTS `site_settings` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `cfg_key` VARCHAR(64) NOT NULL UNIQUE,
  `cfg_value` TEXT NULL,                         -- 可为 JSON 字符串
  `cfg_group` VARCHAR(32) NOT NULL DEFAULT 'general',
  `updated_at` DATETIME NULL ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 官网文章 / 公告（category 区分）
CREATE TABLE IF NOT EXISTS `site_articles` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `slug` VARCHAR(160) NOT NULL UNIQUE,
  `title` VARCHAR(200) NOT NULL,
  `category` VARCHAR(32) NOT NULL DEFAULT 'tutorial',  -- tutorial/science/update/notice
  `cover` VARCHAR(500) NULL,
  `summary` VARCHAR(500) NULL,
  `content` LONGTEXT NULL,
  `content_type` VARCHAR(8) NOT NULL DEFAULT 'html',   -- html/md
  `status` VARCHAR(16) NOT NULL DEFAULT 'draft',       -- draft/published
  `pinned` TINYINT NOT NULL DEFAULT 0,
  `publish_at` DATETIME NULL,
  `seo_title` VARCHAR(200) NULL,
  `seo_desc` VARCHAR(500) NULL,
  `views` INT UNSIGNED NOT NULL DEFAULT 0,
  `created_by` BIGINT NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME NULL ON UPDATE CURRENT_TIMESTAMP,
  KEY `idx_category` (`category`),
  KEY `idx_status` (`status`),
  KEY `idx_publish` (`publish_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 官网使用案例
CREATE TABLE IF NOT EXISTS `site_cases` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `industry` VARCHAR(64) NOT NULL DEFAULT '通用',
  `title` VARCHAR(200) NOT NULL,
  `summary` VARCHAR(500) NULL,
  `content` LONGTEXT NULL,
  `avatar` VARCHAR(500) NULL,
  `metrics` TEXT NULL,                           -- JSON 数组
  `sort` INT NOT NULL DEFAULT 0,
  `status` VARCHAR(16) NOT NULL DEFAULT 'published',
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  `updated_at` DATETIME NULL ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 官网导航菜单
CREATE TABLE IF NOT EXISTS `site_nav` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `position` VARCHAR(16) NOT NULL DEFAULT 'top', -- top/footer
  `label` VARCHAR(64) NOT NULL,
  `url` VARCHAR(500) NOT NULL,
  `target` VARCHAR(16) NOT NULL DEFAULT '_self',
  `sort` INT NOT NULL DEFAULT 0,
  `visible` TINYINT NOT NULL DEFAULT 1,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 官网联系方式
CREATE TABLE IF NOT EXISTS `site_contacts` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `type` VARCHAR(32) NOT NULL DEFAULT 'other',   -- wechat/qq/email/phone/other
  `name` VARCHAR(64) NOT NULL,
  `value` VARCHAR(255) NULL,
  `qrcode` VARCHAR(500) NULL,
  `sort` INT NOT NULL DEFAULT 0,
  `enabled` TINYINT NOT NULL DEFAULT 1,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 官网媒体库
CREATE TABLE IF NOT EXISTS `site_media` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `filename` VARCHAR(255) NOT NULL,
  `path` VARCHAR(500) NOT NULL,
  `size` INT UNSIGNED NOT NULL DEFAULT 0,
  `mime` VARCHAR(64) NULL,
  `width` INT UNSIGNED NULL,
  `height` INT UNSIGNED NULL,
  `uploaded_by` BIGINT NULL,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 官网下载地址（GitHub / 网盘 / 镜像站）
CREATE TABLE IF NOT EXISTS `site_download_mirrors` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `name` VARCHAR(64) NOT NULL,
  `url` VARCHAR(500) NOT NULL,
  `type` VARCHAR(32) NOT NULL DEFAULT 'other',   -- github/netdisk/mirror/other
  `sort` INT NOT NULL DEFAULT 0,
  `enabled` TINYINT NOT NULL DEFAULT 1,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 官网访问控制规则
CREATE TABLE IF NOT EXISTS `site_access_rules` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `type` VARCHAR(16) NOT NULL,                   -- ip_black/ip_white/ua
  `pattern` VARCHAR(255) NOT NULL,
  `note` VARCHAR(255) NULL,
  `enabled` TINYINT NOT NULL DEFAULT 1,
  `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 官网访问统计（按天）
CREATE TABLE IF NOT EXISTS `site_stats_daily` (
  `id` BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  `stat_date` DATE NOT NULL UNIQUE,
  `pv` INT UNSIGNED NOT NULL DEFAULT 0,
  `uv` INT UNSIGNED NOT NULL DEFAULT 0,
  `downloads` INT UNSIGNED NOT NULL DEFAULT 0,
  `buy_clicks` INT UNSIGNED NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
