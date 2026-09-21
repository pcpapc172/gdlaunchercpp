#include "GDParser.h"
#include <cmath>

bool GDValue::has(const QString &key) const { return find(key) != nullptr; }

const GDValue *GDValue::find(const QString &key) const {
    for (const auto &pair : dict) if (pair.first == key) return &pair.second;
    return nullptr;
}

GDValue *GDValue::find(const QString &key) {
    for (auto &pair : dict) if (pair.first == key) return &pair.second;
    return nullptr;
}

void GDValue::set(const QString &key, const GDValue &value) {
    for (auto &pair : dict) {
        if (pair.first == key) { pair.second = value; return; }
    }
    dict.push_back({key, value});
}

void GDValue::remove(const QString &key) {
    for (int i = 0; i < dict.size(); ++i) {
        if (dict[i].first == key) { dict.remove(i); return; }
    }
}

void GDValue::prepend(const QString &key, const GDValue &value) {
    dict.push_front({key, value});
}

QString GDValue::stringOr(const QString &key, const QString &def) const {
    const GDValue *v = find(key);
    if (!v) return def;
    if (v->type == Type::String) return v->strVal;
    if (v->type == Type::Integer) return QString::number(v->intVal);
    if (v->type == Type::Real) return QString::number(v->realVal);
    return def;
}

qint64 GDValue::intOr(const QString &key, qint64 def) const {
    const GDValue *v = find(key);
    if (!v) return def;
    if (v->type == Type::Integer) return v->intVal;
    if (v->type == Type::Real) return static_cast<qint64>(v->realVal);
    if (v->type == Type::String) return v->strVal.toLongLong();
    return def;
}

bool GDValue::boolOr(const QString &key, bool def) const {
    const GDValue *v = find(key);
    if (!v) return def;
    if (v->type == Type::Bool) return v->boolVal;
    return def;
}

