#include "SaveEditor.h"
#include "GDCrypto.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>

namespace {
QString g_instanceName;
GDValue g_gdObject;
bool g_hasObject = false;
bool g_isDirty = false;
bool g_isLegacy = false;
}

bool SaveEditor::hasSession() { return g_hasObject; }

SaveEditorResult SaveEditor::initSession(const QString &instancesDir, const QString &instanceName) {
    SaveEditorResult res;
    const QString p = instancesDir + "/" + instanceName + "/CCLocalLevels.dat";
    if (!QFileInfo::exists(p)) { res.error = "CCLocalLevels.dat not found in this instance."; return res; }

    const QString xml = GDCrypto::decryptSaveFile(p);
    if (xml.isNull()) { res.error = "Failed to decrypt save file. It may be corrupted."; return res; }

    const bool isLegacyFormat = !xml.contains("gjver=\"2.0\"");
    GDValue parsed = GDParser::parse(xml);
    if (!parsed.has("LLM_01")) {
        parsed = GDValue::makeDict();
        parsed.set("LLM_01", GDValue::makeDict());
        parsed.set("LLM_02", GDValue::makeInt(isLegacyFormat ? 23 : 45));
    }

    g_instanceName = instanceName;
    g_gdObject = parsed;
    g_hasObject = true;
    g_isDirty = false;
    g_isLegacy = isLegacyFormat;

    res.success = true;
    return res;
}

SaveEditorResult SaveEditor::persist(const QString &instancesDir) {
    SaveEditorResult res;
    if (!g_hasObject) { res.error = "No session."; return res; }
    const QString xml = GDParser::build(g_gdObject);
    const QString path = instancesDir + "/" + g_instanceName + "/CCLocalLevels.dat";
    if (!GDCrypto::encryptSaveFile(xml, path)) { res.error = "Failed to write save file."; return res; }
    g_isDirty = false;
    res.success = true;
    return res;
}

QVector<EditorLevel> SaveEditor::getLevels() {
    QVector<EditorLevel> out;
    if (!g_hasObject) return out;
    const GDValue *root = g_gdObject.find("LLM_01");
    if (!root) return out;
    for (const auto &pair : root->dict) {
        const QString &k = pair.first;
        if (!k.startsWith("k_")) continue;
        const GDValue &v = pair.second;
        EditorLevel lvl;
        lvl.key = k;
        lvl.name = v.stringOr("k2", "Unnamed");
        if (v.has("k45")) { lvl.songId = v.intOr("k45"); lvl.isCustomSong = true; }
        else { lvl.songId = v.intOr("k8", 0); lvl.isCustomSong = false; }
        lvl.length = v.intOr("k23");
        lvl.description = v.stringOr("k3");
        lvl.starRequest = v.intOr("k66", 0);
        out.push_back(lvl);
    }
    return out;
}

SaveEditorResult SaveEditor::getRaw(const QString &key, QString *outRaw) {
    SaveEditorResult res;
    if (!g_hasObject) { res.error = "No Session"; return res; }
    GDValue *root = g_gdObject.find("LLM_01");
    GDValue *lvl = root ? root->find(key) : nullptr;
    if (!lvl) { res.error = "Level not found"; return res; }
    if (outRaw) *outRaw = GDCrypto::decryptLevelString(lvl->stringOr("k4"));
    res.success = true;
    return res;
}

SaveEditorResult SaveEditor::saveAll(const QString &key, const SaveAllUpdates &updates) {
    SaveEditorResult res;
    if (!g_hasObject) { res.error = "No Session"; return res; }
    GDValue *root = g_gdObject.find("LLM_01");
    GDValue *lvl = root ? root->find(key) : nullptr;
    if (!lvl) { res.error = "Level not found"; return res; }

    if (updates.hasSongId) {
        if (updates.isCustom) { lvl->set("k45", GDValue::makeInt(updates.songId)); lvl->remove("k8"); }
        else { lvl->set("k8", GDValue::makeInt(updates.songId)); lvl->remove("k45"); }
    }
    if (updates.hasRawData) {
        lvl->set("k4", GDValue::makeString(GDCrypto::encryptLevelString(updates.rawData)));
    }
    g_isDirty = true;
    res.success = true;
    return res;
}

SaveEditorResult SaveEditor::renameLevel(const QString &key, const QString &newName) {
    SaveEditorResult res;
    if (!g_hasObject) { return res; }
    GDValue *root = g_gdObject.find("LLM_01");
    GDValue *lvl = root ? root->find(key) : nullptr;
    if (!lvl) return res;
    lvl->set("k2", GDValue::makeString(newName));
    g_isDirty = true;
    res.success = true;
    return res;
}

