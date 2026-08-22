#include "platform/linux/LinuxSecretStore.h"

#include <QProcess>
#include <QStandardPaths>

#include <utility>

#include "core/utils/Logging.h"

#if TRANSMIT_HAVE_LIBSECRET
// Qt defines `signals` as a macro meaning `public`, and GDBusInterfaceInfo has
// a member called exactly that. Every project that mixes Qt and GLib hits this.
#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")
#endif

namespace transmit::platform {
namespace {

/// Runs a command and returns its output. Secrets pass through this, so the
/// output is never logged and the arguments never carry a password - anything
/// sensitive goes in on standard input instead.
QString run(const QString& program, const QStringList& arguments, int timeoutMs = 10000) {
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return {};
    }

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs) || process.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

#if !TRANSMIT_HAVE_LIBSECRET
/// Only the fallback path uses this; with libsecret the library writes the
/// entry directly and keeps its original attributes.
bool runWithSecretOnStdin(const QString& program, const QStringList& arguments,
                          const QString& secret) {
    if (QStandardPaths::findExecutable(program).isEmpty()) {
        return false;
    }

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        return false;
    }

    // Passing a password as an argument would expose it to every other process
    // on the machine through the process list.
    QByteArray payload = secret.toUtf8();
    process.write(payload);
    process.closeWriteChannel();
    payload.fill('\0');

    return process.waitForFinished(10000) && process.exitCode() == 0;
}
#endif

bool haveNetworkManager() {
    return !QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty();
}

bool haveSecretTool() {
    return !QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty();
}

/// True when this build can list the keyring, not merely write to it.
constexpr bool canReadKeyring() {
#if TRANSMIT_HAVE_LIBSECRET
    return true;
#else
    return false;
#endif
}

#if TRANSMIT_HAVE_LIBSECRET

/// Frees a GLib-owned pointer of any type through its own free function, so
/// every early return below cannot leak.
template<typename T, void (*Free)(T*)>
class GOwned {
public:
    GOwned() = default;
    explicit GOwned(T* owned) : owned_(owned) {}
    ~GOwned() {
        if (owned_ != nullptr) {
            Free(owned_);
        }
    }
    GOwned(const GOwned&) = delete;
    GOwned& operator=(const GOwned&) = delete;
    GOwned(GOwned&& other) noexcept : owned_(other.owned_) { other.owned_ = nullptr; }
    GOwned& operator=(GOwned&& other) noexcept {
        std::swap(owned_, other.owned_);
        return *this;
    }

    [[nodiscard]] T* get() const { return owned_; }
    [[nodiscard]] T** receive() { return &owned_; }
    explicit operator bool() const { return owned_ != nullptr; }

private:
    T* owned_ = nullptr;
};

void freeError(GError* error) {
    g_error_free(error);
}
void freeItemList(GList* list) {
    g_list_free_full(list, g_object_unref);
}
void freeHashTable(GHashTable* table) {
    g_hash_table_unref(table);
}
void freeSecretValue(SecretValue* value) {
    secret_value_unref(value);
}
void freeString(gchar* text) {
    g_free(text);
}

using ErrorPtr = GOwned<GError, freeError>;
using ItemListPtr = GOwned<GList, freeItemList>;
using HashTablePtr = GOwned<GHashTable, freeHashTable>;
using SecretValuePtr = GOwned<SecretValue, freeSecretValue>;
using StringPtr = GOwned<gchar, freeString>;

QHash<QString, QString> attributesOf(SecretItem* item) {
    QHash<QString, QString> attributes;

    HashTablePtr table(secret_item_get_attributes(item));
    if (!table) {
        return attributes;
    }

    GHashTableIter iterator;
    gpointer key = nullptr;
    gpointer value = nullptr;
    g_hash_table_iter_init(&iterator, table.get());
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        attributes.insert(QString::fromUtf8(static_cast<const gchar*>(key)),
                          QString::fromUtf8(static_cast<const gchar*>(value)));
    }
    return attributes;
}

/// The first of `names` that the item actually carries.
QString firstAttribute(const QHash<QString, QString>& attributes, const QStringList& names) {
    for (const QString& name : names) {
        const auto found = attributes.constFind(name);
        if (found != attributes.constEnd() && !found->isEmpty()) {
            return *found;
        }
    }
    return {};
}

/// Keyring entries do not say what they are, so this reads it off the shape of
/// the attributes each kind of application happens to write.
SecretKind classify(const QHash<QString, QString>& attributes) {
    const QString schema = attributes.value(QStringLiteral("xdg:schema"));

    if (attributes.contains(QStringLiteral("signon_realm")) ||
        schema.contains(QLatin1String("chrome"), Qt::CaseInsensitive) ||
        schema.contains(QLatin1String("chromium"), Qt::CaseInsensitive)) {
        return SecretKind::BrowserLogin;
    }
    if (attributes.contains(QStringLiteral("server")) ||
        attributes.contains(QStringLiteral("domain")) ||
        schema.contains(QLatin1String("Network"), Qt::CaseInsensitive)) {
        return SecretKind::NetworkCredential;
    }
    return SecretKind::ApplicationPassword;
}

