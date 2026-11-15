#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QNetworkAccessManager;

namespace updates {
int compareVersions(const QString &a, const QString &b);
QJsonObject latestRelease(const QJsonArray &releases, bool prerelease);
QUrl assetUrl(const QJsonObject &release, const QString &architecture);
}

class UpdatesWidget final : public QWidget {
    Q_OBJECT
public:
    explicit UpdatesWidget(QWidget *parent = nullptr, QNetworkAccessManager *network = nullptr);
protected:
    void changeEvent(QEvent *event) override;
private:
    void retranslateUi();
    void check();
    void fetchPage(int page);
    void render();
    QNetworkAccessManager *network_;
    QComboBox *channel_;
    QComboBox *architecture_;
    QLabel *current_;
    QLabel *description_;
    QLabel *status_;
    QPushButton *check_;
    QPushButton *download_;
    QPushButton *releasePage_;
    QPlainTextEdit *notes_;
    QJsonArray releases_;
    bool checked_ = false;
    bool checking_ = false;
};
