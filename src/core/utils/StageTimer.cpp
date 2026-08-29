#include "core/utils/StageTimer.h"

#include <algorithm>

namespace transmit::core {
namespace {

/// A duration in the units somebody reading a stage list actually wants.
///
/// Not formatDuration: that rounds to whole seconds, which turns every stage
/// of a fast capture into "0 s" and makes the report useless for the thing it
/// exists for. The point of measuring stages is to see milliseconds.
QString describe(double milliseconds) {
    if (milliseconds < 1.0) {
        return QStringLiteral("%1 ms").arg(milliseconds, 0, 'f', 2);
    }
    if (milliseconds < 1000.0) {
        return QStringLiteral("%1 ms").arg(milliseconds, 0, 'f', 0);
    }
    return QStringLiteral("%1 s").arg(milliseconds / 1000.0, 0, 'f', 2);
}

}  // namespace

StageTiming* StageTimer::find(const QString& name) {
    for (StageTiming& stage : stages_) {
        if (stage.name == name) {
            return &stage;
        }
    }
    stages_.push_back(StageTiming{name, 0, 0});
    return &stages_.last();
}

void StageTimer::begin(const QString& name) {
    end();
    current_ = name;
    startedAt_ = std::chrono::steady_clock::now();
    running_ = true;
}

void StageTimer::end() {
    if (!running_) {
        return;
    }
    running_ = false;

    const auto elapsed = std::chrono::steady_clock::now() - startedAt_;
    StageTiming* stage = find(current_);
    stage->nanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    ++stage->count;
    current_.clear();
}

void StageTimer::add(const QString& name, qint64 nanoseconds) {
    StageTiming* stage = find(name);
    stage->nanoseconds += nanoseconds;
    ++stage->count;
}

qint64 StageTimer::measuredNanoseconds() const {
    qint64 total = 0;
    for (const StageTiming& stage : stages_) {
        total += stage.nanoseconds;
    }
    return total;
}

QString StageTimer::summary() const {
    // Longest first: the point of reading this is to find out what to look at,
    // and the order stages happened to run in does not answer that.
    QList<StageTiming> sorted = stages_;
    std::sort(sorted.begin(), sorted.end(), [](const StageTiming& a, const StageTiming& b) {
        return a.nanoseconds > b.nanoseconds;
    });

    QStringList parts;
    parts.reserve(sorted.size());
    for (const StageTiming& stage : sorted) {
        parts.push_back(QStringLiteral("%1 %2").arg(stage.name, describe(stage.milliseconds())));
    }
    return parts.join(QStringLiteral(", "));
}

void StageTimer::clear() {
    stages_.clear();
    current_.clear();
    running_ = false;
}

}  // namespace transmit::core
