#pragma once

#include <QString>

// API Key 之类的敏感信息存进系统钥匙串：macOS 钥匙串、Windows 凭据管理器、Linux 的 Secret Service（libsecret）。
// Linux 上没有 libsecret 时退回到普通设置文件（明文），secretStoreName() 会说明存在了哪里
QString readSecret(const QString& key);
bool writeSecret(const QString& key, const QString& value); // value 为空表示删除
QString secretStoreName();
