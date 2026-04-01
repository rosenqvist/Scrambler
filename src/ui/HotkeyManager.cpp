#include "ui/HotkeyManager.h"

#include <QApplication>
#include <QSettings>

namespace scrambler::ui
{

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
    QApplication::instance()->installNativeEventFilter(this);
    LoadSettings();
    RegisterAll();
}

HotkeyManager::~HotkeyManager()
{
    UnregisterAll();
    QApplication::instance()->removeNativeEventFilter(this);
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

    for (int i = 0; i < kHotkeyActionCount; ++i)
    {
        const auto& binding = bindings_.at(static_cast<std::size_t>(i));
        if (binding.IsValid())
        {
            // MOD_NOREPEAT prevents repeated WM_HOTKEY messages when the key is held
            RegisterHotKey(nullptr, kBaseId + i, binding.modifiers | MOD_NOREPEAT, binding.vk);
        }
    }
    registered_ = true;
}

void HotkeyManager::UnregisterAll()
{
    if (!registered_)
    {
        return;
    }

    for (int i = 0; i < kHotkeyActionCount; ++i)
    {
        UnregisterHotKey(nullptr, kBaseId + i);
    }
    registered_ = false;
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

bool HotkeyManager::nativeEventFilter(const QByteArray& event_type, void* message, qintptr* /*result*/)
{
    if (event_type != "windows_generic_MSG")
    {
        return false;
    }

    auto* msg = static_cast<MSG*>(message);
    if (msg->message != WM_HOTKEY)
    {
        return false;
    }

    int id = static_cast<int>(msg->wParam) - kBaseId;
    if (id >= 0 && id < kHotkeyActionCount)
    {
        emit HotkeyTriggered(static_cast<HotkeyAction>(id));
    }
    return true;
}

}  // namespace scrambler::ui
