#include "chat_stream.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace {
QJsonObject asObject(const QByteArray& bytes)
{
    return QJsonDocument::fromJson(bytes).object();
}
}

QString chatApiError(const QByteArray& bytes)
{
    return asObject(bytes).value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString().left(500);
}

QString chatApiReply(const QByteArray& bytes)
{
    const auto choices = asObject(bytes).value(QStringLiteral("choices")).toArray();
    return choices.isEmpty() ? QString{} :
        choices.first().toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
}

QString ChatStream::feed(const QByteArray& bytes)
{
    if (!error_.isEmpty()) {
        return {};
    }
    buffer_.append(bytes);
    QString text;
    qsizetype start = 0;
    for (auto newline = buffer_.indexOf('\n', start); newline >= 0; newline = buffer_.indexOf('\n', start)) {
        const QByteArray line = buffer_.sliced(start, newline - start).trimmed();
        start = newline + 1;
        if (line.isEmpty()) {
            text += dispatch();
        } else if (line.startsWith("data:")) {
            const auto value = line.sliced(5).trimmed();
            if (!event_.isEmpty()) {
                event_.append('\n');
            }
            event_.append(value);
        }
        if (event_.size() > 1024 * 1024 || !error_.isEmpty()) {
            break;
        }
    }
    buffer_.remove(0, start);
    if (event_.size() > 1024 * 1024 || buffer_.size() > 1024 * 1024) {
        error_ = QStringLiteral("接口返回的流式数据过大。");
    }
    return text;
}

QString ChatStream::finish()
{
    return feed("\n\n");
}

QString ChatStream::dispatch()
{
    if (event_.isEmpty()) {
        return {};
    }
    const QByteArray event = std::move(event_);
    event_.clear();
    if (event == "[DONE]") {
        completed_ = true;
        return {};
    }
    if (!error_.isEmpty()) {
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(event, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error_ = QStringLiteral("接口返回了无法读取的流式事件。");
        return {};
    }
    const auto payload = document.object();
    if (payload.contains(QStringLiteral("error"))) {
        error_ = chatApiError(event);
        if (error_.isEmpty()) {
            error_ = QStringLiteral("AI 接口返回了错误事件。");
        }
        return {};
    }
    const auto choices = payload.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        return {};
    }
    const auto choice = choices.first().toObject();
    completed_ |= choice.value(QStringLiteral("finish_reason")).isString();
    return choice.value(QStringLiteral("delta")).toObject().value(QStringLiteral("content")).toString();
}

bool ChatStream::complete() const
{
    return completed_;
}

QString ChatStream::error() const
{
    return error_;
}
