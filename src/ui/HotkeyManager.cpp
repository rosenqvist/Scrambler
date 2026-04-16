#include "ui/HotkeyManager.h"

#include <QApplication>
#include <QDebug>
#include <Qnamespace.h>
#include <QSettings>
#include <Qwidget.h>
#include <QWindow>

#include <atomic>
#include <winuser.h>

namespace scrambler::ui
{

std::atomic<HotkeyManager*> HotkeyManager::instance{nullptr};
std::atomic<HHOOK> HotkeyManager::keyboard_hook{nullptr};
std::atomic<HHOOK> HotkeyManager::mouse_hook{nullptr};

namespace
{

constexpr const char* kSettingsGroup = "Hotkeys";

const char* ActionToSettingsKey(HotkeyAction action)
{
    switch (action)
    {
        case HotkeyAction::kToggleEffects:
            return "ToggleEffects";
        case HotkeyAction::kIncrementDelay:
            return "IncrementDelay";
        case HotkeyAction::kDecrementDelay:
            return "DecrementDelay";
        case HotkeyAction::kIncrementDropRate:
            return "IncrementDropRate";
        case HotkeyAction::kDecrementDropRate:
            return "DecrementDropRate";
        default:
            return "Unknown";
    }
}

// Qt::Key values for alphanumeric keys happen to match ASCII / Win32 VK codes.
// F-keys and special keys need explicit mapping.
UINT QtKeyToVk(int key)
{
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
    {
        return static_cast<UINT>(key);
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
    {
        return static_cast<UINT>(key);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
    {
        return static_cast<UINT>(VK_F1 + (key - Qt::Key_F1));
    }

    switch (key)
    {
        case Qt::Key_Space:
            return VK_SPACE;
        case Qt::Key_Return:
        // Windows doesn't distinguish keyboard "Enter" from numpad "Enter" at the VK level
        case Qt::Key_Enter:
            return VK_RETURN;
        case Qt::Key_Escape:
            return VK_ESCAPE;
        case Qt::Key_Tab:
            return VK_TAB;
        case Qt::Key_Backspace:
            return VK_BACK;
        case Qt::Key_Delete:
            return VK_DELETE;
        case Qt::Key_Insert:
            return VK_INSERT;
        case Qt::Key_Home:
            return VK_HOME;
        case Qt::Key_End:
            return VK_END;
        case Qt::Key_PageUp:
            return VK_PRIOR;
        case Qt::Key_PageDown:
            return VK_NEXT;
        case Qt::Key_Up:
            return VK_UP;
        case Qt::Key_Down:
            return VK_DOWN;
        case Qt::Key_Left:
            return VK_LEFT;
        case Qt::Key_Right:
            return VK_RIGHT;
        case Qt::Key_Plus:
            return VK_OEM_PLUS;
        case Qt::Key_Minus:
            return VK_OEM_MINUS;
        case Qt::Key_BracketLeft:
            return VK_OEM_4;
        case Qt::Key_BracketRight:
            return VK_OEM_6;
        case Qt::Key_Semicolon:
            return VK_OEM_1;
        case Qt::Key_Apostrophe:
            return VK_OEM_7;
        case Qt::Key_Comma:
            return VK_OEM_COMMA;
        case Qt::Key_Period:
            return VK_OEM_PERIOD;
        case Qt::Key_Slash:
            return VK_OEM_2;
        case Qt::Key_Backslash:
            return VK_OEM_5;
        case Qt::Key_QuoteLeft:
            return VK_OEM_3;
        default:
            return 0;
    }
}

int VkToQtKey(UINT vk)
{
    if (vk >= '0' && vk <= '9')
    {
        return static_cast<int>(Qt::Key_0 + (vk - '0'));
    }
    if (vk >= 'A' && vk <= 'Z')
    {
        return static_cast<int>(Qt::Key_A + (vk - 'A'));
    }
    if (vk >= VK_F1 && vk <= VK_F24)
    {
        return static_cast<int>(Qt::Key_F1 + (vk - VK_F1));
    }

    switch (vk)
    {
        case VK_SPACE:
            return Qt::Key_Space;
        case VK_RETURN:
            return Qt::Key_Return;
        case VK_ESCAPE:
            return Qt::Key_Escape;
        case VK_TAB:
            return Qt::Key_Tab;
        case VK_BACK:
            return Qt::Key_Backspace;
        case VK_DELETE:
            return Qt::Key_Delete;
        case VK_INSERT:
            return Qt::Key_Insert;
        case VK_HOME:
            return Qt::Key_Home;
        case VK_END:
            return Qt::Key_End;
        case VK_PRIOR:
            return Qt::Key_PageUp;
        case VK_NEXT:
            return Qt::Key_PageDown;
        case VK_UP:
            return Qt::Key_Up;
        case VK_DOWN:
            return Qt::Key_Down;
        case VK_LEFT:
            return Qt::Key_Left;
        case VK_RIGHT:
            return Qt::Key_Right;
        case VK_OEM_PLUS:
            return Qt::Key_Plus;
        case VK_OEM_MINUS:
            return Qt::Key_Minus;
        case VK_OEM_4:
            return Qt::Key_BracketLeft;
        case VK_OEM_6:
            return Qt::Key_BracketRight;
        case VK_OEM_1:
            return Qt::Key_Semicolon;
        case VK_OEM_7:
            return Qt::Key_Apostrophe;
        case VK_OEM_COMMA:
            return Qt::Key_Comma;
        case VK_OEM_PERIOD:
            return Qt::Key_Period;
        case VK_OEM_2:
            return Qt::Key_Slash;
        case VK_OEM_5:
            return Qt::Key_Backslash;
        case VK_OEM_3:
            return Qt::Key_QuoteLeft;
        default:
            return 0;
    }
}

UINT QtModsToWin32(Qt::KeyboardModifiers mods)
{
    UINT result = 0;
    if ((mods & Qt::ControlModifier) != 0)
    {
        result |= MOD_CONTROL;
    }
    if ((mods & Qt::AltModifier) != 0)
    {
        result |= MOD_ALT;
    }
    if ((mods & Qt::ShiftModifier) != 0)
    {
        result |= MOD_SHIFT;
    }
    return result;
}

Qt::KeyboardModifiers Win32ModsToQt(UINT mods)
{
    Qt::KeyboardModifiers result{};
    if ((mods & MOD_CONTROL) != 0U)
    {
        result |= Qt::ControlModifier;
    }
    if ((mods & MOD_ALT) != 0U)
    {
        result |= Qt::AltModifier;
    }
    if ((mods & MOD_SHIFT) != 0U)
    {
        result |= Qt::ShiftModifier;
    }
    return result;
}

}  // namespace

HotkeyManager::HotkeyManager(QObject* parent) : QObject(parent)
{
    instance = this;  // Set the singleton pointer
    LoadSettings();
    RegisterAll();
}

HotkeyManager::~HotkeyManager()
{
    UnregisterAll();
    instance = nullptr;
}

void HotkeyManager::SetBinding(HotkeyAction action, const HotkeyBinding& binding)
{
    auto index = static_cast<std::size_t>(action);
    if (index >= kHotkeyActionCount)
    {
        return;
    }

    bool was_registered = registered_;
    if (was_registered)
    {
        UnregisterAll();
    }

    bindings_.at(static_cast<std::size_t>(action)) = binding;

    if (was_registered)
    {
        RegisterAll();
    }
    SaveSettings();
}

HotkeyBinding HotkeyManager::GetBinding(HotkeyAction action) const
{
    auto index = static_cast<std::size_t>(action);
    if (index >= kHotkeyActionCount)
    {
        return {};
    }
    return bindings_.at(static_cast<std::size_t>(action));
}

void HotkeyManager::RegisterAll()
{
    if (registered_)
    {
        UnregisterAll();
    }

    HHOOK kb = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandle(nullptr), 0);
    HHOOK mouse = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(nullptr), 0);

