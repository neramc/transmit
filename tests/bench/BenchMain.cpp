/// Measures what a capture and a restore actually cost, in milliseconds.
///
/// The optimisations planned for the pipeline are only worth taking if
/// they can be shown, and only safe to take if a regression is caught.
/// This is the instrument for both: it generates a corpus from a seed,
/// runs the real services over it, and prints numbers a script can
/// compare against a committed baseline.
///
/// Deliberately not a ctest test. A benchmark that fails the build
/// because a machine was busy teaches everyone to ignore the build.

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTextStream>

#include <algorithm>
#include <vector>

#include "core/services/ExportService.h"
#include "core/services/ImportService.h"
#include "core/services/ProfileService.h"
#include "core/utils/Conversions.h"
#include "format/Serialization.h"
#include "format/hash/Blake2b.h"
#include "platform/PlatformService.h"

namespace {

using namespace transmit;

QTextStream& out() {
    static QTextStream stream(stdout);
    return stream;
}

/// The middle value of a set of runs. A mean is pulled around by the one
/// run where the machine was doing something else; a median is not.
double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) / 2.0;
}

// --------------------------------------------------------------- corpus

/// Deterministic from a seed, so two runs on two machines compare. The
/// three profiles are the three shapes a real capture meets: many small
/// text files, a mixture, and a few large incompressible ones.
struct CorpusProfile {
    QString name;
    int fileCount = 0;
    int minBytes = 0;
    int maxBytes = 0;
    int compressiblePercent = 0;
};

const std::vector<CorpusProfile>& corpusProfiles() {
    static const std::vector<CorpusProfile> kProfiles = {
        {QStringLiteral("small"), 4000, 512, 8192, 100},
        {QStringLiteral("mixed"), 2000, 1024, 262144, 60},
        {QStringLiteral("media"), 60, 1048576, 4194304, 0},
    };
    return kProfiles;
}

int writeCorpus(const QString& directory, const QString& profileName, quint32 seed) {
    const auto& profiles = corpusProfiles();
    const auto match = std::find_if(profiles.begin(), profiles.end(),
                                    [&](const CorpusProfile& p) { return p.name == profileName; });
    if (match == profiles.end()) {
        out() << "unknown corpus profile: " << profileName << Qt::endl;
        return 1;
    }

    // The corpus is a home directory, because that is what a capture is
    // pointed at: the same ProfileService selection the product uses.
    const QString documents = QDir(directory).filePath(QStringLiteral("Documents"));
    QDir().mkpath(documents);

    QRandomGenerator random(seed);
    static const QByteArray kWords =
        "the quick brown fox jumps over a lazy dog while transmit copies it ";

    quint64 written = 0;
    for (int i = 0; i < match->fileCount; ++i) {
        const int size = match->minBytes +
                         static_cast<int>(random.bounded(match->maxBytes - match->minBytes + 1));

        QByteArray content;
        content.reserve(size);
        if (static_cast<int>(random.bounded(100u)) < match->compressiblePercent) {
            while (content.size() < size) {
                content.append(kWords);
            }
            content.truncate(size);
        } else {
            content.resize(size);
            for (int b = 0; b < size; ++b) {
                content[b] = static_cast<char>(random.bounded(256u));
            }
        }

        // A few nested directories, because a flat tree is not what a
        // home directory looks like and the scan cost differs.
        const QString folder =
            QDir(documents).filePath(QStringLiteral("d%1").arg(i % 32, 2, 10, QLatin1Char('0')));
        QDir().mkpath(folder);

        QFile file(QDir(folder).filePath(QStringLiteral("f%1.dat").arg(i)));
        if (!file.open(QIODevice::WriteOnly)) {
            out() << "could not write into " << folder << Qt::endl;
            return 1;
        }
        file.write(content);
        written += static_cast<quint64>(content.size());
    }

    out() << "wrote " << match->fileCount << " files, " << core::formatBytes(written) << " into "
          << directory << Qt::endl;
    return 0;
}

// -------------------------------------------------------------- results

struct RunResult {
    QString name;
    std::vector<double> milliseconds;
    quint64 bytesIn = 0;
    quint64 bytesOut = 0;
    quint64 files = 0;
};

QJsonObject toJson(const RunResult& result) {
    QJsonObject object;
    object[QStringLiteral("name")] = result.name;
    object[QStringLiteral("medianMs")] = median(result.milliseconds);
    object[QStringLiteral("bytesIn")] = static_cast<double>(result.bytesIn);
    object[QStringLiteral("bytesOut")] = static_cast<double>(result.bytesOut);
    object[QStringLiteral("files")] = static_cast<double>(result.files);

    QJsonArray runs;
    for (const double value : result.milliseconds) {
        runs.append(value);
    }
    object[QStringLiteral("runsMs")] = runs;

    const double seconds = median(result.milliseconds) / 1000.0;
    if (seconds > 0.0 && result.bytesIn > 0) {
        object[QStringLiteral("mibPerSecond")] =
            static_cast<double>(result.bytesIn) / seconds / (1024.0 * 1024.0);
    }
    return object;
}

