#include "app/LanguageManager.hpp"
#include "app/MainWindow.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("anSofts"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("ansofts.it"));
    QCoreApplication::setApplicationName(QStringLiteral("ChronoLab"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.2"));

    QFont font(QStringLiteral("Segoe UI"));
    font.setStyleHint(QFont::SansSerif);
    application.setFont(font);

    chronolab::LanguageManager languageManager;
    chronolab::MainWindow window(languageManager);
    window.show();
    return application.exec();
}