/// Enumerates the login keyring.
///
/// This is what secret-tool cannot do: it looks a secret up by attribute and
/// has no way to list. Searching with no attributes at all matches everything,
/// which is the only way to find entries whose attribute names are private to
/// the application that wrote them.
QList<SecretRecord> readLoginKeyring() {
    QList<SecretRecord> records;

    ErrorPtr error;
    SecretService* service = secret_service_get_sync(SECRET_SERVICE_NONE, nullptr, error.receive());
    if (service == nullptr) {
        qCInfo(logSecrets) << "no secret service is running:"
                           << (error ? error.get()->message : "unknown reason");
        return records;
    }

    // Empty attributes match every item. UNLOCK prompts the user, which is the
    // honest behaviour: reading their passwords should require their consent.
    HashTablePtr query(g_hash_table_new(g_str_hash, g_str_equal));
    const auto flags = static_cast<SecretSearchFlags>(SECRET_SEARCH_ALL | SECRET_SEARCH_UNLOCK |
                                                      SECRET_SEARCH_LOAD_SECRETS);

    ItemListPtr items(
        secret_service_search_sync(service, nullptr, query.get(), flags, nullptr, error.receive()));
    if (!items) {
        qCInfo(logSecrets) << "the keyring could not be searched";
        return records;
    }

    for (GList* node = items.get(); node != nullptr; node = node->next) {
        auto* const item = static_cast<SecretItem*>(node->data);

        SecretValue* const value = secret_item_get_secret(item);
        if (value == nullptr) {
            continue;  // locked, or holds nothing
        }

        gsize length = 0;
        const gchar* const text = secret_value_get(value, &length);
        if (text == nullptr || length == 0) {
            continue;
        }

        SecretRecord record;
        record.attributes = attributesOf(item);
        record.kind = classify(record.attributes);
        record.secret = QString::fromUtf8(text, static_cast<qsizetype>(length));

        StringPtr label(secret_item_get_label(item));
        record.label = label ? QString::fromUtf8(label.get()) : QString();

        record.service = firstAttribute(
            record.attributes,
            {QStringLiteral("service"), QStringLiteral("server"), QStringLiteral("signon_realm"),
             QStringLiteral("application"), QStringLiteral("xdg:schema")});
        if (record.service.isEmpty()) {
            record.service = record.label;
        }
        record.account = firstAttribute(record.attributes,
                                        {QStringLiteral("username"), QStringLiteral("account"),
                                         QStringLiteral("user"), QStringLiteral("login")});
        if (record.label.isEmpty()) {
            record.label = record.service;
        }

        records.push_back(std::move(record));
    }

    // Deliberately counts rather than names them: a keyring entry's label is
    // often the site it belongs to.
    qCInfo(logSecrets) << "read" << records.size() << "entries from the login keyring";
    return records;
}

/// Writes an entry back with the attributes it originally had, so the
/// application that stored it can still find it.
///
/// libsecret validates the attributes against a schema, and there is no flag
/// that turns that off - so the schema is built to match whatever this
/// particular entry carries. The attribute names belong to whichever
/// application wrote it, and are not knowable in advance.
bool storeInLoginKeyring(const SecretRecord& record) {
    QHash<QString, QString> toWrite = record.attributes;
    if (toWrite.isEmpty()) {
        toWrite.insert(QStringLiteral("service"), record.service);
        if (!record.account.isEmpty()) {
            toWrite.insert(QStringLiteral("account"), record.account);
        }
    }

    // libsecret adds this itself from the schema name; leaving a copy in the
    // table as well makes it an undeclared attribute and the write is refused.
    const QString schemaName = toWrite.take(QStringLiteral("xdg:schema"));

    // The struct holds a fixed array, with the last slot reserved for the
    // terminator. An entry with more attributes than that does not exist in
    // practice, but truncating beats writing past the end.
    constexpr int kMaxAttributes = 31;

    // The schema points at these, so they have to outlive the call.
    QList<QByteArray> names;
    names.reserve(toWrite.size());
    for (auto entry = toWrite.constBegin();
         entry != toWrite.constEnd() && names.size() < kMaxAttributes; ++entry) {
        names.push_back(entry.key().toUtf8());
    }

    const QByteArray schemaNameUtf8 =
        schemaName.isEmpty() ? QByteArray("org.freedesktop.Secret.Generic") : schemaName.toUtf8();

    SecretSchema schema{};
    schema.name = schemaNameUtf8.constData();
    schema.flags = SECRET_SCHEMA_NONE;
    for (qsizetype i = 0; i < names.size(); ++i) {
        schema.attributes[i].name = names.at(i).constData();
        schema.attributes[i].type = SECRET_SCHEMA_ATTRIBUTE_STRING;
    }
    schema.attributes[names.size()].name = nullptr;

    HashTablePtr attributes(g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free));
    for (const QByteArray& name : names) {
        const QByteArray value = toWrite.value(QString::fromUtf8(name)).toUtf8();
        g_hash_table_insert(attributes.get(), g_strdup(name.constData()),
                            g_strdup(value.constData()));
    }

    const QByteArray label =
        record.label.isEmpty() ? record.service.toUtf8() : record.label.toUtf8();
    const QByteArray password = record.secret.toUtf8();

    ErrorPtr error;
    const gboolean stored = secret_password_storev_sync(
        &schema, attributes.get(), SECRET_COLLECTION_DEFAULT, label.constData(),
        password.constData(), nullptr, error.receive());

    if (stored == FALSE) {
        // Never the entry itself: a label is often the site it belongs to.
        qCWarning(logSecrets) << "the keyring refused an entry:"
                              << (error ? error.get()->message : "no reason given");
    }
    return stored != FALSE;
}

