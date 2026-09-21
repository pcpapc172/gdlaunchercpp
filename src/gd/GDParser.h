#pragma once
#include <QString>
#include <QVector>
#include <QPair>

// A faithful C++ port of the tiny hand-rolled GD "plist" parser/builder from
// editor.js (GDParser). It deliberately mirrors the original's permissive,
// non-validating tag scanning rather than doing full XML parsing, and it
// preserves key insertion order the way a JS object literal would.
class GDValue {
public:
    enum class Type { Dict, String, Integer, Real, Bool };

    Type type = Type::Dict;
    QString strVal;
    qint64 intVal = 0;
    double realVal = 0.0;
    bool boolVal = false;
    QVector<QPair<QString, GDValue>> dict;

    static GDValue makeDict() { GDValue v; v.type = Type::Dict; return v; }
    static GDValue makeString(const QString &s) { GDValue v; v.type = Type::String; v.strVal = s; return v; }
    static GDValue makeInt(qint64 i) { GDValue v; v.type = Type::Integer; v.intVal = i; return v; }
    static GDValue makeReal(double r) { GDValue v; v.type = Type::Real; v.realVal = r; return v; }
    static GDValue makeBool(bool b) { GDValue v; v.type = Type::Bool; v.boolVal = b; return v; }

    bool isDict() const { return type == Type::Dict; }

    bool has(const QString &key) const;
    const GDValue *find(const QString &key) const;
    GDValue *find(const QString &key);
    // Updates the value in place if the key exists, otherwise appends it (mirrors obj[key] = v).
    void set(const QString &key, const GDValue &value);
    void remove(const QString &key);
    // Inserts a brand new entry at the front of the dict (mirrors { [k]: v, ...rest }).
    void prepend(const QString &key, const GDValue &value);

    // Convenience accessors with JS-like defaulting.
    QString stringOr(const QString &key, const QString &def = QString()) const;
    qint64 intOr(const QString &key, qint64 def = 0) const;
    bool boolOr(const QString &key, bool def = false) const;
};

namespace GDParser {
GDValue parse(const QString &xml);
QString build(const GDValue &obj);
QString buildPretty(const GDValue &obj);
}
