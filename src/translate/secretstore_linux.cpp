#include "secretstore.h"

#include <QLibrary>
#include <QObject>
#include <QSettings>

// 运行时加载 libsecret（GNOME 钥匙环、KWallet 都实现了 Secret Service），不增加编译依赖；
// 下面的结构体与 libsecret-1 的 SecretSchema 布局一致
namespace {

struct SecretSchemaAttribute {
    const char* name;
    int type; // SECRET_SCHEMA_ATTRIBUTE_STRING = 0
};
struct SecretSchema {
    const char* name;
    int flags; // SECRET_SCHEMA_NONE = 0
    SecretSchemaAttribute attributes[32];
    int reserved;
    void* reserved1;
    void* reserved2;
    void* reserved3;
    void* reserved4;
    void* reserved5;
    void* reserved6;
    void* reserved7;
};

const SecretSchema kSchema = {"com.github.wangchaozhi.SnapPin", 0, {{"key", 0}, {nullptr, 0}}, 0,
                              nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

using StoreFn = int (*)(const SecretSchema*, const char* collection, const char* label, const char* password,
                        void* cancellable, void** error, ...);
using LookupFn = char* (*)(const SecretSchema*, void* cancellable, void** error, ...);
using ClearFn = int (*)(const SecretSchema*, void* cancellable, void** error, ...);
using FreeFn = void (*)(char*);
using ErrorFreeFn = void (*)(void*);

struct Lib {
    StoreFn store = nullptr;
    LookupFn lookup = nullptr;
    ClearFn clear = nullptr;
    FreeFn freePassword = nullptr;
    ErrorFreeFn errorFree = nullptr;
    bool ok = false;
};

const Lib& lib()
{
    static const Lib l = [] {
        Lib r;
        QLibrary secret(QStringLiteral("secret-1"), 0);
        QLibrary glib(QStringLiteral("glib-2.0"), 0);
        if (!secret.load() || !glib.load())
            return r;
        r.store = reinterpret_cast<StoreFn>(secret.resolve("secret_password_store_sync"));
        r.lookup = reinterpret_cast<LookupFn>(secret.resolve("secret_password_lookup_sync"));
        r.clear = reinterpret_cast<ClearFn>(secret.resolve("secret_password_clear_sync"));
        r.freePassword = reinterpret_cast<FreeFn>(secret.resolve("secret_password_free"));
        r.errorFree = reinterpret_cast<ErrorFreeFn>(glib.resolve("g_error_free"));
        r.ok = r.store && r.lookup && r.clear && r.freePassword && r.errorFree;
        return r;
    }();
    return l;
}

// 没有 Secret Service 服务（比如没装钥匙环的精简桌面）时调用会失败，此时也退回到设置文件
bool serviceWorks()
{
    static const bool works = [] {
        if (!lib().ok)
            return false;
        void* error = nullptr;
        char* p = lib().lookup(&kSchema, nullptr, &error, "key", "__probe__", nullptr);
        if (p)
            lib().freePassword(p);
        if (error) {
            lib().errorFree(error);
            return false;
        }
        return true;
    }();
    return works;
}

QString settingsKey(const QString& key)
{
    return QStringLiteral("secrets/") + key;
}

} // namespace

QString readSecret(const QString& key)
{
    if (!serviceWorks())
        return QSettings().value(settingsKey(key)).toString();
    void* error = nullptr;
    const QByteArray k = key.toUtf8();
    char* p = lib().lookup(&kSchema, nullptr, &error, "key", k.constData(), nullptr);
    if (error)
        lib().errorFree(error);
    if (!p)
        return QString();
    const QString value = QString::fromUtf8(p);
    lib().freePassword(p);
    return value;
}

bool writeSecret(const QString& key, const QString& value)
{
    if (!serviceWorks()) {
        QSettings s;
        if (value.isEmpty())
            s.remove(settingsKey(key));
        else
            s.setValue(settingsKey(key), value);
        return true;
    }
    void* error = nullptr;
    const QByteArray k = key.toUtf8();
    int ok;
    if (value.isEmpty()) {
        lib().clear(&kSchema, nullptr, &error, "key", k.constData(), nullptr);
        ok = 1;
    } else {
        const QByteArray label = "SnapPin: " + k;
        ok = lib().store(&kSchema, nullptr, label.constData(), value.toUtf8().constData(), nullptr, &error, "key",
                         k.constData(), nullptr);
    }
    if (error) {
        lib().errorFree(error);
        ok = 0;
    }
    return ok != 0;
}

QString secretStoreName()
{
    return serviceWorks() ? QObject::tr("系统密钥环（Secret Service）")
                          : QObject::tr("设置文件（未找到系统密钥环，明文保存）");
}
