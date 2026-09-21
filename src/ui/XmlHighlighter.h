#pragma once
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>
#include <QVector>

// A lightweight VS Code Dark+-styled XML syntax highlighter for the raw save
// data editor. Deliberately built on QSyntaxHighlighter (part of Qt itself)
// rather than pulling in a full code-editor engine like Monaco, to keep the
// app's footprint small while still giving tag/attribute/string coloring.
class XmlHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit XmlHighlighter(QTextDocument *parent);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<Rule> m_rules;
    QTextCharFormat m_tagFormat;
    QTextCharFormat m_attrNameFormat;
    QTextCharFormat m_attrValueFormat;
    QTextCharFormat m_punctuationFormat;
    QTextCharFormat m_commentFormat;

    QRegularExpression m_commentStart;
    QRegularExpression m_commentEnd;
};
