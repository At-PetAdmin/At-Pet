#pragma once

#include <QDialog>
#include <QString>

class QCloseEvent;

class PermissionDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PermissionDialog(const QString &requestId, const QString &agentApp,
                              const QString &toolName, const QString &description,
                              bool potentiallyDangerous, QWidget *anchor = nullptr);
    void dismiss();

signals:
    void decided(const QString &requestId, bool allowOnce);

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    void decide(bool allowOnce);

    QString requestId_;
    bool decided_ = false;
};
