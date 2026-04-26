#include "ui/MainWindow.h"

#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QPalette>

#include <windows.h>

#include <bit>
#include <cstdlib>
#include <dwmapi.h>
#include <memory>
#include <type_traits>
#include <winsvc.h>

#ifndef DWMWA_CAPTION_COLOR
    #define DWMWA_CAPTION_COLOR 35
#endif

#ifndef SCRAMBLER_VERSION_STRING
    #define SCRAMBLER_VERSION_STRING "dev"
#endif

namespace
{
struct ServiceHandleCloser
{
    void operator()(SC_HANDLE handle) const noexcept
    {
        if (handle != nullptr)
        {
            CloseServiceHandle(handle);
        }
    }
};

using UniqueServiceHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, ServiceHandleCloser>;

void StopWinDivertService()
{
    UniqueServiceHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scm)
    {
        return;
    }

    UniqueServiceHandle svc(OpenServiceW(scm.get(), L"WinDivert", SERVICE_STOP | DELETE));
    if (svc)
    {
        SERVICE_STATUS status{};
        ControlService(svc.get(), SERVICE_CONTROL_STOP, &status);
    }
}
}  // namespace

int main(int argc, char* argv[])
{
    std::atexit(StopWinDivertService);

    QApplication app(argc, argv);
    QApplication::setOrganizationName("Scrambler");
    QApplication::setApplicationName("Scrambler");
    QApplication::setApplicationVersion(SCRAMBLER_VERSION_STRING);

    QIcon icon(":/icons/scrambler_icon_256.png");

    QApplication::setWindowIcon(icon);

    QApplication::setStyle("Fusion");

    // Warm neutral theme.
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(226, 223, 218));
    palette.setColor(QPalette::WindowText, QColor(38, 36, 32));
    palette.setColor(QPalette::Base, QColor(238, 235, 230));
    palette.setColor(QPalette::AlternateBase, QColor(230, 227, 222));
    palette.setColor(QPalette::Text, QColor(38, 36, 32));
    palette.setColor(QPalette::Button, QColor(218, 215, 210));
    palette.setColor(QPalette::ButtonText, QColor(38, 36, 32));
    palette.setColor(QPalette::Highlight, QColor(105, 88, 142));
    palette.setColor(QPalette::HighlightedText, QColor(246, 244, 240));
    palette.setColor(QPalette::Mid, QColor(182, 178, 172));
    palette.setColor(QPalette::Light, QColor(246, 244, 240));
    palette.setColor(QPalette::Midlight, QColor(234, 231, 226));
    palette.setColor(QPalette::Dark, QColor(152, 148, 142));
    palette.setColor(QPalette::Shadow, QColor(122, 118, 112));
    palette.setColor(QPalette::PlaceholderText, QColor(132, 128, 120));
    palette.setColor(QPalette::ToolTipBase, QColor(234, 231, 226));
    palette.setColor(QPalette::ToolTipText, QColor(44, 42, 38));
    palette.setColor(QPalette::Link, QColor(92, 74, 128));

    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(152, 148, 142));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(152, 148, 142));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(152, 148, 142));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(195, 192, 186));

    QApplication::setPalette(palette);

    scrambler::ui::MainWindow window;

    // Match title bar to window background (Windows 11 only)
    auto* hwnd = std::bit_cast<HWND>(window.winId());
    COLORREF caption_color = RGB(226, 223, 218);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption_color, sizeof(caption_color));

    window.show();

    return QApplication::exec();
}
