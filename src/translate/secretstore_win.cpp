#include "secretstore.h"

#include <QObject>

#include <windows.h>
#include <wincred.h>

namespace {
std::wstring targetName(const QString& key)
{
    return (QStringLiteral("SnapPin/") + key).toStdWString();
}
} // namespace

QString readSecret(const QString& key)
{
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(targetName(key).c_str(), CRED_TYPE_GENERIC, 0, &cred))
        return QString();
    const QString value = QString::fromUtf8(reinterpret_cast<const char*>(cred->CredentialBlob),
                                            qsizetype(cred->CredentialBlobSize));
    CredFree(cred);
    return value;
}

bool writeSecret(const QString& key, const QString& value)
{
    const std::wstring target = targetName(key);
    if (value.isEmpty()) {
        CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
        return true;
    }
    QByteArray utf8 = value.toUtf8();
    CREDENTIALW cred = {};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<wchar_t*>(target.c_str());
    cred.CredentialBlobSize = DWORD(utf8.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(utf8.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    wchar_t user[] = L"SnapPin";
    cred.UserName = user;
    return CredWriteW(&cred, 0);
}

QString secretStoreName()
{
    return QObject::tr("Windows 凭据管理器");
}
