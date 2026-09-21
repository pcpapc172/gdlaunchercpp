#pragma once
#include "GDParser.h"
#include <QString>
#include <QVector>

struct SaveEditorResult {
    bool success = false;
    QString error;
};

struct EditorLevel {
    QString key;
    QString name;
    qint64 songId = 0;
    bool isCustomSong = false;
    qint64 length = 0;
    QString description; // base64, matches the original's k3 storage
    qint64 starRequest = 0;
};

// Port of editor.js's module-level session object and exported functions.
// There is a single global session, exactly like the original (globalEditorSession).
class SaveEditor {
public:
    static SaveEditorResult initSession(const QString &instancesDir, const QString &instanceName);
    static SaveEditorResult persist(const QString &instancesDir);

    static bool hasSession();
    static QVector<EditorLevel> getLevels();
    static SaveEditorResult getRaw(const QString &key, QString *outRaw);

    struct SaveAllUpdates {
        bool hasSongId = false;
        qint64 songId = 0;
        bool isCustom = false;
        bool hasRawData = false;
        QString rawData;
    };
    static SaveEditorResult saveAll(const QString &key, const SaveAllUpdates &updates);

    // filePath points to an already-selected .txt or .gmd file (dialog handled by caller).
    static SaveEditorResult importLevel(const QString &key, const QString &filePath);

    static SaveEditorResult setSong(const QString &key, qint64 songId, bool isCustom);
    static SaveEditorResult updateDescription(const QString &key, const QString &base64Desc);
    static SaveEditorResult updateStarRequest(const QString &key, qint64 stars);
    static SaveEditorResult renameLevel(const QString &key, const QString &newName);

    // outFilePath is the destination chosen by the caller (dialog handled by caller).
    static SaveEditorResult exportLevel(const QString &key, const QString &fmt, const QString &outFilePath);

    static SaveEditorResult getXml(QString *outXml);
    static SaveEditorResult saveXml(const QString &xml);

private:
    static GDValue translate19to20(GDValue levelObject);
    static bool is20PlusLevel(const QString &content);
};
