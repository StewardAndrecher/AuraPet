#include "global_hotkeys.h"

#include <QTimer>
#include <utility>

namespace {
constexpr OSType signature = 0x41757261; // Aura
constexpr std::array<UInt32, 3> keyCodes{kVK_ANSI_A, kVK_ANSI_F, kVK_ANSI_D};
}

GlobalHotkeys::GlobalHotkeys(std::function<void(Action)> onPressed) : onPressed_(std::move(onPressed))
{
    const EventTypeSpec types[]{{kEventClassKeyboard, kEventHotKeyPressed}, {kEventClassKeyboard, kEventHotKeyReleased}};
    if (InstallEventHandler(GetApplicationEventTarget(), &GlobalHotkeys::handle, 2, types, this, &handler_) != noErr) {
        handler_ = nullptr;
    }
}

GlobalHotkeys::~GlobalHotkeys()
{
    clear();
    if (handler_) {
        RemoveEventHandler(handler_);
    }
}

QString GlobalHotkeys::chord(Action action)
{
    return QStringLiteral("⌃⌥") + QStringLiteral("AFD").at(static_cast<int>(action));
}

QStringList GlobalHotkeys::setEnabled(bool enabled)
{
    clear();
    QStringList unavailable;
    if (enabled) {
        for (size_t i = 0; i < keys_.size(); ++i) {
            const EventHotKeyID id{signature, static_cast<UInt32>(i)};
            if (!handler_ || RegisterEventHotKey(keyCodes[i], controlKey | optionKey, id,
                                                  GetApplicationEventTarget(), 0, &keys_[i]) != noErr) {
                keys_[i] = nullptr;
                unavailable.append(chord(static_cast<Action>(i)));
            }
        }
    }
    return unavailable;
}

void GlobalHotkeys::clear()
{
    pressed_.fill(false);
    for (auto& key : keys_) {
        if (key) {
            UnregisterEventHotKey(key);
            key = nullptr;
        }
    }
}

OSStatus GlobalHotkeys::handle(EventHandlerCallRef, EventRef event, void* context)
{
    auto* self = static_cast<GlobalHotkeys*>(context);
    EventHotKeyID fired{};
    if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof fired,
                          nullptr, &fired) != noErr || fired.signature != signature
        || fired.id >= self->keys_.size() || !self->keys_[fired.id]) {
        return eventNotHandledErr;
    }
    if (GetEventKind(event) == kEventHotKeyReleased) {
        self->pressed_[fired.id] = false;
        return noErr;
    }
    if (self->pressed_[fired.id]) {
        return noErr;
    }
    self->pressed_[fired.id] = true;
    const auto action = static_cast<Action>(fired.id);
    QTimer::singleShot(0, self, [self, action] {
        if (self->keys_[action]) {
            self->onPressed_(action);
        }
    });
    return noErr;
}