/// How fast the machine running this is, in terms that owe nothing to this
/// project.
///
/// The benchmarks are compared against numbers committed from an earlier run,
/// and shared build machines are not all the same speed - the pool this runs
/// on varies by about a quarter. Measured against a fixed baseline that is a
/// permanent false alarm, and a gate that cries wolf is one everybody learns
/// to re-run until it passes.
///
/// So every run also measures this: a fixed amount of arithmetic over a fixed
/// buffer, using nothing but the standard library. No change to Transmit can
/// move it, which is exactly what makes it useful - divide a measurement by it
/// and what is left is the code rather than the computer.
RunResult measureTheMachine(int repeat) {
    // A megabyte, read forty times. Deliberately the same shape of work as the
    // things being calibrated - memory and simple arithmetic - so that a
    // machine which is slow in the way that matters here is seen to be slow.
    constexpr std::size_t kSize = 1 << 20;
    constexpr int kPasses = 40;
    std::vector<std::uint64_t> buffer(kSize / sizeof(std::uint64_t));
    for (std::size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ULL;
    }

    RunResult result;
    result.name = QStringLiteral("machine/fixed-work");
    result.bytesIn = kSize * kPasses;

    volatile std::uint64_t sink = 0;
    for (int run = 0; run < repeat; ++run) {
        QElapsedTimer timer;
        timer.start();

        std::uint64_t mixed = 0x243F6A8885A308D3ULL;
        for (int pass = 0; pass < kPasses; ++pass) {
            for (const std::uint64_t value : buffer) {
                mixed ^= value;
                mixed *= 0xFF51AFD7ED558CCDULL;
                mixed ^= mixed >> 33;
            }
        }
        const auto nanoseconds = timer.nsecsElapsed();

        sink += mixed;
        result.milliseconds.push_back(static_cast<double>(nanoseconds) / 1e6);
    }
    (void)sink;
    return result;
}

void report(const std::vector<RunResult>& results, const QString& jsonPath) {
    for (const RunResult& result : results) {
        out() << QStringLiteral("%1  %2 ms  %3")
                     .arg(result.name, -28)
                     .arg(median(result.milliseconds), 9, 'f', 1)
                     .arg(result.bytesIn > 0 ? QStringLiteral("%1 MiB/s")
                                                   .arg(static_cast<double>(result.bytesIn) /
                                                            (median(result.milliseconds) / 1000.0) /
                                                            (1024.0 * 1024.0),
                                                        0, 'f', 1)
                                             : QString())
              << Qt::endl;
    }

    if (jsonPath.isEmpty()) {
        return;
    }
    QJsonArray array;
    for (const RunResult& result : results) {
        array.append(toJson(result));
    }
    QJsonObject root;
    root[QStringLiteral("results")] = array;

    QFile file(jsonPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out() << "wrote " << jsonPath << Qt::endl;
    }
}

// ------------------------------------------------------------ the runs

int runCapture(const QString& corpus, const QString& archive, const QString& presetName, int repeat,
               const QString& jsonPath) {
    const auto preset = format::presetFromName(core::toUtf8(presetName));
    if (!preset) {
        out() << "unknown preset: " << presetName << Qt::endl;
        return 1;
    }

    qputenv("HOME", corpus.toUtf8());
    auto platform = platform::PlatformService::create();
    core::ExportService exporter(*platform);

    RunResult result;
    result.name = QStringLiteral("capture/%1").arg(presetName);

    for (int i = 0; i < repeat; ++i) {
        QFile::remove(archive);

        core::ExportRequest request;
        request.destinationPath = archive;
        request.selection =
            core::ProfileService::profileById(QStringLiteral("documents")).selection;
        request.packaging.preset = *preset;

        core::CancelToken token;
        const core::ExportReport report = exporter.run(request, token);
        if (!report.succeeded) {
            out() << "capture failed: " << report.errorMessage << Qt::endl;
            return 1;
        }
        // An empty corpus is the failure that looks most like a success: the
        // capture works, it is instant, and the comparison reports a
        // spectacular improvement. It is measuring nothing.
        if (report.fileCount == 0) {
            out() << "captured nothing from " << corpus
                  << " - is that a corpus directory? Make one with: transmit_bench corpus "
                     "--corpus "
                  << corpus << Qt::endl;
            return 1;
        }

        result.milliseconds.push_back(static_cast<double>(report.elapsedMilliseconds));
        result.bytesIn = report.rawBytes;
        result.bytesOut = report.storedBytes;
        result.files = report.fileCount;
    }

    report({measureTheMachine(repeat), result}, jsonPath);
    return 0;
}

