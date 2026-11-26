#pragma once

#include <QObject>
#include <QStringList>

#ifdef Q_OS_MACOS
#include <Carbon/Carbon.h>
#endif

#include <array>
#include <functional>
#include <memory>

class GlobalHotkeys : public QObject {
public:
    enum Action { Visibility, Focus, Quiet };
    explicit GlobalHotkeys(std::function<void(Action)> onPressed);
    ~GlobalHotkeys() override;
    QStringList setEnabled(bool enabled);
    static QString chord(Action action);

private:
#ifdef Q_OS_MACOS
    static OSStatus handle(EventHandlerCallRef, EventRef event, void* context);
    void clear();

    std::function<void(Action)> onPressed_;
    EventHandlerRef handler_ = nullptr;
    std::array<EventHotKeyRef, 3> keys_{};
    std::array<bool, 3> pressed_{};
#else
    struct Impl;
    std::unique_ptr<Impl> impl_;
#endif
};
