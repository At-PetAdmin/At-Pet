#include "updateswidget.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSysInfo>
#include <QVBoxLayout>
#include <QVersionNumber>

#ifndef AT_PET_VERSION
#define AT_PET_VERSION "dev"
#endif

namespace {
const QString repository = QStringLiteral("https://github.com/At-PetAdmin/At-Pet");
struct Version { QVersionNumber base; int stage = 4; int iteration = 0; bool valid = false; };
Version version(QString value) {
    static const QRegularExpression expression(QStringLiteral("^v?(\\d+\\.\\d+\\.\\d+)(?:-?(alpha|beta|rc)(?:[.-]?(\\d+))?)?(?:\\+[^ ]+)?$"), QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(value.trimmed());
    if (!match.hasMatch()) return {};
    const QString suffix = match.captured(2).toLower();
    return {QVersionNumber::fromString(match.captured(1)), suffix == "alpha" ? 1 : suffix == "beta" ? 2 : suffix == "rc" ? 3 : 4,
        match.captured(3).toInt(), true};
}
QUrl releaseUrl(const QJsonObject &release) {
    const QUrl url(release.value("html_url").toString());
    return url.toString().startsWith(repository + "/releases/tag/") ? url : QUrl();
}
}

int updates::compareVersions(const QString &a, const QString &b) {
    const auto left = version(a), right = version(b);
    if (!left.valid || !right.valid) return 0;
    const int base = QVersionNumber::compare(left.base, right.base);
    if (base) return base;
    if (left.stage != right.stage) return left.stage - right.stage;
    return left.iteration - right.iteration;
}

QJsonObject updates::latestRelease(const QJsonArray &releases, bool prerelease) {
    QJsonObject latest;
    for (const auto &value : releases) {
        const auto release = value.toObject();
        const QString tag = release.value("tag_name").toString();
        const auto parsed = version(tag);
        const bool preview = release.value("prerelease").toBool() || parsed.stage < 4;
        if (release.value("draft").toBool() || !parsed.valid || preview != prerelease || releaseUrl(release).isEmpty()) continue;
        if (latest.isEmpty() || compareVersions(tag, latest.value("tag_name").toString()) > 0) latest = release;
    }
    return latest;
}

QUrl updates::assetUrl(const QJsonObject &release, const QString &architecture) {
    if (architecture != "amd64" && architecture != "arm64") return {};
    const QString name = QString("At-Pet-windows-%1.zip").arg(architecture);
    for (const auto &value : release.value("assets").toArray()) {
        const auto asset = value.toObject();
        const QUrl url(asset.value("browser_download_url").toString());
        if (asset.value("name").toString() == name && url.toString().startsWith(repository + "/releases/download/")) return url;
    }
    return {};
}

UpdatesWidget::UpdatesWidget(QWidget *parent, QNetworkAccessManager *network) : QWidget(parent), network_(network ? network : new QNetworkAccessManager(this)) {
    setObjectName("updatesWidget");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    current_ = new QLabel(this);
    description_ = new QLabel(this);
    description_->setWordWrap(true);
    layout->addWidget(current_);
    layout->addWidget(description_);
    auto *actions = new QHBoxLayout;
    channel_ = new QComboBox(this);
    channel_->setObjectName("updateChannel");
    channel_->addItem(QString(), false);
    channel_->addItem(QString(), true);
    channel_->setCurrentIndex(version(QString::fromLatin1(AT_PET_VERSION)).stage < 4 ? 1 : 0);
    architecture_ = new QComboBox(this);
    architecture_->addItem("Windows AMD64", "amd64");
    architecture_->addItem("Windows ARM64", "arm64");
    architecture_->setCurrentIndex(QSysInfo::currentCpuArchitecture() == "arm64" ? 1 : 0);
    check_ = new QPushButton(this);
    check_->setObjectName("checkUpdatesButton");
    actions->addWidget(channel_);
    actions->addWidget(architecture_);
    actions->addStretch();
    actions->addWidget(check_);
    layout->addLayout(actions);
    status_ = new QLabel(this);
    status_->setObjectName("updateStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    notes_ = new QPlainTextEdit(this);
    notes_->setReadOnly(true);
    layout->addWidget(notes_, 1);
    auto *links = new QHBoxLayout;
    releasePage_ = new QPushButton(this);
    download_ = new QPushButton(this);
    download_->setObjectName("primaryBtn");
    links->addWidget(releasePage_);
    links->addStretch();
    links->addWidget(download_);
    layout->addLayout(links);
    connect(check_, &QPushButton::clicked, this, &UpdatesWidget::check);
    connect(channel_, &QComboBox::currentIndexChanged, this, &UpdatesWidget::render);
    connect(architecture_, &QComboBox::currentIndexChanged, this, &UpdatesWidget::render);
    connect(releasePage_, &QPushButton::clicked, this, [this] {
        const auto url = releaseUrl(updates::latestRelease(releases_, channel_->currentData().toBool()));
        if (!QDesktopServices::openUrl(url.isEmpty() ? QUrl(repository + "/releases") : url)) status_->setText(tr("Could not open the browser."));
    });
    connect(download_, &QPushButton::clicked, this, [this] {
        const auto url = updates::assetUrl(updates::latestRelease(releases_, channel_->currentData().toBool()), architecture_->currentData().toString());
        if (!url.isEmpty() && !QDesktopServices::openUrl(url)) status_->setText(tr("Could not open the browser."));
    });
    retranslateUi();
}

void UpdatesWidget::retranslateUi() {
    current_->setText(tr("Current version: %1").arg(QString::fromLatin1(AT_PET_VERSION)));
    description_->setText(tr("Get official stable and beta builds from At-PetAdmin/At-Pet. Downloads open in your browser; close At-Pet before extracting a new portable package. Keep your existing settings and pets."));
    channel_->setItemText(0, tr("Stable releases"));
    channel_->setItemText(1, tr("Beta / prereleases"));
    channel_->setAccessibleName(tr("Release channel"));
    architecture_->setAccessibleName(tr("Download architecture"));
    check_->setText(tr("Check for updates"));
    releasePage_->setText(tr("Release notes on GitHub"));
    download_->setText(tr("Download this version"));
    notes_->setPlaceholderText(tr("Release notes appear here after checking."));
    render();
}

void UpdatesWidget::check() {
    if (checking_) return;
    releases_ = {}; checked_ = false; checking_ = true;
    check_->setEnabled(false); render(); fetchPage(1);
    status_->setToolTip(QString());
}

void UpdatesWidget::fetchPage(int page) {
    QNetworkRequest request(QUrl(QString("https://api.github.com/repos/At-PetAdmin/At-Pet/releases?per_page=100&page=%1").arg(page)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "At-Pet/" AT_PET_VERSION);
    request.setTransferTimeout(15000);
    auto *reply = network_->get(request);
    connect(reply, &QNetworkReply::readyRead, reply, [reply] { if (reply->bytesAvailable() > 4 * 1024 * 1024) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, page] {
        reply->deleteLater();
        auto fail = [this](const QString &message) {
            releases_ = {}; checking_ = false; checked_ = false;
            check_->setEnabled(true); render(); status_->setText(message);
        };
        if (reply->error() != QNetworkReply::NoError) {
            fail(tr("Could not check updates. Check your connection or GitHub rate limit, then retry. You can still open the releases page."));
            status_->setToolTip(reply->errorString());
            return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isArray()) { fail(tr("Invalid release response. Please retry later.")); return; }
        const auto batch = doc.array();
        for (const auto &release : batch) releases_.append(release);
        if (batch.size() == 100) {
            if (page >= 10) { fail(tr("Too many releases. Open GitHub to view all versions.")); return; }
            fetchPage(page + 1); return;
        }
        checking_ = false; checked_ = true; check_->setEnabled(true); render();
    });
}

void UpdatesWidget::render() {
    download_->setEnabled(false);
    notes_->clear();
    if (checking_) { status_->setText(tr("Checking GitHub releases…")); return; }
    if (!checked_) { status_->setText(tr("Choose a channel, then check for updates.")); return; }
    const auto release = updates::latestRelease(releases_, channel_->currentData().toBool());
    if (release.isEmpty()) { status_->setText(tr("No published release in this channel.")); return; }
    const auto tag = release.value("tag_name").toString();
    const int comparison = updates::compareVersions(tag, QString::fromLatin1(AT_PET_VERSION));
    QString message = comparison > 0 ? tr("%1 is available.").arg(tag) : comparison == 0 ? tr("You are using %1.").arg(tag)
        : tr("Latest in this channel: %1. This is older than your current version.").arg(tag);
    const bool available = !updates::assetUrl(release, architecture_->currentData().toString()).isEmpty();
    if (!available) message += "\n" + tr("The package for this architecture is not available yet.");
    status_->setText(message);
    notes_->setPlainText(release.value("body").toString());
    download_->setEnabled(available);
}

void UpdatesWidget::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslateUi();
}
