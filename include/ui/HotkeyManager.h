#pragma once

#include <QKeySequence>
#include <QObject>

#include <windows.h>

#include <array>
#include <cstdint>

namespace scrambler::ui
{

enum class HotkeyAction : std::uint8_t
{
    kToggleEffects = 0,
    kIncrementDelay,
    kDecrementDelay,
    kIncrementDropRate,
    kDecrementDropRate,
    kCount,
};

inline constexpr int kHotkeyActionCount = static_cast<int>(HotkeyAction::kCount);

struct HotkeyBinding
{
    UINT modifiers = 0;
    UINT vk = 0;

    [[nodiscard]] bool IsValid() const
    {
        return vk != 0;
    }
};

class HotkeyManager : public QObject
{
    Q_OBJECT

public:
    explicit HotkeyManager(QObject* parent = nullptr);
    ~HotkeyManager() override;

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;
    HotkeyManager(HotkeyManager&&) = delete;
    HotkeyManager& operator=(HotkeyManager&&) = delete;

    void SetBinding(HotkeyAction action, const HotkeyBinding& binding);
    [[nodiscard]] HotkeyBinding GetBinding(HotkeyAction action) const;

    void RegisterAll();
    void UnregisterAll();
    void SaveSettings();
    void LoadSettings();

    static HotkeyBinding FromKeySequence(const QKeySequence& seq);
    static QKeySequence ToKeySequence(const HotkeyBinding& binding);

signals:
    void HotkeyTriggered(HotkeyAction action);

private:
    void CheckAndTrigger(UINT vk, UINT modifiers);
    static UINT GetCurrentModifiers();

    // Hook Callbacks
    static LRESULT CALLBACK KeyboardHookProc(int n_code, WPARAM w_param, LPARAM l_param);
    static LRESULT CALLBACK MouseHookProc(int n_code, WPARAM w_param, LPARAM l_param);

    std::array<HotkeyBinding, kHotkeyActionCount> bindings_{};
    bool registered_ = false;

    // Hook state
    static std::atomic<HotkeyManager*> instance;
    static std::atomic<HHOOK> keyboard_hook;
    static std::atomic<HHOOK> mouse_hook;
};

}  // namespace scrambler::ui
