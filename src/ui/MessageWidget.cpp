#include "ui/MessageWidget.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextOption>
#include <QVBoxLayout>

#include <utility>

MessageWidget::MessageWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setObjectName(QStringLiteral("MessageLog"));
    m_textEdit->setReadOnly(true);
    m_textEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_textEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_textEdit->viewport()->installEventFilter(this);
    layout->addWidget(m_textEdit);
}

void MessageWidget::appendMessage(const QString& message)
{
    m_textEdit->appendPlainText(message);
}

void MessageWidget::setObjectActivationHandler(ObjectActivationHandler handler)
{
    m_objectActivationHandler = std::move(handler);
}

bool MessageWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (m_textEdit && watched == m_textEdit->viewport() &&
        event->type() == QEvent::MouseButtonDblClick &&
        m_objectActivationHandler) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        QTextCursor cursor = m_textEdit->cursorForPosition(
            mouseEvent->position().toPoint());
        // A message can span several visual lines; its UUID may be on any
        // one of them. Preserve double-click navigation after wrapping.
        cursor.select(QTextCursor::BlockUnderCursor);
        static const QRegularExpression uuidExpression(
            QStringLiteral(
                R"(\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\})"));
        const QRegularExpressionMatch match =
            uuidExpression.match(cursor.selectedText());
        if (match.hasMatch()) {
            m_objectActivationHandler(match.captured(0));
        }
    }
    return QWidget::eventFilter(watched, event);
}
