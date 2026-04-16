#pragma once

#include "ui/HotkeyManager.h"

#include <qevent.h>
#include <QKeyEvent>
#include <QLineEdit>
#include <Qnamespace.h>

#include <windows.h>

#include <minwindef.h>
#include <winuser.h>

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
    void mousePressEvent(QMouseEvent* event) override
    {
        UINT vk = 0;
        switch (event->button())
        {
            case Qt::XButton1:
                vk = VK_XBUTTON1;
                break;
            case Qt::XButton2:
                vk = VK_XBUTTON2;
                break;

            // Disallow, left,right and middle button clicks
            // binding these will cause a UX nightmare
            case Qt::LeftButton:
            case Qt::RightButton:
            case Qt::MiddleButton:
            default:
                return;
        }

        // Capture modifiers (like Ctrl + Mouse 4)
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

        binding_.vk = vk;
        binding_.modifiers = hotkey_mods;
        setText(BindingToString(binding_));
        emit BindingChanged(binding_);
    }

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

        // manually handle mouse names
        if (binding.vk == VK_LBUTTON)
        {
            parts << "Left Click";
        }
        else if (binding.vk == VK_RBUTTON)
        {
            parts << "Right Click";
        }
        else if (binding.vk == VK_MBUTTON)
        {
            parts << "Middle Click";
        }
        else if (binding.vk == VK_XBUTTON1)
        {
            parts << "Mouse 4";
        }
        else if (binding.vk == VK_XBUTTON2)
        {
            parts << "Mouse 5";
        }
        else
        {
            // Keep your existing MapVirtualKeyW logic here for standard keys
            UINT scan = MapVirtualKeyW(binding.vk, MAPVK_VK_TO_VSC);
            std::array<wchar_t, 64> name{};
            if (GetKeyNameTextW(static_cast<LONG>(scan) << 16, name.data(), static_cast<int>(name.size())) > 0)
            {
                parts << QString::fromWCharArray(name.data());
            }
            else
            {
                parts << QString::number(binding.vk);
            }
        }

        return parts.join("+");
    }

    HotkeyBinding binding_;
};

}  // namespace scrambler::ui
