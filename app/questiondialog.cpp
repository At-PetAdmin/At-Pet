#include "questiondialog.h"

#include "theme.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

QuestionDialog::QuestionDialog(const QString &requestId, const QString &agentApp,
                               const QString &header, const QString &question,
                               const QStringList &choices, QWidget *anchor)
    : QDialog(nullptr, Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint |
                         Qt::WindowSystemMenuHint),
      requestId_(requestId) {
    setWindowTitle(tr("Agent question"));
    setWindowModality(Qt::NonModal);
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(380);
    setMaximumWidth(520);
    const uitheme::Palette palette = uitheme::resolvePalette(uitheme::modeFromSettings());
    setStyleSheet(uitheme::permissionDialogStyle(palette));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    auto *heading = new QLabel(tr("%1 has a question for you").arg(agentApp), this);
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    heading->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 600;"));
    layout->addWidget(heading);

    if (!header.trimmed().isEmpty()) {
        auto *context = new QLabel(header, this);
        context->setTextFormat(Qt::PlainText);
        context->setWordWrap(true);
        context->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;")
                                   .arg(palette.textSecondary.name(QColor::HexRgb)));
        layout->addWidget(context);
    }

    auto *prompt = new QLabel(question, this);
    prompt->setTextFormat(Qt::PlainText);
    prompt->setWordWrap(true);
    prompt->setMaximumHeight(180);
    layout->addWidget(prompt);

    if (!choices.isEmpty()) {
        choiceCombo_ = new QComboBox(this);
        choiceCombo_->setEditable(true);
        choiceCombo_->setInsertPolicy(QComboBox::NoInsert);
        choiceCombo_->addItems(choices);
        choiceCombo_->setCurrentIndex(-1);
        choiceCombo_->setEditText(QString());
        choiceCombo_->setMaxVisibleItems(10);
        choiceCombo_->setToolTip(tr("Select a choice or type your own answer."));
        layout->addWidget(choiceCombo_);
        connect(choiceCombo_->lineEdit(), &QLineEdit::returnPressed, this, &QuestionDialog::submit);
    } else {
        auto *answerEdit = new QLineEdit(this);
        answerEdit->setObjectName(QStringLiteral("answerEdit"));
        answerEdit->setPlaceholderText(tr("Type your answer…"));
        answerEdit->setMaxLength(4000);
        layout->addWidget(answerEdit);
        connect(answerEdit, &QLineEdit::returnPressed, this, &QuestionDialog::submit);
    }

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancelButton = new QPushButton(tr("Cancel"), this);
    auto *submitButton = new QPushButton(tr("Send answer"), this);
    submitButton->setObjectName(QStringLiteral("allowOnceBtn"));
    submitButton->setDefault(true);
    buttons->addWidget(cancelButton);
    buttons->addWidget(submitButton);
    layout->addLayout(buttons);

    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(submitButton, &QPushButton::clicked, this, &QuestionDialog::submit);

    adjustSize();
    QScreen *screen = anchor ? anchor->screen() : QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    QPoint position = anchor && anchor->isVisible()
        ? anchor->mapToGlobal(QPoint(anchor->width() + 10, 0))
        : available.center() - QPoint(width() / 2, height() / 2);
    position.setX(qBound(available.left(), position.x(), qMax(available.left(), available.right() - width() + 1)));
    position.setY(qBound(available.top(), position.y(), qMax(available.top(), available.bottom() - height() + 1)));
    move(position);
}

void QuestionDialog::dismiss() {
    if (resolved_)
        return;
    resolved_ = true;
    close();
}

void QuestionDialog::submit() {
    if (resolved_)
        return;
    QLineEdit *answerEdit = findChild<QLineEdit *>(QStringLiteral("answerEdit"));
    const QString answer = choiceCombo_ ? choiceCombo_->currentText().trimmed()
                                         : (answerEdit ? answerEdit->text().trimmed() : QString());
    if (answer.isEmpty()) {
        if (choiceCombo_)
            choiceCombo_->setFocus();
        else if (answerEdit)
            answerEdit->setFocus();
        return;
    }
    resolved_ = true;
    emit answered(requestId_, answer);
    accept();
}

void QuestionDialog::reject() {
    if (resolved_)
        return;
    resolved_ = true;
    emit cancelled(requestId_);
    QDialog::reject();
}

void QuestionDialog::closeEvent(QCloseEvent *event) {
    if (!resolved_) {
        resolved_ = true;
        emit cancelled(requestId_);
    }
    event->accept();
}
