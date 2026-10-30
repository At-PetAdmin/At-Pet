#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// Global quote pack shared by every pet. The file groups lines by interface language and
// event category and lives at `QStandardPaths::AppDataLocation/quotes.json`.
class QuotePack final {
public:
    enum class Category {
        Poke,
        Done,
        Error,
        Cancel,
        Idle,
    };

    static QString defaultPath();

    // A missing file yields an empty pack without an error. Unreadable, malformed or empty
    // files yield an empty pack plus a user-facing message in errorMessage.
    static QuotePack load(const QString &path, QString *errorMessage = nullptr);

    // Validates sourcePath first and only then atomically replaces defaultPath().
    static bool install(const QString &sourcePath, QString *errorMessage = nullptr);

    bool isEmpty() const;
    bool hasCategory(Category category) const;
    int quoteCount() const;
    int languageCount() const;
    QStringList languages() const;

    // Resolves the requested language with the documented fallbacks and returns its lines.
    QStringList quotes(Category category, const QString &language) const;
    QString randomQuote(Category category, const QString &language) const;

private:
    QHash<QString, QHash<QString, QStringList>> data_;
    QStringList languageOrder_;
};
