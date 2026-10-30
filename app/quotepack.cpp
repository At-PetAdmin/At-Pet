#include "quotepack.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

QString translated(const char *text) {
    return QCoreApplication::translate("QuotePack", text);
}

QString canonicalCategory(const QString &raw) {
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("poke"), QStringLiteral("poke")},
        {QStringLiteral("poked"), QStringLiteral("poke")},
        {QStringLiteral("interact"), QStringLiteral("poke")},
        {QStringLiteral("click"), QStringLiteral("poke")},
        {QStringLiteral("done"), QStringLiteral("done")},
        {QStringLiteral("completed"), QStringLiteral("done")},
        {QStringLiteral("completion"), QStringLiteral("done")},
        {QStringLiteral("finished"), QStringLiteral("done")},
        {QStringLiteral("error"), QStringLiteral("error")},
        {QStringLiteral("failed"), QStringLiteral("error")},
        {QStringLiteral("failure"), QStringLiteral("error")},
        {QStringLiteral("cancel"), QStringLiteral("cancel")},
        {QStringLiteral("cancelled"), QStringLiteral("cancel")},
        {QStringLiteral("canceled"), QStringLiteral("cancel")},
        {QStringLiteral("aborted"), QStringLiteral("cancel")},
        {QStringLiteral("idle"), QStringLiteral("idle")},
        {QStringLiteral("waiting"), QStringLiteral("idle")},
        {QStringLiteral("random"), QStringLiteral("idle")},
    };
    return aliases.value(raw.trimmed().toLower());
}

QString categoryKey(QuotePack::Category category) {
    switch (category) {
    case QuotePack::Category::Poke:
        return QStringLiteral("poke");
    case QuotePack::Category::Done:
        return QStringLiteral("done");
    case QuotePack::Category::Error:
        return QStringLiteral("error");
    case QuotePack::Category::Cancel:
        return QStringLiteral("cancel");
    case QuotePack::Category::Idle:
        return QStringLiteral("idle");
    }
    return {};
}

QString normalizeLanguage(const QString &raw) {
    return raw.trimmed().toLower().replace(QLatin1Char('-'), QLatin1Char('_'));
}

QString baseLanguage(const QString &normalized) {
    const int separator = normalized.indexOf(QLatin1Char('_'));
    return separator <= 0 ? normalized : normalized.left(separator);
}

// Accepts a single string, an array of strings and an array of objects with a "text" field,
// matching the tolerant `quotes` format used by pet.json. Blank lines and non-string entries
// are dropped.
QStringList collectEntries(const QJsonValue &value) {
    QStringList result;
    const auto append = [&result](const QString &text) {
        const QString trimmed = text.trimmed();
        if (!trimmed.isEmpty() && !result.contains(trimmed))
            result.append(trimmed);
    };
    const auto appendText = [&append](const QString &text) {
        for (const QString &line : text.split(QLatin1Char('\n')))
            append(line);
    };
    if (value.isString()) {
        appendText(value.toString());
    } else if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            if (entry.isString())
                appendText(entry.toString());
            else if (entry.isObject())
                appendText(entry.toObject().value(QStringLiteral("text")).toString());
        }
    } else if (value.isObject()) {
        appendText(value.toObject().value(QStringLiteral("text")).toString());
    }
    return result;
}

// QJsonObject keeps its keys sorted, so the on-disk language order is recovered from the raw
// text. Only keys of the top-level object are collected; values are skipped by tracking
// strings, escapes and brace depth.
QStringList topLevelKeys(const QByteArray &json) {
    QStringList keys;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    bool expectKey = false;
    int stringStart = -1;
    for (int i = 0; i < json.size(); ++i) {
        const char c = json.at(i);
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
                if (expectKey && depth == 1 && stringStart >= 0) {
                    const QByteArray literal = json.mid(stringStart, i - stringStart + 1);
                    const QString key = QJsonDocument::fromJson(QByteArray("{") + literal + QByteArray(":0}"))
                                            .object()
                                            .keys()
                                            .value(0);
                    if (!key.isEmpty() && !keys.contains(key))
                        keys.append(key);
                }
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            escaped = false;
            stringStart = i;
        } else if (c == '{') {
            ++depth;
            expectKey = depth == 1;
        } else if (c == '}') {
            --depth;
        } else if (c == ':' && depth == 1) {
            expectKey = false;
        } else if (c == ',' && depth == 1) {
            expectKey = true;
        }
    }
    return keys;
}

} // namespace

QString QuotePack::defaultPath() {
    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dataDirectory).filePath(QStringLiteral("quotes.json"));
}

