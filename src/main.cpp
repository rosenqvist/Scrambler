#include "ui/MainWindow.h"

#include <QApplication>
#include <QPixmap>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Scrambler");

    // Workaround: Qt 6 bug in qt_createIconMask asserts on Format_Mono
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);
    QApplication::setWindowIcon(QIcon(pixmap));

    scrambler::ui::MainWindow window;
    window.show();

    return QApplication::exec();
}
