#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

// Quote packs group lines by interface language and event category. The global pack lives
// at AppDataLocation/quotes.json; independent packs live at pet-quotes/<encoded pet id>.json.
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
    static QString petPath(const QString &petId);

    // A missing file yields an empty pack without an error. Unreadable, malformed or empty
    // files yield an empty pack plus a user-facing message in errorMessage.
    static QuotePack load(const QString &path, QString *errorMessage = nullptr);

    // Validates sourcePath first and only then atomically replaces defaultPath().
    static bool install(const QString &sourcePath, QString *errorMessage = nullptr);
    static bool installForPet(const QString &sourcePath, const QString &petId,
                              QString *errorMessage = nullptr);
    static bool removeForPet(const QString &petId, QString *errorMessage = nullptr);

    bool isEmpty() const;
    bool hasCategory(Category category) const;
    int quoteCount() const;
    int languageCount() const;
    QStringList languages() const;

    // Resolves the requested language with the documented fallbacks and returns its lines.
    QStringList quotes(Category category, const QString &language) const;
    QString randomQuote(Category category, const QString &language) const;

private:
    static bool installToPath(const QString &sourcePath, const QString &target,
                              QString *errorMessage);
    QHash<QString, QHash<QString, QStringList>> data_;
    QStringList languageOrder_;
};
