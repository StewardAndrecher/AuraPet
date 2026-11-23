#pragma once

#include <QObject>
#include <QStringList>

#include <Carbon/Carbon.h>

#include <array>
#include <functional>

class GlobalHotkeys : public QObject {
public:
    enum Action { Visibility, Focus, Quiet };
    explicit GlobalHotkeys(std::function<void(Action)> onPressed);
    ~GlobalHotkeys() override;
    QStringList setEnabled(bool enabled);
    static QString chord(Action action);

private:
    static OSStatus handle(EventHandlerCallRef, EventRef event, void* context);
    void clear();

    std::function<void(Action)> onPressed_;
    EventHandlerRef handler_ = nullptr;
    std::array<EventHotKeyRef, 3> keys_{};
    std::array<bool, 3> pressed_{};
};
