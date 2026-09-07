#include "mainwindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QStringList>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("qMDviewer"));
    QApplication::setApplicationDisplayName(QStringLiteral("qMDviewer"));
    QApplication::setApplicationVersion(QStringLiteral(QMDV_VERSION));
    QApplication::setOrganizationName(QStringLiteral("qMDviewer"));
    QApplication::setOrganizationDomain(QStringLiteral("qmdviewer.org"));

    QApplication::setWindowIcon(MainWindow::applicationIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "Lightweight Markdown viewer."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QCoreApplication::translate("main", "file"),
        QCoreApplication::translate("main", "Markdown file to open ('-' reads stdin)."));
    parser.process(app);

    MainWindow window;
    window.show();

    const QStringList arguments = parser.positionalArguments();
    if (!arguments.isEmpty()) {
        const QString target = arguments.first();
        if (target == QLatin1String("-")) {
            QFile input;
            if (input.open(stdin, QIODevice::ReadOnly | QIODevice::Text)) {
                window.showText(QString::fromUtf8(input.readAll()),
                                QCoreApplication::translate("main", "Standard input"));
            }
        } else if (!window.openFile(target)) {
            QMessageBox::warning(&window, QApplication::applicationDisplayName(),
                                 QCoreApplication::translate("main", "Cannot read %1")
                                     .arg(QDir::toNativeSeparators(target)));
        }
    }

    return app.exec();
}
