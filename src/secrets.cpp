#include "secrets.hpp"
#include "model.hpp"
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QSaveFile>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#elif defined(Q_OS_MACOS)
#include <Security/Security.h>
#elif defined(HAVE_LIBSECRET)
#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")
#endif
namespace bs {
QByteArray Secrets::random() {
  QByteArray b(32, Qt::Uninitialized);
  for (int i = 0; i < 32; i += 4) {
    auto n = QRandomGenerator::system()->generate();
    memcpy(b.data() + i, &n, 4);
  }
  return b.toBase64(QByteArray::Base64UrlEncoding |
                    QByteArray::OmitTrailingEquals);
}
#if defined(HAVE_LIBSECRET)
static const SecretSchema schema = {
    "org.obsproject.BroadcastScheduler",
    SECRET_SCHEMA_NONE,
    {{"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
     {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
#endif
void Secrets::write(const QString &name, const QByteArray &value) {
#ifdef Q_OS_WIN
  DATA_BLOB in{DWORD(value.size()),
               reinterpret_cast<BYTE *>(const_cast<char *>(value.constData()))},
      out{};
  if (!CryptProtectData(&in, L"Broadcast Scheduler", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &out))
    throw Error("DPAPI encryption failed");
  QDir().mkpath(directory);
  QSaveFile f(directory + "/" + name + ".dpapi");
  bool ok =
      f.open(QIODevice::WriteOnly) &&
      f.write(reinterpret_cast<char *>(out.pbData), out.cbData) == out.cbData &&
      f.commit();
  LocalFree(out.pbData);
  if (!ok)
    throw Error("Cannot persist encrypted credential");
#elif defined(Q_OS_MACOS)
  auto account = (directory + "/" + name).toUtf8();
  const char *service = "broadcast-scheduler";
  SecKeychainItemRef item = nullptr;
  auto status = SecKeychainFindGenericPassword(
      nullptr, strlen(service), service, account.size(), account.constData(),
      nullptr, nullptr, &item);
  if (status == errSecSuccess) {
    status = SecKeychainItemModifyAttributesAndData(item, nullptr, value.size(),
                                                    value.constData());
    CFRelease(item);
  } else
    status = SecKeychainAddGenericPassword(
        nullptr, strlen(service), service, account.size(), account.constData(),
        value.size(), value.constData(), nullptr);
  if (status != errSecSuccess)
    throw Error("macOS Keychain write failed");
#elif defined(HAVE_LIBSECRET)
  GError *err = nullptr;
  auto account = (directory + "/" + name).toUtf8();
  bool ok = secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT,
                                       "Broadcast Scheduler", value.constData(),
                                       nullptr, &err, "account",
                                       account.constData(), nullptr);
  if (err)
    g_error_free(err);
  if (!ok)
    throw Error("Secret Service unavailable; unlock your desktop keyring");
#else
  Q_UNUSED(name);
  Q_UNUSED(value);
  throw Error("Secure credential storage is unavailable on this build");
#endif
}
QByteArray Secrets::read(const QString &name) const {
#ifdef Q_OS_WIN
  QFile f(directory + "/" + name + ".dpapi");
  if (!f.exists())
    return {};
  if (!f.open(QIODevice::ReadOnly))
    throw Error("Cannot read encrypted credential");
  auto b = f.readAll();
  DATA_BLOB in{DWORD(b.size()), reinterpret_cast<BYTE *>(b.data())}, out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
    throw Error("DPAPI decryption failed");
  QByteArray result(reinterpret_cast<char *>(out.pbData), out.cbData);
  LocalFree(out.pbData);
  return result;
#elif defined(Q_OS_MACOS)
  auto a = (directory + "/" + name).toUtf8();
  void *p = nullptr;
  UInt32 size = 0;
  auto status = SecKeychainFindGenericPassword(
      nullptr, 19, "broadcast-scheduler", a.size(), a.constData(), &size, &p,
      nullptr);
  if (status == errSecItemNotFound)
    return {};
  if (status != errSecSuccess)
    throw Error("macOS Keychain read failed");
  QByteArray b(static_cast<char *>(p), size);
  SecKeychainItemFreeContent(nullptr, p);
  return b;
#elif defined(HAVE_LIBSECRET)
  GError *err = nullptr;
  auto account = (directory + "/" + name).toUtf8();
  auto p = secret_password_lookup_sync(&schema, nullptr, &err, "account",
                                       account.constData(), nullptr);
  if (err) {
    g_error_free(err);
    throw Error("Secret Service unavailable");
  }
  QByteArray b = p ? QByteArray(p) : QByteArray();
  if (p)
    secret_password_free(p);
  return b;
#else
  Q_UNUSED(name);
  return {};
#endif
}
void Secrets::remove(const QString &name) {
#ifdef Q_OS_WIN
  QFile f(directory + "/" + name + ".dpapi");
  if (f.exists() && !f.remove())
    throw Error("Cannot remove encrypted credential");
#elif defined(Q_OS_MACOS)
  auto a = (directory + "/" + name).toUtf8();
  SecKeychainItemRef item = nullptr;
  if (SecKeychainFindGenericPassword(nullptr, 19, "broadcast-scheduler",
                                     a.size(), a.constData(), nullptr, nullptr,
                                     &item) == errSecSuccess) {
    SecKeychainItemDelete(item);
    CFRelease(item);
  }
#elif defined(HAVE_LIBSECRET)
  GError *err = nullptr;
  auto a = (directory + "/" + name).toUtf8();
  secret_password_clear_sync(&schema, nullptr, &err, "account", a.constData(),
                             nullptr);
  if (err) {
    g_error_free(err);
    throw Error("Cannot remove credential from keyring");
  }
#else
  Q_UNUSED(name);
#endif
}
} // namespace bs
