#pragma once

#include <QByteArray>
#include <QString>
#include <QStringDecoder>

// Solver output can contain UTF-8 input text or local Windows diagnostics.
// Decide per complete line so a process-read boundary cannot split a character.
class FdsOutputTextDecoder final
{
public:
    QString append(const QByteArray& bytes)
    {
        m_pending += bytes;
        QString text;
        qsizetype newline;
        while ((newline = m_pending.indexOf('\n')) >= 0) {
            text += decodeLine(m_pending.left(newline + 1));
            m_pending.remove(0, newline + 1);
        }
        return text;
    }

    // EOF also completes an unterminated line. Repeated flushes are harmless.
    QString finish()
    {
        const QString text = decodeLine(m_pending);
        m_pending.clear();
        return text;
    }

    static QString decodeComplete(const QByteArray& bytes)
    {
        FdsOutputTextDecoder decoder;
        QString text = decoder.append(bytes);
        text += decoder.finish();
        return text;
    }

private:
    static QString decodeLine(const QByteArray& bytes)
    {
        QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
        const QString text = utf8(bytes);
        return utf8.hasError() ? QString::fromLocal8Bit(bytes) : text;
    }

    QByteArray m_pending;
};
