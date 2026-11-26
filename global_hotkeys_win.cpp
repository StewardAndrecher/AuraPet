#include "global_hotkeys.h"

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
constexpr int baseHotkeyId = 0x4150; // "AP"
constexpr std::array<UINT, 3> winKeyCodes{'A', 'F', 'D'};
}

struct GlobalHotkeys::Impl : public QAbstractNativeEventFilter {
    explicit Impl(GlobalHotkeys* owner, std::function<void(Action)> cb)
        : owner_(owner), onPressed_(std::move(cb))
    {
        if (auto* app = QCoreApplication::instance()) {
            app->installNativeEventFilter(this);
        }
    }

    ~Impl() override
    {
        clear();
        if (auto* app = QCoreApplication::instance()) {
            app->removeNativeEventFilter(this);
        }
    }

    void clear()
    {
        for (size_t i = 0; i < registered_.size(); ++i) {
            if (registered_[i]) {
                UnregisterHotKey(nullptr, baseHotkeyId + static_cast<int>(i));
                registered_[i] = false;
            }
        }
    }

    QStringList setEnabled(bool enabled)
    {
        clear();
        QStringList unavailable;
        if (enabled) {
            for (size_t i = 0; i < winKeyCodes.size(); ++i) {
                if (RegisterHotKey(nullptr, baseHotkeyId + static_cast<int>(i),
                                   MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, winKeyCodes[i])) {
                    registered_[i] = true;
                } else {
                    unavailable.append(GlobalHotkeys::chord(static_cast<Action>(i)));
                }
            }
        }
        return unavailable;
    }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override
    {
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
            return false;
        }
        const auto* msg = static_cast<const MSG*>(message);
        if (!msg || msg->message != WM_HOTKEY) {
            return false;
        }
        const int idx = static_cast<int>(msg->wParam) - baseHotkeyId;
        if (idx < 0 || idx >= static_cast<int>(registered_.size()) || !registered_[static_cast<size_t>(idx)]) {
            return false;
        }
        const auto action = static_cast<Action>(idx);
        QTimer::singleShot(0, owner_, [this, action] {
            if (onPressed_) {
                onPressed_(action);
            }
        });
        return true;
    }

    GlobalHotkeys* owner_ = nullptr;
    std::function<void(Action)> onPressed_;
    std::array<bool, 3> registered_{};
};

GlobalHotkeys::GlobalHotkeys(std::function<void(Action)> onPressed)
    : impl_(std::make_unique<Impl>(this, std::move(onPressed)))
{
}

GlobalHotkeys::~GlobalHotkeys() = default;

QString GlobalHotkeys::chord(Action action)
{
    return QStringLiteral("Ctrl+Alt+") + QStringLiteral("AFD").at(static_cast<int>(action));
}

QStringList GlobalHotkeys::setEnabled(bool enabled)
{
    return impl_ ? impl_->setEnabled(enabled) : QStringList{};
}

#else

struct GlobalHotkeys::Impl {};
GlobalHotkeys::GlobalHotkeys(std::function<void(Action)>) {}
GlobalHotkeys::~GlobalHotkeys() = default;
QString GlobalHotkeys::chord(Action action)
{
    return QStringLiteral("Ctrl+Alt+") + QStringLiteral("AFD").at(static_cast<int>(action));
}
QStringList GlobalHotkeys::setEnabled(bool) { return {}; }

#endif
