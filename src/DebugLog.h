#pragma once
#include <QObject>
#include <QString>

// A process-wide sink for verbose diagnostic messages (version detection,
// downloads, extraction progress, instance operations, launch steps, ...).
// Only active in builds configured with -DGDLAUNCHER_DEBUG_LOGGING=ON; in a
// normal build the GD_DEBUG_LOG macro compiles away to nothing so there's no
// runtime cost. When active, every call both prints to stderr (qDebug) and
// emits message() so the UI can surface it live in the log console.
class DebugLog : public QObject {
    Q_OBJECT
public:
    static DebugLog &instance();
    void log(const QString &category, const QString &message);

signals:
    void message(const QString &line);

private:
    explicit DebugLog(QObject *parent = nullptr) : QObject(parent) {}
};

#ifdef GDLAUNCHER_DEBUG_LOGGING
#define GD_DEBUG_LOG(category, msg) DebugLog::instance().log(QStringLiteral(category), (msg))
constexpr bool kDebugLoggingEnabled = true;
#else
#define GD_DEBUG_LOG(category, msg) do {} while (0)
constexpr bool kDebugLoggingEnabled = false;
#endif
