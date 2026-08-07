#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""自动更新 dist/update.json：
   - 从 release 资产目录找 *.exe，计算 SHA-256 与文件大小
   - 从 tag（vX.Y.Z）解析版本号与 latestVersionCode（major*10000+minor*100+patch）
   - 从 release body 提取 changelog（每行一条，兼容 "- " / "* " markdown 列表前缀）
   - 更新 downloadUrl / releaseDate / downloadSha256 / downloadSize
用法：update_manifest.py <assets_dir> <body_file> <tag> <published_at>
"""
import hashlib
import json
import os
import re
import sys

assets_dir, body_file, tag, published_at = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

# 1) 找 exe 资产
exe_files = [f for f in os.listdir(assets_dir) if f.lower().endswith(".exe")]
if not exe_files:
    print("::error::no exe asset found in release")
    sys.exit(1)
exe_name = exe_files[0]
exe_path = os.path.join(assets_dir, exe_name)

# 2) SHA-256 + 大小
sha256 = hashlib.sha256()
with open(exe_path, "rb") as f:
    for chunk in iter(lambda: f.read(64 * 1024), b""):
        sha256.update(chunk)
sha_hex = sha256.hexdigest()
size = os.path.getsize(exe_path)

# 3) 版本号解析：v2.8.0 -> 20800（与 MainApp.kVerCode 规则一致）
m = re.match(r"^[vV]?(\d+)\.(\d+)\.(\d+)", tag)
if not m:
    print(f"::error::tag format invalid: {tag}, expected vX.Y.Z")
    sys.exit(1)
major, minor, patch = int(m.group(1)), int(m.group(2)), int(m.group(3))
version_code = major * 10000 + minor * 100 + patch

# 4) changelog：每行一条，去掉 markdown 列表符号与首尾空白
changelog = []
if os.path.exists(body_file):
    with open(body_file, "r", encoding="utf-8") as f:
        for line in f:
            text = line.strip()
            text = re.sub(r"^[-*#>\s]+", "", text).strip()
            if text:
                changelog.append(text)

# 5) 发布日期：取 UTC 日期部分（与客户端显示格式 "2026/8/7" 一致，去前导零）
year, month, day = published_at[:10].split("-")
release_date = f"{year}/{int(month)}/{int(day)}"

# 6) 更新清单（保留其它字段如 minSkipVersionCode / forceUpdate）
with open("dist/update.json", "r", encoding="utf-8") as f:
    manifest = json.load(f)

manifest["latestVersion"] = f"v{major}.{minor}.{patch}"
manifest["latestVersionCode"] = version_code
manifest["releaseDate"] = release_date
manifest["downloadUrl"] = (
    f"https://github.com/shushuhao01/ebox/releases/download/{tag}/{exe_name}"
)
manifest["downloadSha256"] = sha_hex
manifest["downloadSize"] = size
manifest["changelog"] = changelog

with open("dist/update.json", "w", encoding="utf-8") as f:
    json.dump(manifest, f, ensure_ascii=False, indent=4)
    f.write("\n")

print(f"updated: v{major}.{minor}.{patch} (code={version_code}), sha256={sha_hex[:16]}..., size={size}")
