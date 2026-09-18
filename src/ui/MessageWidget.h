#pragma once

#include <QString>
#include <QWidget>

#include <functional>

class QPlainTextEdit;

class MessageWidget final : public QWidget
{
public:
    using ObjectActivationHandler = std::function<void(const QString&)>;

    explicit MessageWidget(QWidget* parent = nullptr);

    void appendMessage(const QString& message);
    void setObjectActivationHandler(ObjectActivationHandler handler);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPlainTextEdit* m_textEdit = nullptr;
    ObjectActivationHandler m_objectActivationHandler;
};
