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
#include <QLineEdit>
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
        check(dialog.findChildren<QComboBox *>().isEmpty(), "installation no longer manually selectable");
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
