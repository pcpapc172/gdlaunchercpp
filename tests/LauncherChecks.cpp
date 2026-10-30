#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QProgressBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <functional>
#include <iostream>
#include <stdexcept>
#include "ProcessTracker.h"
#include "ProtonManager.h"
#include "NongManager.h"
#include "ui/NongDialog.h"
#include "VersionManager.h"
#include "ui/DownloadsDialog.h"
#include "BackupManager.h"
#include "ui/CloseGuard.h"
#include "ui/BackupsDialog.h"
#include <QTableWidget>
#include <QSpinBox>
#include <QMessageBox>
#include <QLineEdit>
#include <sys/resource.h>
#include "GameLauncher.h"
#include "InstanceManager.h"
#include "Settings.h"
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void put(const QString &path, const QByteArray &bytes) {
    check(QDir().mkpath(QFileInfo(path).absolutePath()), "create fixture directory");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write fixture");
}
QByteArray get(const QString &path) { QFile file(path); check(file.open(QIODevice::ReadOnly), "read fixture"); return file.readAll(); }
bool waitUntil(const std::function<bool()> &condition, int milliseconds = 5000) {
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < milliseconds) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    return condition();
}
bool download(NongManager &manager, const NongSong &song, const QString &directory, QString *error = nullptr,
              bool cancel = false, bool reopenDialog = false) {
    bool finished = false, success = false;
    const QString key = NongManager::operationKey(directory, song.songId);
    auto connection = QObject::connect(&manager, &NongManager::downloadFinished, &manager,
        [&](const QString &resultKey, bool result, const QString &message) {
            if (key != resultKey) return;
            finished = true; success = result;
            if (error) *error = message;
        });
    manager.downloadSong(song, directory);
    if (reopenDialog) {
        for (int reopening = 0; reopening < 2; ++reopening) {
            NongDialog dialog(nullptr, &manager);
            manager.downloadProgress(key, 45, 100);
            int visibleBars = 0, downloadingButtons = 0, blockedAlternatives = 0, cancelButtons = 0;
            for (auto *bar : dialog.findChildren<QProgressBar *>("songProgress")) {
                if (!bar->isHidden()) { ++visibleBars; check(bar->value() == 45, "selected song receives progress"); }
                else check(bar->value() != 45, "other titles sharing the song ID receive no progress");
            }
            for (auto *button : dialog.findChildren<QPushButton *>()) {
                if (button->text() == "Downloading...") ++downloadingButtons;
                if (button->text() == "Same ID downloading...") { ++blockedAlternatives; check(!button->isEnabled(), "same-ID alternatives stay locked"); }
                if (button->text() == "Cancel" && !button->isHidden()) ++cancelButtons;
            }
            check(visibleBars == 1 && downloadingButtons == 1 && blockedAlternatives == 1 && cancelButtons == 1,
                  "only the selected SFH record shows progress and Cancel after reopening");
        }
        check(manager.isDownloading(key), "closing NONG dialog keeps downloads alive");
    }
    if (cancel) manager.cancelDownload(key);
    const bool done = waitUntil([&]() { return finished; }, 20000);
    QObject::disconnect(connection);
    check(done, "NONG download completes");
    return success;
}
void nongChecks(const QString &root) {
    AppSettings settings = Settings::load();
    settings.useProtonGE = false;
    settings.nongDownloadFolder = "%LOCALAPPDATA%/custom";
    Settings::save(settings);
    const QString directory = NongManager::resolveDirectory("%localappdata%\\custom");
    check(directory == Settings::gameSaveDir("custom"), "expand custom localappdata folder in Wine prefix");
    check(NongManager::resolveDirectory("relative/folder").isEmpty(), "reject ambiguous relative folder");
    settings.useProtonGE = true; Settings::save(settings);
    check(NongManager::resolveDirectory("%LOCALAPPDATA%/custom") == Settings::gameSaveDir("custom", true), "expand folder in Proton prefix");
    settings.useProtonGE = false; Settings::save(settings);
    QJsonArray entries{
        QJsonObject{{"_id", "record-a"}, {"songID", "123"}, {"name", "Test Level"}, {"songName", "Fixture Song"},
                    {"filetype", "mp3"}, {"downloads", 30}, {"levelID", QJsonArray{"42"}}},
        QJsonObject{{"_id", "record-b"}, {"songID", "456"}, {"name", "Other Level"}, {"songName", "Other Song"},
                    {"filetype", "ogg"}, {"downloads", 10}},
        QJsonObject{{"_id", "record-remix"}, {"songID", "123"}, {"name", "Another Level"}, {"songName", "Different Remix"},
                    {"filetype", "mp3"}, {"downloads", 20}},
        QJsonObject{{"_id", "record-c"}, {"songID", "../escape"}, {"filetype", "mp3"}}
    };
    const QString catalog = root + "/songs.json";
    put(catalog, QJsonDocument(entries).toJson());
    NongManager manager(nullptr, QUrl::fromLocalFile(catalog));
    bool ready = false;
    QObject::connect(&manager, &NongManager::catalogReady, &manager, [&]() { ready = true; });
    manager.fetchCatalog();
    check(waitUntil([&]() { return ready; }), "parse SongFileHub catalogue asynchronously");
    check(manager.songs().size() == 3 && manager.songs().first().songId == "123", "catalogue filters unsafe IDs and sorts songs");
    check(manager.songs().first().url.toString() == "https://api.songfilehub.com/song/record-a?download=true&name=123", "use SFH song download API");
    auto song = manager.songs().first();
    const QString mp3 = root + "/fixture.mp3";
    const QString ogg = root + "/fixture.ogg";
    put(mp3, "ID3" + QByteArray(1024 * 1024, 'm'));
    put(ogg, "OggS" + QByteArray(1024 * 1024, 'o'));
    song.url = QUrl::fromLocalFile(mp3);
    put(directory + "/123.mp3", "original song");
    int progress = 0;
    QObject::connect(&manager, &NongManager::downloadProgress, &manager, [&](const QString &, qint64, qint64) { ++progress; });
    check(download(manager, song, directory, nullptr, false, true), "NONG downloads survive dialog close");
    check(get(directory + "/123.mp3.original") == "original song", "replacement renames original");
    check(get(directory + "/123.mp3").startsWith("ID3"), "install ID.mp3");
    check(progress > 0, "report NONG progress");
    song.format = "ogg"; song.url = QUrl::fromLocalFile(ogg);
    check(download(manager, song, directory), "switch replacement to OGG");
    check(!QFileInfo::exists(directory + "/123.mp3") && get(directory + "/123.ogg").startsWith("OggS"), "remove conflicting active MP3 when switching formats");
    check(get(directory + "/123.mp3.original") == "original song" && !QFileInfo::exists(directory + "/123.ogg.original"), "repeated replacements preserve first original");
    QString error;
    check(manager.restoreOriginal(directory, "123", &error), "restore original across format change");
    check(get(directory + "/123.mp3") == "original song" && !QFileInfo::exists(directory + "/123.ogg"), "restoration removes replacement and restores original extension");
    check(!NongManager::hasOriginal(directory, "123"), "restore consumes original backup");
    put(root + "/not-a-song.mp3", "<html>server error</html>");
    song.format = "mp3"; song.url = QUrl::fromLocalFile(root + "/not-a-song.mp3");
    check(!download(manager, song, directory, &error) && get(directory + "/123.mp3") == "original song", "invalid response leaves original untouched");
    song.url = QUrl::fromLocalFile(mp3);
    check(!download(manager, song, directory, &error, true) && error == "Cancelled", "cancel NONG download");
    check(get(directory + "/123.mp3") == "original song", "cancellation leaves original untouched");
    const QString otherDirectory = root + "/another-custom-folder";
    check(download(manager, song, otherDirectory), "download to independent custom folder");
    check(!NongManager::hasOriginal(otherDirectory, "123") && get(directory + "/123.mp3") == "original song", "custom-folder downloads do not touch other folders");
    check(QDir(directory).entryList({".nong-*"}, QDir::Files | QDir::Hidden).isEmpty(), "temporary downloads cleaned up");
    for (const auto &theme : {QString("Dark"), QString("Light")}) {
        settings.theme = theme; Settings::save(settings);
        Theme::apply(Theme::fromSettingsString(theme));
        NongDialog dialog(nullptr, &manager);
        dialog.show(); QApplication::processEvents();
        auto *folder = dialog.findChild<QLineEdit *>("nongFolder");
        check(folder && folder->text() == "%LOCALAPPDATA%/custom", "custom folder preference persists");
        dialog.grab().save(QCoreApplication::applicationDirPath() + "/nong-" + theme + ".png");
    }
    {
        NongDialog dialog(nullptr, &manager);
        auto *status = dialog.findChild<QLabel *>("nongStatus");
        put(catalog, "{broken json");
        manager.fetchCatalog();
        check(waitUntil([&]() { return status->text().contains("Could not load SongFileHub"); }), "NONG fetch failure is visible");
        check(manager.songs().size() == 3, "failed NONG refresh keeps prior catalogue");
        check(dialog.findChild<QPushButton *>("nongRetry")->isEnabled(), "NONG error offers Retry");
    }
    const QString versions = root + "/versions.json";
    put(versions, QJsonDocument(QJsonArray{QJsonObject{{"category", "remote"}, {"version", "fixture"},
        {"name", "Fixture Version"}, {"url", QUrl::fromLocalFile(root + "/missing.zip").toString()}}}).toJson());
    VersionManager versionManager(nullptr, QUrl::fromLocalFile(versions));
    DownloadsDialog versionDialog(nullptr, &versionManager);
    auto *fetchStatus = versionDialog.findChild<QLabel *>("versionFetchStatus");
    check(waitUntil([&]() { return fetchStatus->text().contains("1 versions"); }), "version catalogue loads");
    put(versions, "<html>error</html>");
    versionManager.fetchRemoteVersions();
    check(waitUntil([&]() { return fetchStatus->text().contains("Invalid version catalogue"); }), "version parse failure is visible");
    check(versionDialog.findChild<QLabel *>("sizeLabel") != nullptr, "failed version refresh keeps prior cards");
    check(versionDialog.findChild<QPushButton *>("versionRetry")->isEnabled(), "version error offers Retry");
}
void liveSfhCheck() {
    QTemporaryDir temporary;
    NongManager manager;
    bool ready = false;
    QString error;
    QObject::connect(&manager, &NongManager::catalogReady, &manager, [&]() { ready = true; });
    QObject::connect(&manager, &NongManager::catalogError, &manager, [&](const QString &message) { error = message; });
    manager.fetchCatalog();
    check(waitUntil([&]() { return ready || !error.isEmpty(); }, 90000), "live SFH catalogue request completes");
    if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
    check(!manager.songs().isEmpty(), "live SFH catalogue is populated");
    auto song = manager.songs().first();
    for (const auto &candidate : manager.songs()) if (candidate.recordId == "6ad1c23ecc40bb69f37264f9") song = candidate;
    check(download(manager, song, temporary.path(), &error), "download a real SFH song through redirects");
    const QString file = temporary.path() + "/" + song.songId + "." + song.format;
    check(QFileInfo(file).size() > 1000, "live SFH song contains audio");
    std::cout << "PASS: live SFH API (" << manager.songs().size() << " songs), download "
              << song.songId.toStdString() << "." << song.format.toStdString()
              << " (" << QFileInfo(file).size() << " bytes)\n";
}
void start(QProcess &process, const QString &path, const QStringList &args, const QString &token = {}) {
    auto env = QProcessEnvironment::systemEnvironment();
    if (!token.isEmpty()) env.insert("GDLAUNCHER_SESSION", token);
    else env.remove("GDLAUNCHER_SESSION");
    process.setProcessEnvironment(env);
    process.start(path, args);
    check(process.waitForStarted(), "start fixture process");
}
void checks(const QString &self) {
    auto checkCloseConfirmation = [](bool force, bool confirmForce, bool allowForce = true, bool confirmTwice = true) {
        int firstDialogs = 0, secondDialogs = 0;
        QSet<QString> handled;
        QTimer responder;
        QObject::connect(&responder, &QTimer::timeout, &responder, [&]() {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box || handled.contains(box->windowTitle())) return;
            handled.insert(box->windowTitle());
            if (box->windowTitle() == "Cannot Close GDLauncher") {
                ++firstDialogs;
                check(box->defaultButton()->objectName() == "closeGuardStay", "closing stays blocked by default");
                if (!allowForce) check(!box->findChild<QPushButton *>("closeGuardForce"), "force close can be disabled");
                box->findChild<QPushButton *>(force ? "closeGuardForce" : "closeGuardStay")->click();
            } else if (box->windowTitle() == "Confirm Force Close") {
                ++secondDialogs;
                check(box->defaultButton()->objectName() == "closeGuardCancel", "force-close confirmation defaults to staying open");
                box->findChild<QPushButton *>(confirmForce ? "closeGuardConfirm" : "closeGuardCancel")->click();
            }
        });
        responder.start(5);
        const bool allowed = CloseGuard::confirmForceClose(nullptr, "A game or file operation is still active.", allowForce, confirmTwice);
        responder.stop();
        check(allowed == (force && allowForce && (!confirmTwice || confirmForce)) &&
              firstDialogs == 1 && secondDialogs == (force && allowForce && confirmTwice ? 1 : 0),
              "force close requires two explicit confirmations");
    };
    checkCloseConfirmation(false, false);
    checkCloseConfirmation(true, false);
    checkCloseConfirmation(true, true);
    checkCloseConfirmation(false, false, false);
    checkCloseConfirmation(true, false, true, false);
    struct rlimit originalLimit;
    check(getrlimit(RLIMIT_NOFILE, &originalLimit) == 0, "read descriptor limits");
    struct rlimit desktopLimit = originalLimit;
    desktopLimit.rlim_cur = qMin<rlim_t>(1024, desktopLimit.rlim_max);
    check(setrlimit(RLIMIT_NOFILE, &desktopLimit) == 0, "simulate desktop descriptor limit");
    QProcess runtimeCheck;
    ProtonManager::configureProcess(&runtimeCheck);
    runtimeCheck.start("/bin/sh", {"-c", "ulimit -n"});
    const bool completedLimitCheck = runtimeCheck.waitForFinished();
    struct rlimit parentLimit;
    getrlimit(RLIMIT_NOFILE, &parentLimit);
    setrlimit(RLIMIT_NOFILE, &originalLimit);
    check(completedLimitCheck && runtimeCheck.exitCode() == 0 &&
          runtimeCheck.readAllStandardOutput().trimmed().toULongLong() == originalLimit.rlim_max,
          "runtime child inherits the permitted hard descriptor limit");
    check(parentLimit.rlim_cur == desktopLimit.rlim_cur, "runtime setup leaves parent descriptor limit unchanged");
    QTemporaryDir temporary;
    check(temporary.isValid(), "create isolated test home");
    qputenv("HOME", temporary.path().toUtf8());
    const QString originalPath = qEnvironmentVariable("PATH");
    const QString bin = temporary.path() + "/bin";
    const QString game = bin + "/GeometryDash.exe";
    check(QDir().mkpath(bin) && QFile::link(self, game), "create named game fixture");

    QProcess unrelated;
    start(unrelated, game, {"--sleep"});
    ProcessTracker tracker;
    check(!tracker.isRunning("GeometryDash.exe"), "unrelated process must be excluded");
    QProcess own;
    tracker.configureProcess(&own);
    start(own, game, {"--sleep"}, tracker.token());
    check(tracker.isRunning("GeometryDash.exe"), "own process must be recognized");
    check(!tracker.isRunning("Other.exe"), "custom name must match exactly");
    tracker.terminate();
    check(own.waitForFinished() && unrelated.state() != QProcess::NotRunning, "termination must spare unrelated game");
    check(!tracker.isRunning("GeometryDash.exe"), "unrelated process must not keep tracking alive");

    // A short-lived wrapper can exit before the first poll; its detached child
    // retains the launch token and must still be owned.
    tracker.reset();
    QProcess wrapper;
    start(wrapper, "/bin/sh", {"-c", "\"$1\" --sleep >/dev/null 2>&1 &", "fixture", game}, tracker.token());
    check(wrapper.waitForFinished(), "wrapper exits");
    check(waitUntil([&]() { return tracker.isRunning("GeometryDash.exe"); }), "recognize detached launch descendant");
    tracker.terminate();
    check(waitUntil([&]() { return !tracker.isRunning("GeometryDash.exe"); }), "detached descendant terminates");

    Settings::ensureDirs();
    put(bin + "/dpkg-query", "#!/bin/sh\nprintf 'gdlauncher: %s\\n' \"$2\"\n");
    put(bin + "/rpm", "#!/bin/sh\nprintf gdlauncher\n");
    for (const auto &tool : {"dpkg-query", "rpm"}) QFile::setPermissions(bin + "/" + tool, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    qputenv("PATH", (bin + ":" + originalPath).toUtf8());
    check(Settings::installationType() == "deb", "detect Debian package ownership");
    put(bin + "/dpkg-query", "#!/bin/sh\nexit 1\n");
    check(Settings::installationType() == "rpm", "detect RPM package ownership");
    put(bin + "/rpm", "#!/bin/sh\nexit 1\n");
    check(Settings::installationType() == "portable", "unowned executable must be portable");
    auto legacy = AppSettings::fromJson(QJsonObject{{"update_package_type", "rpm"}});
    check(!legacy.toJson().contains("update_package_type"), "ignore obsolete manual installation choice");

    AppSettings settings;
    settings.syncDelay = 0;
    check(Settings::save(settings), "save settings");
    for (const auto &theme : {QString("Dark"), QString("Light")}) {
        settings.theme = theme;
        Theme::apply(Theme::fromSettingsString(theme));
        SettingsDialog dialog(nullptr, settings, "test");
        auto *button = dialog.findChild<QPushButton *>("sourceCodeButton");
        check(button && button->text() == "Source code", "source button exists");
        auto icon = button->icon().pixmap(24, 24).toImage();
        check(!icon.isNull(), "GitHub SVG renders");
        int pixels = 0, brightness = 0;
        for (int y = 0; y < icon.height(); ++y) for (int x = 0; x < icon.width(); ++x) {
            const auto color = icon.pixelColor(x, y);
            if (color.alpha() > 128) { ++pixels; brightness += color.lightness(); }
        }
        check(pixels > 20, "GitHub icon is visible");
        check(theme == "Dark" ? brightness / pixels > 180 : brightness / pixels < 80, "icon contrasts with theme");
        dialog.resize(dialog.sizeHint());
        dialog.show();
        QApplication::processEvents();
        dialog.grab().save(QCoreApplication::applicationDirPath() + "/settings-" + theme + ".png");
        for (auto *combo : dialog.findChildren<QComboBox *>())
            check(combo->findData("portable") < 0, "installation no longer manually selectable");
    }

    const QString source = temporary.path() + "/source";
    const QString dest = temporary.path() + "/dest";
    put(source + "/data/nested/file", "keep me");
    put(dest, "not a directory");
    check(!InstanceManager::transferManagedItems(source, dest, {"data"}, true), "copy failure is reported");
    check(get(source + "/data/nested/file") == "keep me", "failed move keeps source");

    // A fake Wine executable runs a native fixture, exercising the actual
    // launcher lifecycle and worker transfers without touching real saves.
    put(bin + "/wine", "#!/bin/sh\nexec \"$@\"\n");
    QFile::setPermissions(bin + "/wine", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    settings.theme = "Dark";
    Settings::save(settings);
    const QString version = Settings::versionsDir() + "/test/game";
    QDir().mkpath(version);
    check(QFile::link(self, version + "/GeometryDash.exe"), "create installed game fixture");
    QJsonObject instance{{"name", "test"}, {"versionType", "local"}, {"version", "test/game"},
                         {"saveFolderName", "GeometryDash"}, {"isGeodeCompatible", true}, {"useMegaHack", false}};
    check(InstanceManager::createInstance(instance).success, "create test instance");
    const QString instancePath = Settings::instancesDir() + "/test";
    put(instancePath + "/CCGameManager.dat", "before game");
    put(instancePath + "/.hidden-save", "hidden instance data");
    const QByteArray payload(64 * 1024, 'x');
    for (int i = 0; i < 700; ++i) put(instancePath + QString("/geode/mods/file-%1").arg(i), payload);
    const QString livePath = temporary.path() + "/.wine/drive_c/users/" + QFileInfo(temporary.path()).fileName() + "/AppData/Local/GeometryDash";
    qputenv("GDLAUNCHER_TEST_SAVE_PATH", livePath.toUtf8());
    GameLauncher launcher(nullptr);
    int ticks = 0, prepProgress = 0, syncProgress = 0;
    bool started = false, completed = false;
    QTimer pulse;
    QObject::connect(&pulse, &QTimer::timeout, &launcher, [&]() { ++ticks; });
    pulse.start(1);
    QObject::connect(&launcher, &GameLauncher::statusUpdate, &launcher, [&](const QString &text) {
        if (text.startsWith("Preparing test (")) { ++prepProgress; check(launcher.isLaunching(), "preparation remains protected"); }
        if (text.startsWith("Syncing test (")) { ++syncProgress; check(launcher.isSyncing(), "sync remains protected"); }
    });
    QObject::connect(&launcher, &GameLauncher::gameStarted, &launcher, [&]() { started = true; });
    QObject::connect(&launcher, &GameLauncher::launchComplete, &launcher, [&]() { completed = true; });
    launcher.launchInstance("test");
    check(launcher.isLaunching() && !started, "preparation runs asynchronously");
    check(waitUntil([&]() { return completed; }, 15000), "own game exits and syncs despite unrelated process");
    check(started && prepProgress > 0 && syncProgress > 0 && ticks > 10, "UI remains responsive with live copying status");
    check(get(instancePath + "/CCGameManager.dat") == "from launched game", "sync back actual game progress");
    const auto firstBackup = BackupManager::createScheduled("test");
    check(firstBackup.success && !firstBackup.created, "launch already created today's persistent backup");
    check(QFileInfo(firstBackup.path).fileName().startsWith("test-"), "backup folder is instance name and timestamp");
    check(get(firstBackup.path + "/CCGameManager.dat") == "before game", "backup precedes game launch");
    check(get(firstBackup.path + "/.hidden-save") == "hidden instance data" &&
          QFileInfo::exists(firstBackup.path + "/instance.json"), "snapshot includes hidden files and instance metadata");
    put(instancePath + "/CCGameManager.dat", "later today");
    const auto sameDay = BackupManager::createScheduled("test");
    check(sameDay.success && !sameDay.created && sameDay.path == firstBackup.path &&
          get(sameDay.path + "/CCGameManager.dat") == "before game", "same-day launch never replaces the first backup");
    const auto nextDay = BackupManager::createScheduled("test", QDate::currentDate().addDays(1));
    check(nextDay.success && nextDay.created && nextDay.path != firstBackup.path &&
          get(nextDay.path + "/CCGameManager.dat") == "later today", "next local day gets a fresh backup");
    QJsonObject other = instance; other["name"] = "another";
    check(InstanceManager::createInstance(other).success, "create another backup instance");
    const QString rootBackups = Settings::backupsDir();
    check(QDir().rename(rootBackups, rootBackups + "-kept"), "temporarily make backup path unavailable");
    put(rootBackups, "not a directory");
    const auto failedBackup = BackupManager::createScheduled("another");
    check(!failedBackup.success && !failedBackup.error.isEmpty(), "backup failure is reported");
    QFile::remove(rootBackups);
    check(QDir().rename(rootBackups + "-kept", rootBackups), "restore backup location");
    const auto retriedBackup = BackupManager::createScheduled("another");
    check(retriedBackup.success && retriedBackup.created, "failed attempt does not mark the instance backed up today");
    check(!BackupManager::createScheduled("test").created, "instances have separate daily backup schedules");
    auto backupSettings = Settings::load();
    check(backupSettings.backupsEnabled && backupSettings.backupIntervalDays == 1 &&
          backupSettings.backupKeepCount == 0 && backupSettings.closeGuardEnabled &&
          backupSettings.allowForceClose && backupSettings.confirmForceCloseTwice, "safe backup and close defaults");
    const QString anotherPath = Settings::instancesDir() + "/another";
    put(anotherPath + "/CCGameManager.dat", "current save before restore");
    put(anotherPath + "/extra.txt", "untracked current file");
    backupSettings.backupsEnabled = false;
    Settings::save(backupSettings);
    const auto disabled = BackupManager::createScheduled("another");
    check(disabled.success && !disabled.created && disabled.path.isEmpty(), "automatic backups can be disabled");
    const auto restored = BackupManager::restore(retriedBackup.path);
    check(restored.success && get(restored.path + "/CCGameManager.dat") == "current save before restore" &&
          get(restored.path + "/extra.txt") == "untracked current file", "restore always backs up the entire current instance even when automatic backups are disabled");
    check(get(anotherPath + "/CCGameManager.dat").isEmpty() && !QFileInfo::exists(anotherPath + "/extra.txt"), "full restore replaces the current snapshot");
    check(!QFileInfo::exists(anotherPath + "/.gdlauncher-backup.json"), "restoring does not copy backup bookkeeping into the instance");
    const auto forked = BackupManager::fork(retriedBackup.path, "forked");
    check(forked.success && get(forked.path + "/CCGameManager.dat").isEmpty(), "fork creates an independent instance with the backed-up saves");
    check(QJsonDocument::fromJson(get(forked.path + "/instance.json")).object().value("name").toString() == "forked", "fork updates instance identity");
    check(!BackupManager::fork(retriedBackup.path, "forked").success, "fork never overwrites an existing instance");
    QString backupError;
    check(!BackupManager::remove(anotherPath, &backupError) && QFileInfo::exists(anotherPath), "backup deletion rejects instance folders");
    check(BackupManager::remove(restored.path, &backupError) && !QFileInfo::exists(restored.path), "delete removes only the selected backup");
    backupSettings.backupsEnabled = true;
    backupSettings.backupScope = "managed";
    backupSettings.backupIntervalDays = 7;
    Settings::save(backupSettings);
    put(anotherPath + "/CCGameManager.dat", "managed snapshot");
    put(anotherPath + "/extra.txt", "untracked first");
    const auto managed = BackupManager::createScheduled("another");
    check(managed.success && managed.created && !QFileInfo::exists(managed.path + "/extra.txt"), "managed-only backup excludes untracked content");
    check(!BackupManager::createScheduled("another", QDate::currentDate().addDays(6)).created &&
           BackupManager::createScheduled("another", QDate::currentDate().addDays(7)).created, "custom backup intervals are honored");
    put(anotherPath + "/CCGameManager.dat", "managed later");
    put(anotherPath + "/extra.txt", "keep this current untracked file");
    const auto managedRestore = BackupManager::restore(managed.path);
    check(managedRestore.success && get(anotherPath + "/CCGameManager.dat") == "managed snapshot" &&
          get(anotherPath + "/extra.txt") == "keep this current untracked file", "managed restore preserves untracked current content");
    backupSettings.backupIntervalDays = 0;
    backupSettings.backupKeepCount = 2;
    Settings::save(backupSettings);
    const auto everyLaunch = BackupManager::createScheduled("another");
    const auto everyLaunchAgain = BackupManager::createScheduled("another");
    check(everyLaunch.created && everyLaunchAgain.created && everyLaunch.path != everyLaunchAgain.path, "every-launch mode creates unique snapshots");
    int retained = 0;
    for (const auto &backup : BackupManager::list()) if (backup.instanceName == "another") ++retained;
    check(retained == 2, "retention keeps the configured number of completed backups per instance");
    backupSettings.backupDirectory = temporary.path() + "/custom-backups";
    backupSettings.backupKeepCount = 0;
    Settings::save(backupSettings);
    const auto custom = BackupManager::createScheduled("another");
    check(custom.success && custom.path.startsWith(backupSettings.backupDirectory + "/"), "custom backup location is used");
    {
        BackupsDialog dialog;
        auto *table = dialog.findChild<QTableWidget *>("backupTable");
        auto *search = dialog.findChild<QLineEdit *>("backupSearch");
        check(table && table->rowCount() == 1, "backup browser lists configured folder");
        search->setText("no matches here");
        check(table->rowCount() == 0, "backup search filters rows");
        search->clear();
        table->selectRow(0);
        check(dialog.findChild<QPushButton *>("backupRestore")->isEnabled() &&
              dialog.findChild<QPushButton *>("backupFork")->isEnabled(), "backup selection enables restore and fork");
        put(anotherPath + "/CCGameManager.dat", "do not replace after cancelled restore");
        QSet<QString> handled;
        int warnings = 0;
        QTimer cancelRestore;
        QObject::connect(&cancelRestore, &QTimer::timeout, &dialog, [&]() {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box || handled.contains(box->windowTitle())) return;
            handled.insert(box->windowTitle());
            ++warnings;
            box->button(box->windowTitle() == "Restore Backup" ? QMessageBox::Ok : QMessageBox::Cancel)->click();
        });
        cancelRestore.start(5);
        dialog.findChild<QPushButton *>("backupRestore")->click();
        cancelRestore.stop();
        check(warnings == 2 && get(anotherPath + "/CCGameManager.dat") == "do not replace after cancelled restore", "restore has two warnings and cancellation preserves current saves");
        dialog.show();
        QApplication::processEvents();
        dialog.grab().save(QCoreApplication::applicationDirPath() + "/backups.png");
    }
    {
        SettingsDialog dialog(nullptr, backupSettings, "test");
        dialog.findChild<QCheckBox *>("closeGuardEnabled")->setChecked(false);
        dialog.findChild<QCheckBox *>("backupsEnabled")->setChecked(false);
        dialog.findChild<QComboBox *>("backupFrequency")->setCurrentIndex(3);
        dialog.findChild<QSpinBox *>("backupIntervalDays")->setValue(3);
        for (auto *button : dialog.findChildren<QPushButton *>()) if (button->text() == "Save and Close") button->click();
        const auto selected = dialog.resultSettings();
        check(!selected.closeGuardEnabled && !selected.backupsEnabled && selected.backupIntervalDays == 3, "backup and close settings are editable");
        const auto persisted = AppSettings::fromJson(selected.toJson());
        check(persisted.backupDirectory == backupSettings.backupDirectory && persisted.backupIntervalDays == 3 &&
              !persisted.closeGuardEnabled && !persisted.backupsEnabled, "all backup and close options persist");
    }
    Settings::save(AppSettings{});
    check(unrelated.state() != QProcess::NotRunning, "unrelated process remains alive after sync");
    check(!QFileInfo::exists(Settings::baseDir() + "/info.json"), "recovery marker cleared after sync");

    completed = false;
    launcher.runExecutable("explorer.exe");
    check(waitUntil([&]() { return completed; }), "tool launch failure returns control");
    // Real Explorer is not needed: verify the selected backend receives its name.
    put(bin + "/wine", "#!/bin/sh\nprintf '%s' \"$1\" > \"$HOME/tool-argument\"\n");
    completed = false;
    launcher.runExecutable("explorer.exe");
    check(waitUntil([&]() { return completed; }), "Explorer tool lifecycle completes");
    check(get(temporary.path() + "/tool-argument") == "explorer.exe", "Explorer opens through Wine");

    settings.useProtonGE = true;
    Settings::save(settings);
    put(ProtonManager::protonDir() + "/proton", "fixture");
    put(ProtonManager::prefixDir() + "/system.reg", "fixture");
    put(ProtonManager::launcherPath(), R"PY(import json, os, pathlib, sys
root = pathlib.Path(os.environ['HOME'])
(root / 'umu-arguments').write_text(json.dumps(sys.argv[1:]))
(root / 'umu-environment').write_text(json.dumps(dict(os.environ)))
)PY");
    completed = false;
    launcher.runExecutable("explorer.exe", true);
    check(waitUntil([&]() { return completed; }), "UMU tool lifecycle completes");
    check(QJsonDocument::fromJson(get(temporary.path() + "/umu-arguments")).array().first().toString() == "explorer.exe", "Explorer opens through UMU");
    const auto toolEnv = QJsonDocument::fromJson(get(temporary.path() + "/umu-environment")).object();
    check(toolEnv.value("WINEPREFIX").toString() == ProtonManager::prefixDir(), "UMU tool uses dedicated prefix");
    check(toolEnv.value("PROTONPATH").toString() == ProtonManager::protonDir(), "UMU tool uses installed GE-Proton");
    check(toolEnv.value("PROTON_VERB").toString() == "run", "UMU launches do not wait for existing prefix services or Explorer");
    check(toolEnv.value("WINEDLLOVERRIDES").toString() == "xinput1_4=n,b", "UMU tool keeps Geode override");
    nongChecks(temporary.path());
    unrelated.kill(); unrelated.waitForFinished();
    qputenv("PATH", originalPath.toUtf8());
    std::cout << "PASS: process ownership, detached descendants, safe termination, install detection, themes, async copying, save sync, Linux tools\n";
}
}
int main(int argc, char **argv) {
    const QString executable = QFileInfo(QString::fromLocal8Bit(argv[0])).fileName();
    if (executable == "GeometryDash.exe") {
        if (argc > 1) { QThread::sleep(25); return 0; }
        const QString path = qEnvironmentVariable("GDLAUNCHER_TEST_SAVE_PATH");
        if (path.isEmpty()) return 2;
        put(path + "/CCGameManager.dat", "from launched game");
        QThread::msleep(800);
        return 0;
    }
    QApplication application(argc, argv);
    try {
        if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "--sfh-live") liveSfhCheck();
        else checks(QCoreApplication::applicationFilePath());
        return 0;
    }
    catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
