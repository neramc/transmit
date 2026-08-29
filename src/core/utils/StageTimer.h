#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <chrono>

namespace transmit::core {

/// One named stretch of a run, and how long it took.
struct StageTiming {
    QString name;
    qint64 nanoseconds = 0;

    /// How many times this stage ran. Stages inside a loop - reading a file,
    /// hashing it - are summed rather than listed thousands of times.
    quint64 count = 0;

    [[nodiscard]] double milliseconds() const {
        return static_cast<double>(nanoseconds) / 1'000'000.0;
    }
};

/// Where the time in a capture or a restore actually went.
///
/// "The capture took four minutes" is not a fact anybody can act on. "Three
/// and a half of them were spent hashing, and eleven seconds writing" is: it
/// says which change would be worth making, and it says whether a change that
/// was made did anything. Every optimisation in this codebase is expected to
/// come with the two numbers it moved.
///
/// Deliberately simple. Nesting, threads and percentiles are what a profiler
/// is for; this is a list of named spans that ships in the release build, so
/// somebody's slow capture on their own machine can be explained without
/// asking them to install anything.
class StageTimer {
public:
    /// Starts, or resumes, a stage. Ending the previous one first.
    void begin(const QString& name);

    /// Ends the current stage. Safe to call when none is running.
    void end();

    /// Adds time to a stage without making it current, for work that happens
    /// inside another stage - hashing inside the read loop, say.
    void add(const QString& name, qint64 nanoseconds);

    /// A stage measured by scope. `end()` is called for you.
    class Scope {
    public:
        Scope(StageTimer& timer, const QString& name) : timer_(&timer) { timer_->begin(name); }
        ~Scope() { timer_->end(); }

        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        StageTimer* timer_;
    };

    [[nodiscard]] QList<StageTiming> stages() const { return stages_; }

    /// Every stage on one line, longest first: "scan 4.1s, hash 2.9s, ...".
    [[nodiscard]] QString summary() const;

    /// Total time in all stages. Not the wall clock of the run - stages do not
    /// have to cover all of it - which is why it is named for what it is.
    [[nodiscard]] qint64 measuredNanoseconds() const;

    void clear();

private:
    [[nodiscard]] StageTiming* find(const QString& name);

    QList<StageTiming> stages_;
    QString current_;
    std::chrono::steady_clock::time_point startedAt_;
    bool running_ = false;
};

}  // namespace transmit::core
