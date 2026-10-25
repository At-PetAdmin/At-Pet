#include "permissiondialog.h"

#include "theme.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

PermissionDialog::PermissionDialog(const QString &requestId, const QString &agentApp,
                                   const QString &toolName, const QString &description,
                                   bool potentiallyDangerous, QWidget *anchor)
    : QDialog(nullptr, Qt::Dialog | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint |
                         Qt::WindowSystemMenuHint),
      requestId_(requestId) {
    setWindowTitle(tr("Permission request"));
    setWindowModality(Qt::NonModal);
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(360);
    setMaximumWidth(480);
    const uitheme::Palette palette = uitheme::resolvePalette(uitheme::modeFromSettings());
    setStyleSheet(uitheme::permissionDialogStyle(palette));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(10);

    auto *heading = new QLabel(tr("%1 requests permission to use %2")
                                   .arg(agentApp, toolName), this);
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    heading->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 600;"));
    layout->addWidget(heading);

    if (potentiallyDangerous) {
        auto *warning = new QLabel(tr("Potentially dangerous action — review the details before allowing."), this);
        warning->setWordWrap(true);
        warning->setObjectName(QStringLiteral("dangerWarning"));
        warning->setStyleSheet(QStringLiteral("color: %1; background: %2; border: 1px solid %3; border-radius: 6px; padding: 8px; font-weight: 600;")
            .arg(palette.dangerText.name(QColor::HexRgb),
                 palette.dangerBg.name(QColor::HexArgb),
                 palette.dangerBorder.name(QColor::HexArgb)));
        layout->addWidget(warning);
    }

    if (!description.trimmed().isEmpty()) {
        auto *details = new QLabel(description, this);
        details->setTextFormat(Qt::PlainText);
        details->setWordWrap(true);
        details->setMaximumHeight(140);
        details->setStyleSheet(QStringLiteral("color: %1;").arg(palette.textSecondary.name(QColor::HexRgb)));
        layout->addWidget(details);
    }

    auto *scope = new QLabel(tr("Allow once applies only to this request."), this);
    scope->setWordWrap(true);
    scope->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(palette.textFaint.name(QColor::HexRgb)));
    layout->addWidget(scope);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *denyButton = new QPushButton(tr("Deny"), this);
    denyButton->setObjectName(QStringLiteral("denyBtn"));
    auto *allowButton = new QPushButton(tr("Allow once"), this);
    allowButton->setObjectName(QStringLiteral("allowOnceBtn"));
    allowButton->setDefault(true);
    buttons->addWidget(denyButton);
    buttons->addWidget(allowButton);
    layout->addLayout(buttons);

    connect(denyButton, &QPushButton::clicked, this, [this] { decide(false); });
    connect(allowButton, &QPushButton::clicked, this, [this] { decide(true); });

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

void PermissionDialog::dismiss() {
    if (decided_)
        return;
    decided_ = true;
    close();
}

void PermissionDialog::decide(bool allowOnce) {
    if (decided_)
        return;
    decided_ = true;
    emit decided(requestId_, allowOnce);
    done(allowOnce ? QDialog::Accepted : QDialog::Rejected);
}

void PermissionDialog::closeEvent(QCloseEvent *event) {
    if (!decided_) {
        decided_ = true;
        emit decided(requestId_, false);
    }
    event->accept();
}

void PermissionDialog::reject() {
    decide(false);
}