#endif  // TRANSMIT_HAVE_LIBSECRET

}  // namespace

bool LinuxSecretStore::isAvailable() const {
    return haveNetworkManager() || haveSecretTool() || canReadKeyring();
}

QString LinuxSecretStore::describe() const {
    QStringList stores;
    if (haveNetworkManager()) {
        stores << QStringLiteral("NetworkManager");
    }
    if (canReadKeyring() || haveSecretTool()) {
        stores << QStringLiteral("the login keyring");
    }
    return stores.isEmpty() ? QStringLiteral("no credential store found")
                            : stores.join(QStringLiteral(" and "));
}

QList<SecretRecord> LinuxSecretStore::read(bool includeWifi, bool includeApplications) const {
    QList<SecretRecord> records;

    if (includeWifi && haveNetworkManager()) {
        const QString connections =
            run(QStringLiteral("nmcli"),
                {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME,TYPE"),
                 QStringLiteral("connection"), QStringLiteral("show")});

        for (const QString& line : connections.split(u'\n', Qt::SkipEmptyParts)) {
            const QStringList columns = line.split(u':');
            if (columns.size() < 2 || !columns.at(1).contains(QLatin1String("wireless"))) {
                continue;
            }
            const QString name = columns.at(0);

            // -s asks NetworkManager to include secrets; without the rights to
            // read them it returns an empty value rather than failing, and the
            // network is then reported as needing the user's attention.
            const QString passphrase =
                run(QStringLiteral("nmcli"),
                    {QStringLiteral("-s"), QStringLiteral("-g"),
                     QStringLiteral("802-11-wireless-security.psk"), QStringLiteral("connection"),
                     QStringLiteral("show"), name});

            SecretRecord record;
            record.kind = SecretKind::WifiNetwork;
            record.service = name;
            record.label = name;
            record.secret = passphrase;
            records.push_back(std::move(record));
        }
    }

    if (includeApplications) {
#if TRANSMIT_HAVE_LIBSECRET
        records += readLoginKeyring();
#else
        // Without libsecret there is no way to list the keyring: secret-tool
        // looks an entry up by attribute and nothing more. Application
        // passwords kept inside an application's own profile still travel with
        // that profile; the rest stay behind, and the report says so.
        qCInfo(logSecrets) << "built without libsecret - the login keyring cannot be read";
#endif
    }

    return records;
}

ApplyResult LinuxSecretStore::store(const SecretRecord& record) const {
    switch (record.kind) {
        case SecretKind::WifiNetwork: {
            if (!haveNetworkManager()) {
                return {ApplyOutcome::Unsupported,
                        QStringLiteral("NetworkManager is not installed here"),
                        {}};
            }
            if (record.secret.isEmpty()) {
                return {ApplyOutcome::Failed,
                        QStringLiteral("no passphrase was captured for this network"),
                        {}};
            }

            // Adding a system connection needs rights Transmit does not ask
            // for, so the command is handed to the user with the passphrase
            // left out of the process list.
            return {ApplyOutcome::NeedsPrivilege,
                    QStringLiteral("adding a wireless network needs administrator rights"),
                    QStringLiteral("nmcli device wifi connect %1 --ask").arg(record.service)};
        }

        case SecretKind::ApplicationPassword:
        case SecretKind::NetworkCredential:
        case SecretKind::BrowserLogin: {
#if TRANSMIT_HAVE_LIBSECRET
            // The library puts back every attribute the entry originally had.
            // secret-tool below can only write service and account, which is
            // enough to find the entry again by hand but not enough for the
            // application that stored it to recognise its own.
            const bool stored = storeInLoginKeyring(record);
#else
            if (!haveSecretTool()) {
                return {ApplyOutcome::Unsupported,
                        QStringLiteral("no keyring tool is installed here"),
                        {}};
            }
            const bool stored =
                runWithSecretOnStdin(QStringLiteral("secret-tool"),
                                     {QStringLiteral("store"), QStringLiteral("--label"),
                                      record.label, QStringLiteral("service"), record.service,
                                      QStringLiteral("account"), record.account},
                                     record.secret);
#endif

            return stored ? ApplyResult{ApplyOutcome::Applied, {}, {}}
                          : ApplyResult{ApplyOutcome::Failed,
                                        QStringLiteral("the keyring refused it, or is locked"),
                                        {}};
        }
    }
    return {ApplyOutcome::Unsupported, {}, {}};
}

}  // namespace transmit::platform