SaveEditorResult SaveEditor::setSong(const QString &key, qint64 songId, bool isCustom) {
    SaveEditorResult res;
    if (!g_hasObject) return res;
    GDValue *root = g_gdObject.find("LLM_01");
    GDValue *lvl = root ? root->find(key) : nullptr;
    if (!lvl) return res;
    if (isCustom) { lvl->set("k45", GDValue::makeInt(songId)); lvl->remove("k8"); }
    else { lvl->set("k8", GDValue::makeInt(songId)); lvl->remove("k45"); }
    g_isDirty = true;
    res.success = true;
    return res;
}

SaveEditorResult SaveEditor::updateDescription(const QString &key, const QString &base64Desc) {
    SaveEditorResult res;
    if (!g_hasObject) return res;
    GDValue *root = g_gdObject.find("LLM_01");
    GDValue *lvl = root ? root->find(key) : nullptr;
    if (!lvl) return res;
    lvl->set("k3", GDValue::makeString(base64Desc));
    g_isDirty = true;
    res.success = true;
    return res;
}

SaveEditorResult SaveEditor::updateStarRequest(const QString &key, qint64 stars) {
    SaveEditorResult res;
    if (!g_hasObject) return res;
    GDValue *root = g_gdObject.find("LLM_01");
    GDValue *lvl = root ? root->find(key) : nullptr;
    if (!lvl) return res;
    lvl->set("k66", GDValue::makeInt(stars));
    g_isDirty = true;
    res.success = true;
    return res;
}