int runRestore(const QString& archive, const QString& into, int repeat, const QString& jsonPath) {
    auto platform = platform::PlatformService::create();
    core::ImportService importer(*platform);

    RunResult result;
    result.name = QStringLiteral("restore");

    for (int i = 0; i < repeat; ++i) {
        QDir(into).removeRecursively();
        QDir().mkpath(into);

        core::ImportRequest request;
        request.archivePath = archive;
        request.destinationOverride = into;
        request.createRollback = false;

        core::CancelToken token;
        const core::ImportReport report = importer.run(request, token);
        if (!report.succeeded) {
            out() << "restore failed: " << report.errorMessage << Qt::endl;
            return 1;
        }
        result.milliseconds.push_back(static_cast<double>(report.elapsedMilliseconds));
        result.bytesIn = report.bytesWritten;
        result.files = report.filesRestored;
    }

    report({measureTheMachine(repeat), result}, jsonPath);
    return 0;
}

/// The pieces every capture runs over every byte. Measured on their own
/// because a change here moves the whole pipeline and the macro number
/// is too noisy to attribute it.
int runMicro(int repeat, const QString& jsonPath) {
    std::vector<RunResult> results;
    volatile unsigned sink = 0;

    // First, so that a comparison has something to divide by even if a later
    // measurement fails.
    results.push_back(measureTheMachine(repeat));

    for (const std::size_t size : {std::size_t{4096}, std::size_t{1 << 20}, std::size_t{1 << 26}}) {
        format::ByteBuffer data(size);
        for (std::size_t i = 0; i < size; ++i) {
            data[i] = static_cast<format::Byte>(i * 31 + (i >> 8));
        }

        RunResult hash;
        hash.name = QStringLiteral("blake2b/%1").arg(core::formatBytes(size));
        hash.bytesIn = size;
        for (int i = 0; i < repeat; ++i) {
            QElapsedTimer timer;
            timer.start();
            const auto digest = format::Blake2b::hash256(data);
            const auto nanoseconds = timer.nsecsElapsed();

            // The compiler must not decide the hash was pointless and
            // remove it, which would leave this timing an empty loop.
            sink += static_cast<unsigned>(digest[0]);
            hash.milliseconds.push_back(static_cast<double>(nanoseconds) / 1e6);
        }
        results.push_back(hash);
    }

    report(results, jsonPath);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("transmit-bench"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Measures capture, restore and the per-byte primitives underneath them.");
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("corpus, capture, restore or micro"));

    const QCommandLineOption outOption(QStringLiteral("out"), QStringLiteral("Where to write."),
                                       QStringLiteral("path"));
    const QCommandLineOption corpusOption(
        QStringLiteral("corpus"), QStringLiteral("Corpus directory."), QStringLiteral("path"));
    const QCommandLineOption archiveOption(QStringLiteral("archive"), QStringLiteral("Archive."),
                                           QStringLiteral("path"));
    const QCommandLineOption intoOption(QStringLiteral("into"), QStringLiteral("Restore into."),
                                        QStringLiteral("path"));
    const QCommandLineOption profileOption(QStringLiteral("profile"),
                                           QStringLiteral("small, mixed or media."),
                                           QStringLiteral("name"), QStringLiteral("mixed"));
    const QCommandLineOption presetOption(QStringLiteral("preset"),
                                          QStringLiteral("Compression preset."),
                                          QStringLiteral("name"), QStringLiteral("balanced"));
    const QCommandLineOption repeatOption(QStringLiteral("repeat"),
                                          QStringLiteral("How many runs to take the median of."),
                                          QStringLiteral("count"), QStringLiteral("5"));
    const QCommandLineOption seedOption(QStringLiteral("seed"), QStringLiteral("Corpus seed."),
                                        QStringLiteral("number"), QStringLiteral("20260829"));
    const QCommandLineOption jsonOption(QStringLiteral("json"),
                                        QStringLiteral("Also write the numbers here."),
                                        QStringLiteral("path"));

    parser.addOptions({outOption, corpusOption, archiveOption, intoOption, profileOption,
                       presetOption, repeatOption, seedOption, jsonOption});
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        parser.showHelp(1);
    }

    const QString command = positional.first();
    const int repeat = std::max(1, parser.value(repeatOption).toInt());

    if (command == QLatin1String("corpus")) {
        // --corpus, the same option every other command uses for the same
        // directory. It used to read --out, so `corpus --corpus /dev/shm/x`
        // wrote two hundred megabytes into the working directory and then the
        // capture measured an empty folder and called it a 99% improvement.
        QString directory = parser.value(corpusOption);
        if (directory.isEmpty()) {
            directory = parser.value(outOption);
        }
        if (directory.isEmpty()) {
            out() << "corpus needs --corpus DIRECTORY" << Qt::endl;
            return 1;
        }
        return writeCorpus(directory, parser.value(profileOption),
                           parser.value(seedOption).toUInt());
    }
    if (command == QLatin1String("capture")) {
        return runCapture(parser.value(corpusOption), parser.value(outOption),
                          parser.value(presetOption), repeat, parser.value(jsonOption));
    }
    if (command == QLatin1String("restore")) {
        return runRestore(parser.value(archiveOption), parser.value(intoOption), repeat,
                          parser.value(jsonOption));
    }
    if (command == QLatin1String("micro")) {
        return runMicro(repeat, parser.value(jsonOption));
    }

    out() << "unknown command: " << command << Qt::endl;
    return 1;
}