    keyboard_hook.store(kb, std::memory_order_release);
    mouse_hook.store(mouse, std::memory_order_release);

    if (kb == nullptr || mouse == nullptr)
    {
        qWarning() << "[HOTKEY] Failed to install hooks. Keyboard:" << (kb != nullptr) << "Mouse:" << (mouse != nullptr)
                   << "Error:" << GetLastError();
    }

    registered_ = (kb != nullptr) && (mouse != nullptr);
}

void HotkeyManager::UnregisterAll()
{
    if (!registered_)
    {
        return;
    }

    if (keyboard_hook)
    {
        UnhookWindowsHookEx(keyboard_hook);
    }
    if (mouse_hook)
    {
        UnhookWindowsHookEx(mouse_hook);
    }

    keyboard_hook = nullptr;
    mouse_hook = nullptr;
    registered_ = false;
}

// Helper to check the current state of modifiers at the time of a click/keypress
UINT HotkeyManager::GetCurrentModifiers()
{
    UINT mods = 0;
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0)
    {
        mods |= MOD_CONTROL;
    }
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
    {
        mods |= MOD_SHIFT;
    }
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0)
    {
        mods |= MOD_ALT;
    }
    return mods;
}

void HotkeyManager::CheckAndTrigger(UINT vk, UINT modifiers)
{
    HWND foreground_win = GetForegroundWindow();

    HWND my_win = nullptr;
    if (auto* active_win = QApplication::activeWindow())
    {
        my_win = reinterpret_cast<HWND>(active_win->winId());  // NOLINT(performance-no-int-to-ptr)
    }

    if (foreground_win == my_win)
    {
        return;
    }

    for (int i = 0; i < kHotkeyActionCount; ++i)
    {
        const auto& binding = bindings_.at(static_cast<std::size_t>(i));
        if (binding.IsValid() && binding.vk == vk && binding.modifiers == modifiers)
        {
            emit HotkeyTriggered(static_cast<HotkeyAction>(i));
        }
    }
}

