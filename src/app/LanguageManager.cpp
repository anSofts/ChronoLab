#include "app/LanguageManager.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

namespace chronolab {
namespace {

QString messageKey(const QString& context, const QString& source)
{
    return context + QChar(0x001f) + source;
}

} // namespace

bool CatalogTranslator::loadCatalog(const QString& resourcePath)
{
    clear();
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject contexts =
        document.object().value(QStringLiteral("translations")).toObject();
    for (auto contextIt = contexts.begin(); contextIt != contexts.end(); ++contextIt) {
        const QJsonObject messages = contextIt.value().toObject();
        for (auto messageIt = messages.begin(); messageIt != messages.end(); ++messageIt) {
            const QString translated = messageIt.value().toString();
            if (!translated.isEmpty()) {
                m_messages.insert(
                    messageKey(contextIt.key(), messageIt.key()), translated);
            }
        }
    }
    return true;
}

void CatalogTranslator::clear()
{
    m_messages.clear();
}

QString CatalogTranslator::translate(
    const char* context,
    const char* sourceText,
    const char* disambiguation,
    int n) const
{
    Q_UNUSED(disambiguation);
    Q_UNUSED(n);
    if (!context || !sourceText)
        return {};
    return m_messages.value(messageKey(
        QString::fromUtf8(context), QString::fromUtf8(sourceText)));
}

LanguageManager::LanguageManager()
{
    QSettings settings;
    const QString saved = settings.value(QStringLiteral("ui/language")).toString();
    const QString initial = saved.isEmpty()
        ? normalizedLanguage(QLocale::system().name())
        : normalizedLanguage(saved);
    if (!installLanguage(initial))
        installLanguage(QStringLiteral("it"));
}

const std::vector<LanguageOption>& LanguageManager::availableLanguages()
{
    static const std::vector<LanguageOption> languages {
        {QStringLiteral("it"), QStringLiteral("Italiano")},
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("fr"), QStringLiteral("Français")},
        {QStringLiteral("de"), QStringLiteral("Deutsch")},
        {QStringLiteral("es"), QStringLiteral("Español")}
    };
    return languages;
}

QString LanguageManager::currentLanguage() const
{
    return m_currentLanguage;
}

void LanguageManager::setLanguage(const QString& code)
{
    const QString normalized = normalizedLanguage(code);
    QSettings settings;
    settings.setValue(QStringLiteral("ui/language"), normalized);
    settings.sync();
}

QString LanguageManager::normalizedLanguage(const QString& code)
{
    const QString shortCode = code.left(2).toLower();
    for (const auto& language : availableLanguages()) {
        if (language.code == shortCode)
            return shortCode;
    }
    return QStringLiteral("it");
}

bool LanguageManager::installLanguage(const QString& code)
{
    auto* application = QCoreApplication::instance();
    if (!application)
        return false;

    application->removeTranslator(&m_applicationTranslator);
    application->removeTranslator(&m_qtTranslator);
    m_applicationTranslator.clear();

    if (code != QStringLiteral("it")) {
        const QString catalog =
            QStringLiteral(":/i18n/chronolab_%1.json").arg(code);
        if (!m_applicationTranslator.loadCatalog(catalog)) {
            m_currentLanguage = QStringLiteral("it");
            QLocale::setDefault(QLocale(m_currentLanguage));
            return false;
        }
        application->installTranslator(&m_applicationTranslator);
    }

    const QString qtCatalog = QStringLiteral("qtbase_%1").arg(code);
    if (m_qtTranslator.load(qtCatalog, QLibraryInfo::path(
            QLibraryInfo::TranslationsPath))) {
        application->installTranslator(&m_qtTranslator);
    }

    m_currentLanguage = code;
    QLocale::setDefault(QLocale(code));
    return true;
}

} // namespace chronolab
