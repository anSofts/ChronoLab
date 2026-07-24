#pragma once

#include <QHash>
#include <QString>
#include <QTranslator>

#include <vector>

namespace chronolab {

struct LanguageOption {
    QString code;
    QString nativeName;
};

class CatalogTranslator final : public QTranslator {
public:
    bool loadCatalog(const QString& resourcePath);
    void clear();

    [[nodiscard]] QString translate(
        const char* context,
        const char* sourceText,
        const char* disambiguation = nullptr,
        int n = -1) const override;

private:
    QHash<QString, QString> m_messages;
};

class LanguageManager final {
public:
    LanguageManager();

    [[nodiscard]] static const std::vector<LanguageOption>& availableLanguages();
    [[nodiscard]] QString currentLanguage() const;

    void setLanguage(const QString& code);

private:
    static QString normalizedLanguage(const QString& code);
    bool installLanguage(const QString& code);

    CatalogTranslator m_applicationTranslator;
    QTranslator m_qtTranslator;
    QString m_currentLanguage;
};

} // namespace chronolab