LRESULT CALLBACK HotkeyManager::KeyboardHookProc(int n_code, WPARAM w_param, LPARAM l_param)
{
    auto* inst = instance.load(std::memory_order_acquire);
    if (n_code >= 0 && (w_param == WM_KEYDOWN || w_param == WM_SYSKEYDOWN) && inst)
    {
        auto* hook_struct = reinterpret_cast<KBDLLHOOKSTRUCT*>(l_param);  // NOLINT(performance-no-int-to-ptr)
        UINT vk = hook_struct->vkCode;

        if (vk != VK_CONTROL && vk != VK_SHIFT && vk != VK_MENU && vk != VK_LCONTROL && vk != VK_RCONTROL
            && vk != VK_LSHIFT && vk != VK_RSHIFT && vk != VK_LMENU && vk != VK_RMENU)
        {
            inst->CheckAndTrigger(vk, GetCurrentModifiers());
        }
    }

    return CallNextHookEx(keyboard_hook.load(std::memory_order_acquire), n_code, w_param, l_param);
}

LRESULT CALLBACK HotkeyManager::MouseHookProc(int n_code, WPARAM w_param, LPARAM l_param)
{
    auto* inst = instance.load(std::memory_order_acquire);
    if (n_code >= 0 && inst != nullptr)
    {
        UINT vk = 0;
        if (w_param == WM_LBUTTONDOWN)
        {
            vk = VK_LBUTTON;
        }
        else if (w_param == WM_RBUTTONDOWN)
        {
            vk = VK_RBUTTON;
        }
        else if (w_param == WM_MBUTTONDOWN)
        {
            vk = VK_MBUTTON;
        }
        else if (w_param == WM_XBUTTONDOWN)
        {
            auto* hook_struct = reinterpret_cast<MSLLHOOKSTRUCT*>(l_param);  // NOLINT(performance-no-int-to-ptr)
            vk = (HIWORD(hook_struct->mouseData) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
        }

        if (vk != 0)
        {
            inst->CheckAndTrigger(vk, GetCurrentModifiers());
        }
    }
    return CallNextHookEx(mouse_hook.load(std::memory_order_acquire), n_code, w_param, l_param);
}

void HotkeyManager::SaveSettings()
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);

    for (int i = 0; i < kHotkeyActionCount; ++i)
    {
        auto action = static_cast<HotkeyAction>(i);
        const auto& binding = bindings_.at(static_cast<std::size_t>(i));
        auto key = QString(ActionToSettingsKey(action));
        settings.setValue(key + "_mods", binding.modifiers);
        settings.setValue(key + "_vk", binding.vk);
    }

    settings.endGroup();
}

void HotkeyManager::LoadSettings()
{
    QSettings settings;
    settings.beginGroup(kSettingsGroup);

    for (int i = 0; i < kHotkeyActionCount; ++i)
    {
        auto action = static_cast<HotkeyAction>(i);
        auto& binding = bindings_.at(static_cast<std::size_t>(i));
        auto key = QString(ActionToSettingsKey(action));
        binding.modifiers = settings.value(key + "_mods", 0).toUInt();
        binding.vk = settings.value(key + "_vk", 0).toUInt();
    }

    settings.endGroup();
}

HotkeyBinding HotkeyManager::FromKeySequence(const QKeySequence& seq)
{
    if (seq.isEmpty())
    {
        return {};
    }

    auto combo = seq[0];
    HotkeyBinding binding;
    binding.vk = QtKeyToVk(combo.key());
    binding.modifiers = QtModsToWin32(combo.keyboardModifiers());
    return binding;
}

QKeySequence HotkeyManager::ToKeySequence(const HotkeyBinding& binding)
{
    if (!binding.IsValid())
    {
        return {};
    }

    int qt_key = VkToQtKey(binding.vk);
    if (qt_key == 0)
    {
        return {};
    }

    return {QKeyCombination(Win32ModsToQt(binding.modifiers), static_cast<Qt::Key>(qt_key))};
}

}  // namespace scrambler::ui
