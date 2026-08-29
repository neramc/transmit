#include "core/continuity/SelectionCodec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/utils/Conversions.h"

namespace transmit::core {
namespace {

QJsonArray fromStringList(const QStringList& values) {
    QJsonArray array;
    for (const QString& value : values) {
        array.append(value);
    }
    return array;
}

QStringList toStringList(const QJsonValue& value) {
    QStringList list;
    for (const QJsonValue& item : value.toArray()) {
        if (item.isString()) {
            list << item.toString();
        }
    }
    return list;
}

QJsonArray fromStringSet(const QSet<QString>& values) {
    // Sorted, so the same selection always produces the same bytes. A document
    // that reorders itself between runs is useless in version control and
    // useless for telling two captures apart.
    QStringList sorted(values.constBegin(), values.constEnd());
    sorted.sort();
    return fromStringList(sorted);
}

QSet<QString> toStringSet(const QJsonValue& value) {
    QSet<QString> set;
    for (const QJsonValue& item : value.toArray()) {
        if (item.isString()) {
            set.insert(item.toString().toLower());
        }
    }
    return set;
}

QString nameOfPreset(format::CompressionPreset preset) {
    return fromUtf8(format::presetName(preset));
}

QJsonObject encodeScope(const ScopeRule& scope) {
    QJsonObject object;
    if (scope.maximumFileSize > 0) {
        object.insert(QStringLiteral("maximumFileSize"),
                      static_cast<qint64>(scope.maximumFileSize));
    }
    if (scope.minimumFileSize > 0) {
        object.insert(QStringLiteral("minimumFileSize"),
                      static_cast<qint64>(scope.minimumFileSize));
    }
    if (!scope.includeExtensions.isEmpty()) {
        object.insert(QStringLiteral("includeExtensions"), fromStringSet(scope.includeExtensions));
    }
    if (!scope.excludeExtensions.isEmpty()) {
        object.insert(QStringLiteral("excludeExtensions"), fromStringSet(scope.excludeExtensions));
    }
    if (scope.modifiedSince.isValid()) {
        object.insert(QStringLiteral("modifiedSince"), scope.modifiedSince.toString(Qt::ISODate));
    }
    if (scope.modifiedBefore.isValid()) {
        object.insert(QStringLiteral("modifiedBefore"), scope.modifiedBefore.toString(Qt::ISODate));
    }
    if (!scope.excludePatterns.isEmpty()) {
        object.insert(QStringLiteral("exclude"), fromStringList(scope.excludePatterns));
    }
    if (scope.followSymlinks) {
        object.insert(QStringLiteral("followSymlinks"), true);
    }
    if (!scope.includeHidden) {
        object.insert(QStringLiteral("includeHidden"), false);
    }
    return object;
}

ScopeRule decodeScope(const QJsonObject& object) {
    ScopeRule scope;
    scope.maximumFileSize = static_cast<quint64>(
        std::max<qint64>(0, object.value(QStringLiteral("maximumFileSize")).toInteger(0)));
    scope.minimumFileSize = static_cast<quint64>(
        std::max<qint64>(0, object.value(QStringLiteral("minimumFileSize")).toInteger(0)));
    scope.includeExtensions = toStringSet(object.value(QStringLiteral("includeExtensions")));
    scope.excludeExtensions = toStringSet(object.value(QStringLiteral("excludeExtensions")));
    if (object.contains(QStringLiteral("modifiedSince"))) {
        scope.modifiedSince = QDateTime::fromString(
            object.value(QStringLiteral("modifiedSince")).toString(), Qt::ISODate);
    }
    if (object.contains(QStringLiteral("modifiedBefore"))) {
        scope.modifiedBefore = QDateTime::fromString(
            object.value(QStringLiteral("modifiedBefore")).toString(), Qt::ISODate);
    }
    scope.excludePatterns = toStringList(object.value(QStringLiteral("exclude")));
    scope.followSymlinks = object.value(QStringLiteral("followSymlinks")).toBool(false);
    scope.includeHidden = object.value(QStringLiteral("includeHidden")).toBool(true);
    return scope;
}

QString appModeName(AppSelectionMode mode) {
    switch (mode) {
        case AppSelectionMode::None:
            return QStringLiteral("none");
        case AppSelectionMode::Explicit:
            return QStringLiteral("explicit");
        case AppSelectionMode::All:
            break;
    }
    return QStringLiteral("all");
}

AppSelectionMode appModeFromName(const QString& name) {
    if (name == QLatin1String("none"))
        return AppSelectionMode::None;
    if (name == QLatin1String("explicit"))
        return AppSelectionMode::Explicit;
    return AppSelectionMode::All;
}

}  // namespace

QByteArray SelectionCodec::encode(const CaptureDocument& document) {
    QJsonObject root;
    root.insert(QStringLiteral("selectionVersion"), kVersion);
    if (!document.label.isEmpty()) {
        root.insert(QStringLiteral("label"), document.label);
    }

    QStringList domains;
    for (const format::DomainId domain : format::allDomains()) {
        if (document.selection.includes(domain)) {
            domains << fromUtf8(format::domainName(domain));
        }
    }
    root.insert(QStringLiteral("domains"), fromStringList(domains));

    const QJsonObject scope = encodeScope(document.selection.scope);
    if (!scope.isEmpty()) {
        root.insert(QStringLiteral("scope"), scope);
    }

    QJsonArray roots;
    for (const CaptureRoot& capture : document.selection.roots) {
        QJsonObject object;
        object.insert(QStringLiteral("token"), fromUtf8(format::tokenName(capture.token)));
        if (!capture.relative.isEmpty()) {
            object.insert(QStringLiteral("relative"), capture.relative);
        }
        object.insert(QStringLiteral("domain"), fromUtf8(format::domainName(capture.domain)));
        if (!capture.appId.isEmpty()) {
            object.insert(QStringLiteral("appId"), capture.appId);
        }
        if (!capture.stateRootId.isEmpty()) {
            object.insert(QStringLiteral("stateRoot"), capture.stateRootId);
        }
        if (!capture.recursive) {
            object.insert(QStringLiteral("recursive"), false);
        }
        if (capture.isFallback) {
            object.insert(QStringLiteral("fallback"), true);
        }
        if (!capture.excludePatterns.isEmpty()) {
            object.insert(QStringLiteral("exclude"), fromStringList(capture.excludePatterns));
        }
        const QJsonObject rootScope = encodeScope(capture.scope);
        if (!rootScope.isEmpty()) {
            object.insert(QStringLiteral("scope"), rootScope);
        }
        roots.append(object);
    }
    root.insert(QStringLiteral("roots"), roots);

    QJsonObject apps;
    apps.insert(QStringLiteral("mode"), appModeName(document.selection.appMode));
    QJsonArray entries;
    for (const AppSelection& app : document.selection.apps) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), app.appId);
        object.insert(QStringLiteral("captureState"), app.captureState);
        object.insert(QStringLiteral("recordForReinstall"), app.recordForReinstall);
        if (!app.stateRootIds.isEmpty()) {
            object.insert(QStringLiteral("stateRoots"), fromStringList(app.stateRootIds));
        }
        const QJsonObject appScope = encodeScope(app.scope);
        if (!appScope.isEmpty()) {
            object.insert(QStringLiteral("scope"), appScope);
        }
        entries.append(object);
    }
    if (!entries.isEmpty()) {
        apps.insert(QStringLiteral("entries"), entries);
    }
    root.insert(QStringLiteral("apps"), apps);

    QJsonObject packaging;
    packaging.insert(QStringLiteral("preset"), nameOfPreset(document.packaging.preset));
    packaging.insert(QStringLiteral("partSize"), static_cast<qint64>(document.packaging.partSize));
    packaging.insert(QStringLiteral("blockSize"),
                     static_cast<qint64>(document.packaging.solidBlockSize));
    packaging.insert(QStringLiteral("workers"), document.packaging.workerCount);
    packaging.insert(QStringLiteral("syncIntervalBytes"),
                     static_cast<qint64>(document.packaging.syncIntervalBytes));
    packaging.insert(QStringLiteral("verifyAfterWriting"), document.packaging.verifyAfterWriting);
    packaging.insert(QStringLiteral("recordMd5"), document.packaging.recordMd5);
    packaging.insert(QStringLiteral("writeMd5Sidecar"), document.packaging.writeMd5Sidecar);
    packaging.insert(QStringLiteral("sidecarNamesEvenWhenEncrypted"),
                     document.packaging.sidecarNamesEvenWhenEncrypted);
    root.insert(QStringLiteral("packaging"), packaging);

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool SelectionCodec::decode(const QByteArray& json, CaptureDocument& document,
                            QString* errorMessage) {
    const auto fail = [errorMessage](const QString& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    QJsonParseError parseError{};
    const QJsonDocument parsed = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(parseError.errorString());
    }
    if (!parsed.isObject()) {
        return fail(QObject::tr("A selection file has to be a JSON object."));
    }

    const QJsonObject root = parsed.object();
    const int version = root.value(QStringLiteral("selectionVersion")).toInt(kVersion);
    if (version > kVersion) {
        // Refused rather than read as far as it goes. A newer document may
        // hold a choice this build does not know about, and quietly ignoring
        // it would capture something other than what it says.
        return fail(QObject::tr("This selection was written by a newer version of Transmit "
                                "(document version %1, this build understands %2).")
                        .arg(version)
                        .arg(kVersion));
    }

    document.label = root.value(QStringLiteral("label")).toString();

    document.selection.domains.clear();
    for (const QString& name : toStringList(root.value(QStringLiteral("domains")))) {
        const auto domain = format::domainFromName(toUtf8(name));
        if (!domain) {
            return fail(QObject::tr("\"%1\" is not a kind of data Transmit knows.").arg(name));
        }
        document.selection.domains.insert(static_cast<int>(*domain));
    }
    if (document.selection.domains.isEmpty()) {
        return fail(QObject::tr("A selection has to name at least one kind of data."));
    }

    document.selection.scope = decodeScope(root.value(QStringLiteral("scope")).toObject());

    document.selection.roots.clear();
    for (const QJsonValue& value : root.value(QStringLiteral("roots")).toArray()) {
        const QJsonObject object = value.toObject();

        const QString tokenText = object.value(QStringLiteral("token")).toString();
        const auto token = format::tokenFromName(toUtf8(tokenText));
        if (!token) {
            return fail(QObject::tr("\"%1\" is not a folder Transmit knows.").arg(tokenText));
        }

        CaptureRoot capture;
        capture.token = *token;
        capture.relative = object.value(QStringLiteral("relative")).toString();
        if (capture.relative.contains(QStringLiteral(".."))) {
            // A selection is a file somebody may have been sent. A root that
            // climbs out of the folder it names would read from somewhere the
            // person never chose.
            return fail(
                QObject::tr("A capture root may not contain \"..\": %1").arg(capture.relative));
        }

        const QString domainText = object.value(QStringLiteral("domain")).toString();
        const auto domain = format::domainFromName(toUtf8(domainText));
        capture.domain = domain ? *domain : DomainId::UserData;

        capture.appId = object.value(QStringLiteral("appId")).toString();
        capture.stateRootId = object.value(QStringLiteral("stateRoot")).toString();
        capture.recursive = object.value(QStringLiteral("recursive")).toBool(true);
        capture.isFallback = object.value(QStringLiteral("fallback")).toBool(false);
        capture.excludePatterns = toStringList(object.value(QStringLiteral("exclude")));
        capture.scope = decodeScope(object.value(QStringLiteral("scope")).toObject());
        document.selection.roots.push_back(capture);
    }

    const QJsonObject apps = root.value(QStringLiteral("apps")).toObject();
    document.selection.appMode = appModeFromName(apps.value(QStringLiteral("mode")).toString());
    document.selection.apps.clear();
    for (const QJsonValue& value : apps.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject object = value.toObject();
        AppSelection app;
        app.appId = object.value(QStringLiteral("id")).toString();
        if (app.appId.isEmpty()) {
            return fail(QObject::tr("An application entry has no id."));
        }
        app.captureState = object.value(QStringLiteral("captureState")).toBool(true);
        app.recordForReinstall = object.value(QStringLiteral("recordForReinstall")).toBool(true);
        app.stateRootIds = toStringList(object.value(QStringLiteral("stateRoots")));
        app.scope = decodeScope(object.value(QStringLiteral("scope")).toObject());
        document.selection.apps.push_back(app);
    }

    const QJsonObject packaging = root.value(QStringLiteral("packaging")).toObject();
    if (packaging.contains(QStringLiteral("preset"))) {
        const QString name = packaging.value(QStringLiteral("preset")).toString();
        const auto preset = format::presetFromName(toUtf8(name));
        if (!preset) {
            return fail(QObject::tr("\"%1\" is not a compression setting Transmit has.").arg(name));
        }
        document.packaging.preset = *preset;
    }
    document.packaging.partSize = static_cast<quint64>(
        std::max<qint64>(0, packaging.value(QStringLiteral("partSize")).toInteger(0)));
    document.packaging.solidBlockSize = static_cast<quint64>(
        std::max<qint64>(1, packaging.value(QStringLiteral("blockSize"))
                                .toInteger(static_cast<qint64>(format::kDefaultSolidBlockSize))));
    document.packaging.workerCount =
        std::max(0, packaging.value(QStringLiteral("workers")).toInt(0));
    document.packaging.syncIntervalBytes = static_cast<quint64>(
        std::max<qint64>(0, packaging.value(QStringLiteral("syncIntervalBytes")).toInteger(0)));
    document.packaging.verifyAfterWriting =
        packaging.value(QStringLiteral("verifyAfterWriting")).toBool(true);
    document.packaging.recordMd5 = packaging.value(QStringLiteral("recordMd5")).toBool(true);
    document.packaging.writeMd5Sidecar =
        packaging.value(QStringLiteral("writeMd5Sidecar")).toBool(true);
    document.packaging.sidecarNamesEvenWhenEncrypted =
        packaging.value(QStringLiteral("sidecarNamesEvenWhenEncrypted")).toBool(false);

    return true;
}

}  // namespace transmit::core
