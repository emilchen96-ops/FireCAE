#include "ui/FdsRecordEditor.h"

#include <QAbstractTextDocumentLayout>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>

namespace
{
class LineNumberArea final : public QWidget
{
public:
    explicit LineNumberArea(FdsRecordEditor* editor)
        : QWidget(editor), m_editor(editor) {}

    QSize sizeHint() const override
    {
        return {m_editor->lineNumberAreaWidth(), 0};
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        m_editor->lineNumberAreaPaintEvent(event);
    }

private:
    FdsRecordEditor* m_editor = nullptr;
};

class FdsSyntaxHighlighter final : public QSyntaxHighlighter
{
public:
    explicit FdsSyntaxHighlighter(QTextDocument* document)
        : QSyntaxHighlighter(document) {}

protected:
    void highlightBlock(const QString& text) override
    {
        const auto apply = [this, &text](const QString& pattern,
                                         const QTextCharFormat& format) {
            QRegularExpression expression(pattern,
                QRegularExpression::CaseInsensitiveOption);
            auto match = expression.globalMatch(text);
            while (match.hasNext()) {
                const QRegularExpressionMatch value = match.next();
                setFormat(value.capturedStart(), value.capturedLength(), format);
            }
        };
        QTextCharFormat keyword;
        keyword.setForeground(QColor(36, 92, 170));
        keyword.setFontWeight(QFont::Bold);
        apply(QStringLiteral(R"(&[A-Z][A-Z0-9_]*)"), keyword);

        QTextCharFormat parameter;
        parameter.setForeground(QColor(125, 64, 150));
        apply(QStringLiteral(R"(\b[A-Z][A-Z0-9_]*(?=\s*=))"), parameter);

        QTextCharFormat stringValue;
        stringValue.setForeground(QColor(35, 120, 74));
        apply(QStringLiteral(R"('(?:''|[^'])*')"), stringValue);

        QTextCharFormat numberValue;
        numberValue.setForeground(QColor(173, 82, 24));
        apply(QStringLiteral(R"((?<![A-Z_])[-+]?(?:\d+\.?\d*|\.\d+)(?:[Ee][-+]?\d+)?)"),
              numberValue);

        const int comment = text.indexOf(QLatin1Char('!'));
        if (comment >= 0) {
            QTextCharFormat commentFormat;
            commentFormat.setForeground(QColor(105, 112, 120));
            commentFormat.setFontItalic(true);
            setFormat(comment, text.size() - comment, commentFormat);
        }
    }
};
}

FdsRecordEditor::FdsRecordEditor(QWidget* parent)
    : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this))
{
    setObjectName(QStringLiteral("FdsRecordView"));
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_lineNumberArea->setObjectName(QStringLiteral("FdsRecordLineNumberArea"));
    new FdsSyntaxHighlighter(document());
    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this]() { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            &FdsRecordEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            &FdsRecordEditor::highlightCurrentLine);
    updateLineNumberAreaWidth();
    highlightCurrentLine();
}

void FdsRecordEditor::setGeneratedContent(
    const QString& text, const std::vector<FdsSourceMapEntry>& sourceMap)
{
    const QString current = toPlainText();
    if (current == text) {
        m_sourceMap = sourceMap;
        return;
    }

    const int currentLength = static_cast<int>(current.size());
    const int generatedLength = static_cast<int>(text.size());
    int prefix = 0;
    const int sharedLength = qMin(currentLength, generatedLength);
    while (prefix < sharedLength && current.at(prefix) == text.at(prefix)) {
        ++prefix;
    }
    int currentSuffix = currentLength;
    int generatedSuffix = generatedLength;
    while (currentSuffix > prefix && generatedSuffix > prefix &&
           current.at(currentSuffix - 1) == text.at(generatedSuffix - 1)) {
        --currentSuffix;
        --generatedSuffix;
    }

    const int oldCursorPosition = textCursor().position();
    const int scrollPosition = verticalScrollBar()->value();
    QTextCursor replacement(document());
    replacement.setPosition(prefix);
    replacement.setPosition(currentSuffix, QTextCursor::KeepAnchor);
    replacement.insertText(text.mid(prefix, generatedSuffix - prefix));

    int restoredCursorPosition = oldCursorPosition;
    if (oldCursorPosition > currentSuffix) {
        restoredCursorPosition += (generatedSuffix - prefix) -
                                  (currentSuffix - prefix);
    } else if (oldCursorPosition > prefix) {
        restoredCursorPosition = generatedSuffix;
    }
    QTextCursor restored(document());
    restored.setPosition(qBound(0, restoredCursorPosition,
                                qMax(0, document()->characterCount() - 1)));
    setTextCursor(restored);
    verticalScrollBar()->setValue(scrollPosition);
    m_sourceMap = sourceMap;
}

void FdsRecordEditor::setSourceMap(
    const std::vector<FdsSourceMapEntry>& sourceMap)
{
    m_sourceMap = sourceMap;
}

int FdsRecordEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    for (int value = qMax(1, blockCount()); value >= 10; value /= 10) ++digits;
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void FdsRecordEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), palette().alternateBase());
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(palette().mid().color());
            painter.drawText(0, top, m_lineNumberArea->width() - 5,
                             fontMetrics().height(), Qt::AlignRight,
                             QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void FdsRecordEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    m_lineNumberArea->setGeometry(
        QRect(area.left(), area.top(), lineNumberAreaWidth(), area.height()));
}

void FdsRecordEditor::mouseReleaseEvent(QMouseEvent* event)
{
    QPlainTextEdit::mouseReleaseEvent(event);
    const FdsSourceMapEntry source = sourceAtCursor();
    if (!source.objectId.isEmpty()) {
        emit sourceActivated(source.objectId, source.parameterKey);
    }
}

void FdsRecordEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void FdsRecordEditor::updateLineNumberArea(const QRect& rect, int dy)
{
    if (dy) m_lineNumberArea->scroll(0, dy);
    else m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth();
}

void FdsRecordEditor::highlightCurrentLine()
{
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor(220, 234, 252, 90));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    setExtraSelections({selection});
}

FdsSourceMapEntry FdsRecordEditor::sourceAtCursor() const
{
    const int line = textCursor().blockNumber() + 1;
    const int column = textCursor().positionInBlock();
    for (const FdsSourceMapEntry& source : m_sourceMap) {
        if (source.line == line && !source.parameterKey.isEmpty() &&
            column >= source.columnStart && column < source.columnEnd) {
            return source;
        }
    }
    for (const FdsSourceMapEntry& source : m_sourceMap) {
        if (source.line == line && source.parameterKey.isEmpty()) return source;
    }
    return {};
}
