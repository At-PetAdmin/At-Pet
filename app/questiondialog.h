#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QCloseEvent;
class QComboBox;
class QWidget;

class QuestionDialog final : public QDialog {
    Q_OBJECT

public:
    explicit QuestionDialog(const QString &requestId, const QString &agentApp,
                            const QString &header, const QString &question,
                            const QStringList &choices, QWidget *anchor = nullptr);
    void dismiss();

signals:
    void answered(const QString &requestId, const QString &answer);
    void cancelled(const QString &requestId);

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    void submit();

    QString requestId_;
    QComboBox *choiceCombo_ = nullptr;
    bool resolved_ = false;
};