namespace {

struct ParseNode {
    enum class Kind { None, Close, Key, Val };
    Kind kind = Kind::None;
    QString keyStr;
    GDValue val;
};

ParseNode parseNext(const QString &xml, int &pos) {
    const int openStart = xml.indexOf('<', pos);
    if (openStart == -1) return {};
    const int openEnd = xml.indexOf('>', openStart);
    if (openEnd == -1) return {};
    const QString fullTag = xml.mid(openStart + 1, openEnd - openStart - 1);
    const QString tagName = fullTag.split(' ').constFirst();
    const bool isClosing = fullTag.startsWith('/');
    const bool isSelfClosing = fullTag.endsWith('/');
    pos = openEnd + 1;

    if (isClosing) {
        ParseNode n; n.kind = ParseNode::Kind::Close; return n;
    }
    if (tagName == "k" || tagName == "key") {
        const QString endTag = tagName == "k" ? "</k>" : "</key>";
        const int endPos = xml.indexOf(endTag, pos);
        if (endPos == -1) return {};
        const QString key = xml.mid(pos, endPos - pos);
        pos = endPos + endTag.length();
        ParseNode n; n.kind = ParseNode::Kind::Key; n.keyStr = key; return n;
    }
    if (tagName == "s" || tagName == "string") {
        const QString endTag = tagName == "s" ? "</s>" : "</string>";
        const int endPos = xml.indexOf(endTag, pos);
        if (endPos == -1) return {};
        const QString val = xml.mid(pos, endPos - pos);
        pos = endPos + endTag.length();
        ParseNode n; n.kind = ParseNode::Kind::Val; n.val = GDValue::makeString(val); return n;
    }
    if (tagName == "i" || tagName == "integer") {
        const QString endTag = tagName == "i" ? "</i>" : "</integer>";
        const int endPos = xml.indexOf(endTag, pos);
        if (endPos == -1) return {};
        const QString val = xml.mid(pos, endPos - pos);
        pos = endPos + endTag.length();
        ParseNode n; n.kind = ParseNode::Kind::Val; n.val = GDValue::makeInt(val.toLongLong()); return n;
    }
    if (tagName == "r" || tagName == "real") {
        const QString endTag = tagName == "r" ? "</r>" : "</real>";
        const int endPos = xml.indexOf(endTag, pos);
        if (endPos == -1) return {};
        const QString val = xml.mid(pos, endPos - pos);
        pos = endPos + endTag.length();
        ParseNode n; n.kind = ParseNode::Kind::Val; n.val = GDValue::makeReal(val.toDouble()); return n;
    }
    if (tagName == "t" || tagName == "true") { ParseNode n; n.kind = ParseNode::Kind::Val; n.val = GDValue::makeBool(true); return n; }
    if (tagName == "f" || tagName == "false") { ParseNode n; n.kind = ParseNode::Kind::Val; n.val = GDValue::makeBool(false); return n; }
    if (tagName == "d" || tagName == "dict") {
        if (isSelfClosing) { ParseNode n; n.kind = ParseNode::Kind::Val; n.val = GDValue::makeDict(); return n; }
        GDValue obj = GDValue::makeDict();
        QString currentKey;
        bool hasKey = false;
        while (true) {
            ParseNode node = parseNext(xml, pos);
            if (node.kind == ParseNode::Kind::None || node.kind == ParseNode::Kind::Close) break;
            if (node.kind == ParseNode::Kind::Key) { currentKey = node.keyStr; hasKey = true; }
            else if (node.kind == ParseNode::Kind::Val && hasKey) {
                if (currentKey != "_isArr") obj.set(currentKey, node.val);
                hasKey = false;
            }
        }
        ParseNode n; n.kind = ParseNode::Kind::Val; n.val = obj; return n;
    }
    if (tagName.startsWith("?xml") || tagName.startsWith("plist")) return parseNext(xml, pos);
    return parseNext(xml, pos);
}

bool isIntegral(double d) {
    return std::isfinite(d) && d == std::floor(d) && std::abs(d) < 1e18;
}

QString formatReal(double d) {
    QString s = QString::number(d, 'g', 15);
    return s;
}

void processDict(const GDValue &o, QString &out, bool pretty, int level) {
    const auto indent = [&](int lvl) { return pretty ? QString("  ").repeated(lvl) : QString(); };
    for (const auto &pair : o.dict) {
        const QString &k = pair.first;
        const GDValue &v = pair.second;
        if (pretty) out += QString("\n%1<k>%2</k>").arg(indent(level), k);
        else out += QString("<k>%1</k>").arg(k);

        if (v.type == GDValue::Type::Dict) {
            if (v.dict.isEmpty()) {
                if (pretty) out += QString("\n%1<d />").arg(indent(level));
                else out += "<d />";
            } else {
                if (pretty) {
                    out += QString("\n%1<d>").arg(indent(level));
                    processDict(v, out, pretty, level + 1);
                    out += QString("\n%1</d>").arg(indent(level));
                } else {
                    out += "<d>";
                    processDict(v, out, pretty, level);
                    out += "</d>";
                }
            }
        } else if (v.type == GDValue::Type::Bool) {
            const QString tag = v.boolVal ? "<t />" : "<f />";
            if (pretty) out += QString("\n%1%2").arg(indent(level), tag);
            else out += tag;
        } else if (v.type == GDValue::Type::Integer) {
            const QString tag = QString("<i>%1</i>").arg(v.intVal);
            if (pretty) out += QString("\n%1%2").arg(indent(level), tag);
            else out += tag;
        } else if (v.type == GDValue::Type::Real) {
            QString tag;
            if (isIntegral(v.realVal)) tag = QString("<i>%1</i>").arg(static_cast<qint64>(v.realVal));
            else tag = QString("<r>%1</r>").arg(formatReal(v.realVal));
            if (pretty) out += QString("\n%1%2").arg(indent(level), tag);
            else out += tag;
        } else { // String
            const QString tag = QString("<s>%1</s>").arg(v.strVal);
            if (pretty) out += QString("\n%1%2").arg(indent(level), tag);
            else out += tag;
        }
    }
}

} // namespace

GDValue GDParser::parse(const QString &xmlIn) {
    QString xml = xmlIn;
    xml.remove(QChar(0));
    int pos = xml.indexOf("<d");
    if (pos == -1) pos = xml.indexOf("<dict");
    if (pos == -1) return GDValue::makeDict();
    ParseNode result = parseNext(xml, pos);
    if (result.kind == ParseNode::Kind::Val && result.val.type == GDValue::Type::Dict) return result.val;
    return GDValue::makeDict();
}

QString GDParser::build(const GDValue &obj) {
    QString body;
    processDict(obj, body, false, 0);
    return QString("<?xml version=\"1.0\"?><plist version=\"1.0\" gjver=\"2.0\"><dict>%1</dict></plist>").arg(body);
}

QString GDParser::buildPretty(const GDValue &obj) {
    QString body;
    processDict(obj, body, true, 1);
    return QString("<?xml version=\"1.0\"?>\n<plist version=\"1.0\" gjver=\"2.0\">\n<dict>%1\n</dict>\n</plist>").arg(body);
}
