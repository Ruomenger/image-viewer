#include "MainWindow.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ImageViewer"));
    QApplication::setOrganizationName(QStringLiteral("image-viewer"));

    MainWindow window;
    window.resize(1100, 750);
    window.show();

    // Allow `image-viewer <path>` to open straight away.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        window.openPath(args.at(1));

    return QApplication::exec();
}