SaveEditorResult SaveEditor::exportLevel(const QString &key, const QString &fmt, const QString &outFilePath) {
    SaveEditorResult res;
    if (!g_hasObject) return res;
    GDValue *root = g_gdObject.find("LLM_01");
    GDValue *lvl = root ? root->find(key) : nullptr;
    if (!lvl) return res;

    QString out;
    if (fmt == "gmd") {
        out = QString("<d><k>k2</k><s>%1</s><k>k4</k><s>%2</s><k>k1</k><i>1</i><k>k50</k><i>24</i><k>kCEK</k><i>4</i></d>")
                  .arg(lvl->stringOr("k2"), lvl->stringOr("k4"));
    } else {
        out = GDCrypto::decryptLevelString(lvl->stringOr("k4"));
    }

    QFile f(outFilePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { res.error = "Failed to write file"; return res; }
    f.write(out.toUtf8());
    res.success = true;
    return res;
}

SaveEditorResult SaveEditor::getXml(QString *outXml) {
    SaveEditorResult res;
    if (!g_hasObject) return res;
    if (outXml) *outXml = GDParser::buildPretty(g_gdObject);
    res.success = true;
    return res;
}

SaveEditorResult SaveEditor::saveXml(const QString &xml) {
    SaveEditorResult res;
    if (!g_hasObject) return res;
    GDValue parsed = GDParser::parse(xml);
    g_gdObject = parsed;
    g_isDirty = true;
    res.success = true;
    return res;
}

bool SaveEditor::is20PlusLevel(const QString &content) {
    if (content.contains("gjver=\"2.0\"")) return true;
    if (content.contains("k101")) return true;
    if (content.contains("<k>k101</k>")) return true;

    const QString decrypted = GDCrypto::decryptLevelString(content);
    if (decrypted.contains("kA14") || decrypted.contains(';')) return true;

    return false;
}

GDValue SaveEditor::translate19to20(GDValue levelObject) {
    if (GDValue *ki6 = levelObject.find("kI6")) {
        if (ki6->type == GDValue::Type::Dict) {
            GDValue newKi6 = GDValue::makeDict();
            for (const auto &pair : ki6->dict) {
                qint64 v = 0;
                if (pair.second.type == GDValue::Type::String) v = pair.second.strVal.toLongLong();
                else if (pair.second.type == GDValue::Type::Integer) v = pair.second.intVal;
                newKi6.set(pair.first, GDValue::makeInt(v));
            }
            levelObject.set("kI6", newKi6);
        }
    }

    if (!levelObject.has("k101")) {
        levelObject.set("k101", GDValue::makeString("0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"));
    }

    if (GDValue *k50 = levelObject.find("k50")) {
        if (k50->type == GDValue::Type::Integer && k50->intVal == 23) {
            levelObject.set("k50", GDValue::makeInt(45));
        }
    }

    return levelObject;
}

SaveEditorResult SaveEditor::importLevel(const QString &key, const QString &filePath) {
    SaveEditorResult res;
    if (!g_hasObject) { res.error = "No active editor session."; return res; }

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) { res.error = "Cancelled"; return res; }
    QString content = QString::fromUtf8(f.readAll()).trimmed();

    const bool isTargetLegacy = g_isLegacy;
    const bool isSource20Plus = is20PlusLevel(content);

    if (isTargetLegacy && isSource20Plus) {
        res.error = "Cannot import a 2.0+ level into a 1.9 or older instance. The level format is incompatible.";
        return res;
    }

    GDValue levelObject = GDValue::makeDict();
    const bool isGMD = content.startsWith('<');

    if (isGMD) {
        GDValue parsedGMD = GDParser::parse(content);
        if (parsedGMD.has("k2") && parsedGMD.has("k4")) {
            levelObject = parsedGMD;
        } else {
            bool found = false;
            for (const auto &pair : parsedGMD.dict) {
                if (pair.second.type == GDValue::Type::Dict && pair.second.has("k2") && pair.second.has("k4")) {
                    levelObject = pair.second;
                    found = true;
                    break;
                }
            }
            if (!found) { res.error = "Could not find a valid level in the GMD file."; return res; }
        }
        if (!levelObject.has("k4")) { res.error = "Could not find a valid level in the GMD file."; return res; }
    } else {
        const bool isEncrypted = content.startsWith("H4sIA");
        levelObject.set("k4", GDValue::makeString(isEncrypted ? content : GDCrypto::encryptLevelString(content)));
    }

    if (!isTargetLegacy && !isSource20Plus) {
        levelObject = translate19to20(levelObject);
    }

    GDValue *root = g_gdObject.find("LLM_01");
    if (!root) { g_gdObject.set("LLM_01", GDValue::makeDict()); root = g_gdObject.find("LLM_01"); }

    if (key == "new") {
        int counter = 1;
        QString newName;
        while (true) {
            const QString proposed = QString("Imported %1").arg(counter);
            bool exists = false;
            for (const auto &pair : root->dict) {
                if (pair.second.stringOr("k2") == proposed) { exists = true; break; }
            }
            if (!exists) { newName = proposed; break; }
            counter++;
        }

        qint64 maxKey = 0;
        for (const auto &pair : root->dict) {
            const QString &k = pair.first;
            if (k.startsWith("k_")) {
                bool ok = false;
                const qint64 num = k.mid(2).toLongLong(&ok);
                if (ok) maxKey = qMax(maxKey, num);
            }
        }
        const QString newKey = QString("k_%1").arg(maxKey + 1);

        GDValue defaultLevel = GDValue::makeDict();
        defaultLevel.set("k1", GDValue::makeInt(1));
        defaultLevel.set("k2", GDValue::makeString(newName));
        defaultLevel.set("k5", GDValue::makeString("Player"));
        defaultLevel.set("k13", GDValue::makeBool(true));
        defaultLevel.set("k21", GDValue::makeInt(2));
        defaultLevel.set("k16", GDValue::makeInt(1));
        defaultLevel.set("k80", GDValue::makeInt(0));
        defaultLevel.set("kCEK", GDValue::makeInt(4));
        defaultLevel.set("k101", GDValue::makeString("0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"));
        defaultLevel.set("k50", GDValue::makeInt(isTargetLegacy ? 23 : 45));
        defaultLevel.set("kI1", GDValue::makeInt(0));
        defaultLevel.set("kI2", GDValue::makeInt(0));
        defaultLevel.set("kI3", GDValue::makeInt(0));
        GDValue ki6 = GDValue::makeDict();
        for (int i = 0; i <= 13; ++i) ki6.set(QString::number(i), GDValue::makeInt(0));
        defaultLevel.set("kI6", ki6);

        // finalLevelObject = { ...defaultLevel, ...levelObject } -> levelObject's fields win.
        GDValue finalLevel = defaultLevel;
        for (const auto &pair : levelObject.dict) finalLevel.set(pair.first, pair.second);

        root->prepend(newKey, finalLevel);
    } else {
        GDValue *existing = root->find(key);
        if (existing) {
            for (const auto &pair : levelObject.dict) existing->set(pair.first, pair.second);
        }
    }

    g_isDirty = true;
    res.success = true;
    return res;
}
