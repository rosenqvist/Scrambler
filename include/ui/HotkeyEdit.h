#pragma once

#include "ui/HotkeyManager.h"

#include <QKeyEvent>
#include <QLineEdit>

namespace scrambler::ui
{

class HotkeyEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit HotkeyEdit(QWidget* parent = nullptr) : QLineEdit(parent)
    {
        setReadOnly(true);
        setPlaceholderText("Click and press a key...");
    }

    void SetBinding(const HotkeyBinding& binding)
    {
        binding_ = binding;
        setText(BindingToString(binding_));
    }

    [[nodiscard]] HotkeyBinding GetBinding() const
    {
        return binding_;
    }

signals:
    void BindingChanged(const HotkeyBinding& binding);

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        auto key = static_cast<UINT>(event->nativeVirtualKey());
        if (key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU || key == VK_LCONTROL || key == VK_RCONTROL
            || key == VK_LSHIFT || key == VK_RSHIFT || key == VK_LMENU || key == VK_RMENU)
        {
            return;
        }

        UINT hotkey_mods = 0;
        if ((event->modifiers() & Qt::ControlModifier) != 0)
        {
            hotkey_mods |= MOD_CONTROL;
        }
        if ((event->modifiers() & Qt::ShiftModifier) != 0)
        {
            hotkey_mods |= MOD_SHIFT;
        }
        if ((event->modifiers() & Qt::AltModifier) != 0)
        {
            hotkey_mods |= MOD_ALT;
        }

        if (key == VK_ESCAPE && hotkey_mods == 0)
        {
            binding_ = {};
            setText("");
            emit BindingChanged(binding_);
            return;
        }

        binding_.vk = key;
        binding_.modifiers = hotkey_mods;
        setText(BindingToString(binding_));
        emit BindingChanged(binding_);
    }

private:
    static QString BindingToString(const HotkeyBinding& binding)
    {
        if (!binding.IsValid())
        {
            return {};
        }

        QStringList parts;
        if ((binding.modifiers & MOD_CONTROL) != 0U)
        {
            parts << "Ctrl";
        }
        if ((binding.modifiers & MOD_ALT) != 0U)
        {
            parts << "Alt";
        }
        if ((binding.modifiers & MOD_SHIFT) != 0U)
        {
            parts << "Shift";
        }

        // Map VK to a readable name
        UINT scan = MapVirtualKeyW(binding.vk, MAPVK_VK_TO_VSC);
        std::array<wchar_t, 64> name{};
        // GetKeyNameTextW expects the scan code in bits 16-23
        // and the extended flag in bit 24
        if (GetKeyNameTextW(static_cast<LONG>(scan) << 16, name.data(), static_cast<int>(name.size())) > 0)
        {
            parts << QString::fromWCharArray(name.data());
        }
        else
        {
            parts << QString::number(binding.vk);
        }

        return parts.join("+");
    }

    HotkeyBinding binding_;
};

}  // namespace scrambler::ui
