#include "XmlHighlighter.h"

XmlHighlighter::XmlHighlighter(QTextDocument *parent) : QSyntaxHighlighter(parent) {
    // Colors match VS Code's Dark+ theme so the editor reads the same as the source it mirrors.
    m_tagFormat.setForeground(QColor("#569cd6"));
    m_tagFormat.setFontWeight(QFont::Bold);

    m_attrNameFormat.setForeground(QColor("#9cdcfe"));

    m_attrValueFormat.setForeground(QColor("#ce9178"));

    m_punctuationFormat.setForeground(QColor("#808080"));

    m_commentFormat.setForeground(QColor("#6a9955"));
    m_commentFormat.setFontItalic(true);

    // Element/tag names: <foo, </foo, foo>  (captures the identifier only)
    m_rules.push_back({QRegularExpression(R"(</?\s*([A-Za-z_][A-Za-z0-9_\-.:]*))"), m_tagFormat});
    // Attribute names immediately followed by '='
    m_rules.push_back({QRegularExpression(R"(\b([A-Za-z_][A-Za-z0-9_\-.:]*)\s*(?=\=))"), m_attrNameFormat});
    // Quoted attribute values / text content
    m_rules.push_back({QRegularExpression(R"("[^"]*"|'[^']*')"), m_attrValueFormat});
    // Angle brackets, slashes, equals
    m_rules.push_back({QRegularExpression(R"([<>/=?])"), m_punctuationFormat});

    m_commentStart = QRegularExpression(R"(<!--)");
    m_commentEnd = QRegularExpression(R"(-->)");
}

void XmlHighlighter::highlightBlock(const QString &text) {
    for (const Rule &rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            // Prefer the captured group (e.g. the tag/attribute name without the punctuation)
            // when the pattern has one, otherwise highlight the whole match.
            const int group = match.lastCapturedIndex() >= 1 ? 1 : 0;
            setFormat(match.capturedStart(group), match.capturedLength(group), rule.format);
        }
    }

    // Multi-line <!-- ... --> comments, tracked via block state like Qt's own examples do.
    setCurrentBlockState(0);
    int startIndex = 0;
    if (previousBlockState() != 1) {
        const QRegularExpressionMatch m = m_commentStart.match(text);
        startIndex = m.hasMatch() ? m.capturedStart() : -1;
    }

    while (startIndex >= 0) {
        const QRegularExpressionMatch endMatch = m_commentEnd.match(text, startIndex);
        int commentLength;
        if (!endMatch.hasMatch()) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endMatch.capturedEnd() - startIndex;
        }
        setFormat(startIndex, commentLength, m_commentFormat);
        const QRegularExpressionMatch nextStart = m_commentStart.match(text, startIndex + commentLength);
        startIndex = nextStart.hasMatch() ? nextStart.capturedStart() : -1;
    }
}
