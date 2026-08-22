#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "core/rewrite/formats/Rewriters.h"

namespace transmit::core::rewriters {
namespace {

/// Walks a dotted key path ("download.default_directory") and rewrites every
/// string it reaches. A key naming an object or an array is descended into, so
/// one rule can cover a list of profile directories.
bool rewriteValue(QJsonValue& value, const PathTranslator& translator, const QString& path,
                  const QString& appId, const QString& location, QList<RewriteEdit>& edits) {
    if (value.isString()) {
        const QString original = value.toString();
        int replacements = 0;
        const QString rewritten = translator.translateWithin(original, &replacements);
        if (replacements == 0 || rewritten == original) {
            return false;
        }
        edits.append(RewriteEdit{path, location, original, rewritten, appId});
        value = rewritten;
        return true;
    }

    if (value.isArray()) {
        QJsonArray array = value.toArray();
        bool changed = false;
        for (qsizetype i = 0; i < array.size(); ++i) {
            QJsonValue item = array.at(i);
            if (rewriteValue(item, translator, path, appId,
                             QStringLiteral("%1[%2]").arg(location).arg(i), edits)) {
                array.replace(i, item);
                changed = true;
            }
        }
        if (changed) {
            value = array;
        }
        return changed;
    }

    if (value.isObject()) {
        QJsonObject object = value.toObject();
        bool changed = false;
        for (const QString& key : object.keys()) {
            QJsonValue item = object.value(key);
            if (rewriteValue(item, translator, path, appId, location + u'.' + key, edits)) {
                object.insert(key, item);
                changed = true;
            }
        }
        if (changed) {
            value = object;
        }
        return changed;
    }
    return false;
}

bool rewriteAtKeyPath(QJsonObject& root, const QStringList& segments, int depth,
                      const PathTranslator& translator, const QString& path, const QString& appId,
                      const QString& location, QList<RewriteEdit>& edits) {
    if (depth >= segments.size()) {
        return false;
    }

    const QString key = segments.at(depth);
    if (!root.contains(key)) {
        return false;
    }

    QJsonValue value = root.value(key);
    const QString here = location.isEmpty() ? key : location + u'.' + key;

    if (depth == segments.size() - 1) {
        if (rewriteValue(value, translator, path, appId, here, edits)) {
            root.insert(key, value);
            return true;
        }
        return false;
    }

    if (!value.isObject()) {
        return false;
    }
    QJsonObject child = value.toObject();
    if (rewriteAtKeyPath(child, segments, depth + 1, translator, path, appId, here, edits)) {
        root.insert(key, child);
        return true;
    }
    return false;
}

}  // namespace

QList<RewriteEdit> rewriteJson(const QString& path, const QStringList& keys,
                               const PathTranslator& translator, const QString& appId) {
    QList<RewriteEdit> edits;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return edits;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError error{};
    QJsonDocument document = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return edits;
    }

    QJsonObject root = document.object();
    bool changed = false;
    for (const QString& key : keys) {
        if (rewriteAtKeyPath(root, key.split(u'.', Qt::SkipEmptyParts), 0, translator, path, appId,
                             {}, edits)) {
            changed = true;
        }
    }

    if (!changed) {
        return edits;
    }

    // Compact when the original was compact: these files are machine-written,
    // and reflowing one would show up as a spurious change to anything
    // watching it.
    const bool wasIndented = raw.contains("\n  ");
    QFile staged(path + QStringLiteral(".transmit-staged"));
    if (!staged.open(QIODevice::WriteOnly)) {
        return {};
    }
    staged.write(
        QJsonDocument(root).toJson(wasIndented ? QJsonDocument::Indented : QJsonDocument::Compact));
    return edits;
}

}  // namespace transmit::core::rewriters
