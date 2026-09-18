#pragma once

#include "fds/FdsWriter.h"

#include <QPlainTextEdit>

#include <vector>

class FdsRecordEditor final : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit FdsRecordEditor(QWidget* parent = nullptr);

    void setGeneratedContent(const QString& text,
                             const std::vector<FdsSourceMapEntry>& sourceMap);
    void setSourceMap(const std::vector<FdsSourceMapEntry>& sourceMap);
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

signals:
    void sourceActivated(const QString& objectId, const QString& parameterKey);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();
    FdsSourceMapEntry sourceAtCursor() const;

    QWidget* m_lineNumberArea = nullptr;
    std::vector<FdsSourceMapEntry> m_sourceMap;
};
