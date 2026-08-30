#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/rewrite/formats/Rewriters.h"
#include "core/utils/Logging.h"

namespace transmit::core::rewriters {
namespace {

constexpr char kBinaryPlistMagic[] = "bplist00";

bool isBinaryPlist(const QByteArray& raw) {
    return raw.startsWith(kBinaryPlistMagic);
}

/// macOS ships plutil, which converts between the binary and XML forms without
/// needing a plist library. On other platforms only XML plists can be handled,
/// which is enough: a binary plist is only ever restored onto macOS.
bool convertPlist(const QString& from, const QString& to, const QString& format) {
    const QString plutil = QStandardPaths::findExecutable(QStringLiteral("plutil"));
    if (plutil.isEmpty()) {
        return false;
    }

    QProcess process;
    process.start(plutil, {QStringLiteral("-convert"), format, QStringLiteral("-o"), to, from});
    return process.waitForFinished(30000) && process.exitCode() == 0;
}

/// Rewrites <string> values whose enclosing <key> the rule asked for.
QByteArray rewriteXmlPlist(const QByteArray& xml, const QStringList& keys,
                           const PathTranslator& translator, const QString& path,
                           const QString& appId, QList<RewriteEdit>& edits) {
    QByteArray output;
    QXmlStreamReader reader(xml);
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(1);

    // The key most recently seen, and whether the reader is inside one now.
    //
    // The second is a flag rather than "pendingKey is not null", which is what
    // this used to be: QString::clear() makes a string null, so the key text
    // was never captured, so no <string> ever matched, so this rewriter
    // silently did nothing at all. Everything restored onto a Mac kept the
    // paths of the machine it came from.
    QString pendingKey;
    bool insideKey = false;
    bool wantNextString = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.hasError()) {
            return {};
        }

        switch (reader.tokenType()) {
            case QXmlStreamReader::StartDocument:
                writer.writeStartDocument(reader.documentVersion().toString());
                break;
            case QXmlStreamReader::DTD:
                writer.writeDTD(reader.text().toString());
                break;
            case QXmlStreamReader::StartElement:
                writer.writeStartElement(reader.name().toString());
                writer.writeAttributes(reader.attributes());
                if (reader.name() == QLatin1String("key")) {
                    insideKey = true;
                    pendingKey.clear();
                } else if (reader.name() == QLatin1String("string")) {
                    wantNextString =
                        !pendingKey.isEmpty() && keys.contains(pendingKey, Qt::CaseInsensitive);
                }
                break;
            case QXmlStreamReader::EndElement:
                writer.writeEndElement();
                if (reader.name() == QLatin1String("key")) {
                    insideKey = false;
                    pendingKey = pendingKey.trimmed();
                } else if (reader.name() == QLatin1String("string")) {
                    wantNextString = false;
                }
                break;
            case QXmlStreamReader::Characters: {
                QString text = reader.text().toString();
                if (wantNextString) {
                    int replacements = 0;
                    const QString rewritten = translator.translateWithin(text, &replacements);
                    if (replacements > 0 && rewritten != text) {
                        edits.append(RewriteEdit{path, pendingKey, text, rewritten, appId});
                        text = rewritten;
                    }
                } else if (insideKey) {
                    // Remember the key text so the following value can be
                    // matched. Appended rather than assigned: a reader is
                    // entitled to hand back one element's text in several
                    // pieces, and a key split across two of them would
                    // otherwise be remembered as only its last piece.
                    pendingKey += text;
                }
                writer.writeCharacters(text);
                break;
            }
            case QXmlStreamReader::Comment:
                writer.writeComment(reader.text().toString());
                break;
            case QXmlStreamReader::EndDocument:
                writer.writeEndDocument();
                break;
            default:
                break;
        }
    }
    return output;
}

}  // namespace

QList<RewriteEdit> rewritePlist(const QString& path, const QStringList& keys,
                                const PathTranslator& translator, const QString& appId) {
    QList<RewriteEdit> edits;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return edits;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QTemporaryDir workspace;
    QByteArray xml = raw;
    const bool binary = isBinaryPlist(raw);

    if (binary) {
        if (!workspace.isValid()) {
            return edits;
        }
        const QString asXml = workspace.filePath(QStringLiteral("plist.xml"));
        if (!convertPlist(path, asXml, QStringLiteral("xml1"))) {
            qCInfo(logRewrite) << "cannot rewrite the binary property list" << path
                               << "- plutil is not available on this system";
            return edits;
        }
        QFile converted(asXml);
        if (!converted.open(QIODevice::ReadOnly)) {
            return edits;
        }
        xml = converted.readAll();
    }

    const QByteArray rewritten = rewriteXmlPlist(xml, keys, translator, path, appId, edits);
    if (edits.isEmpty() || rewritten.isEmpty()) {
        return edits;
    }

    const QString stagedPath = path + QStringLiteral(".transmit-staged");
    if (binary) {
        const QString xmlStaged = workspace.filePath(QStringLiteral("rewritten.xml"));
        QFile intermediate(xmlStaged);
        if (!intermediate.open(QIODevice::WriteOnly)) {
            return {};
        }
        intermediate.write(rewritten);
        intermediate.close();

        // Written back in the form it arrived in, so the application does not
        // suddenly find an XML plist where it expects a binary one.
        if (!convertPlist(xmlStaged, stagedPath, QStringLiteral("binary1"))) {
            return {};
        }
        return edits;
    }

    QFile staged(stagedPath);
    if (!staged.open(QIODevice::WriteOnly)) {
        return {};
    }
    staged.write(rewritten);
    return edits;
}

}  // namespace transmit::core::rewriters