QuotePack QuotePack::load(const QString &path, QString *errorMessage) {
    if (errorMessage)
        errorMessage->clear();
    QuotePack pack;
    if (path.isEmpty())
        return pack;

    QFile file(path);
    if (!file.exists())
        return pack;
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = translated("Could not read %1.").arg(QDir::toNativeSeparators(path));
        return pack;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage)
            *errorMessage = translated("Invalid JSON at offset %1: %2")
                                .arg(parseError.offset)
                                .arg(parseError.errorString());
        return pack;
    }
    if (!document.isObject()) {
        if (errorMessage)
            *errorMessage = translated("The quote pack must be a JSON object.");
        return pack;
    }

    const QJsonObject root = document.object();
    QStringList orderedKeys = topLevelKeys(bytes);
    for (const QString &key : root.keys()) {
        if (!orderedKeys.contains(key))
            orderedKeys.append(key);
    }

    for (const QString &rawLanguage : orderedKeys) {
        const QJsonValue languageValue = root.value(rawLanguage);
        if (!languageValue.isObject())
            continue;
        const QString language = normalizeLanguage(rawLanguage);
        if (language.isEmpty())
            continue;

        const QJsonObject languageObject = languageValue.toObject();
        QHash<QString, QStringList> categories;
        for (auto it = languageObject.constBegin(); it != languageObject.constEnd(); ++it) {
            const QString category = canonicalCategory(it.key());
            if (category.isEmpty())
                continue;
            const QStringList entries = collectEntries(it.value());
            if (entries.isEmpty())
                continue;
            QStringList &target = categories[category];
            for (const QString &entry : entries) {
                if (!target.contains(entry))
                    target.append(entry);
            }
        }
        if (categories.isEmpty())
            continue;

        QHash<QString, QStringList> &bucket = pack.data_[language];
        for (auto it = categories.constBegin(); it != categories.constEnd(); ++it) {
            QStringList &target = bucket[it.key()];
            for (const QString &entry : it.value()) {
                if (!target.contains(entry))
                    target.append(entry);
            }
        }
        if (!pack.languageOrder_.contains(language))
            pack.languageOrder_.append(language);
    }

    if (pack.isEmpty()) {
        if (errorMessage)
            *errorMessage = translated("The quote pack contains no usable quotes.");
        pack.languageOrder_.clear();
    }
    return pack;
}

bool QuotePack::install(const QString &sourcePath, QString *errorMessage) {
    if (errorMessage)
        errorMessage->clear();

    QString loadError;
    const QuotePack validated = load(sourcePath, &loadError);
    if (validated.isEmpty()) {
        if (errorMessage)
            *errorMessage = loadError.isEmpty()
                ? translated("The quote pack contains no usable quotes.")
                : loadError;
        return false;
    }

    QFile source(sourcePath);
    if (!source.exists() || !source.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = translated("Could not read %1.").arg(QDir::toNativeSeparators(sourcePath));
        return false;
    }
    const QByteArray bytes = source.readAll();
    source.close();

    const QString target = defaultPath();
    const QString directory = QFileInfo(target).absolutePath();
    if (!QDir().mkpath(directory)) {
        if (errorMessage)
            *errorMessage = translated("Could not create %1.").arg(QDir::toNativeSeparators(directory));
        return false;
    }

    // QSaveFile writes a temporary file and renames it into place, so a failed import can never
    // leave a half-written pack behind or truncate the previously installed one.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (errorMessage)
            *errorMessage = translated("Could not write %1.").arg(QDir::toNativeSeparators(target));
        return false;
    }
    return true;
}

bool QuotePack::isEmpty() const {
    return data_.isEmpty();
}

bool QuotePack::hasCategory(Category category) const {
    const QString key = categoryKey(category);
    if (key.isEmpty())
        return false;
    for (auto it = data_.constBegin(); it != data_.constEnd(); ++it) {
        if (!it.value().value(key).isEmpty())
            return true;
    }
    return false;
}

int QuotePack::quoteCount() const {
    int count = 0;
    for (auto it = data_.constBegin(); it != data_.constEnd(); ++it) {
        for (auto category = it.value().constBegin(); category != it.value().constEnd(); ++category)
            count += static_cast<int>(category.value().size());
    }
    return count;
}

int QuotePack::languageCount() const {
    return data_.size();
}

QStringList QuotePack::languages() const {
    return languageOrder_;
}

QStringList QuotePack::quotes(Category category, const QString &language) const {
    const QString key = categoryKey(category);
    if (key.isEmpty() || data_.isEmpty())
        return {};

    const auto inLanguage = [this, &key](const QString &normalized) {
        const auto it = data_.constFind(normalized);
        return it == data_.constEnd() ? QStringList() : it.value().value(key);
    };

    const QString requested = normalizeLanguage(language);
    if (!requested.isEmpty()) {
        const QStringList exact = inLanguage(requested);
        if (!exact.isEmpty())
            return exact;

        // zh_CN -> zh, en_US -> en, and any regional variant of the same base language.
        const QString base = baseLanguage(requested);
        const QStringList bare = inLanguage(base);
        if (!bare.isEmpty())
            return bare;
        for (const QString &candidate : languageOrder_) {
            if (baseLanguage(candidate) != base)
                continue;
            const QStringList regional = inLanguage(candidate);
            if (!regional.isEmpty())
                return regional;
        }
    }

    const QStringList english = inLanguage(QStringLiteral("en"));
    if (!english.isEmpty())
        return english;
    for (const QString &candidate : languageOrder_) {
        if (baseLanguage(candidate) != QLatin1String("en"))
            continue;
        const QStringList regional = inLanguage(candidate);
        if (!regional.isEmpty())
            return regional;
    }

    // Last resort: the first language of the file that has this category at all.
    for (const QString &candidate : languageOrder_) {
        const QStringList fallback = inLanguage(candidate);
        if (!fallback.isEmpty())
            return fallback;
    }
    return {};
}

QString QuotePack::randomQuote(Category category, const QString &language) const {
    const QStringList lines = quotes(category, language);
    if (lines.isEmpty())
        return {};
    return lines.at(QRandomGenerator::global()->bounded(static_cast<int>(lines.size())));
}
