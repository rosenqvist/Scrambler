#include "ui/MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Scrambler");

    scrambler::ui::MainWindow window;
    window.show();

    return QApplication::exec();
}
