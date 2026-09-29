#include "BusyProgressDialog.h"

#include <QCloseEvent>

namespace fm {

BusyProgressDialog::BusyProgressDialog(const QString &labelText, QWidget *parent)
    : QProgressDialog(labelText, QString(), 0, 100, parent) // empty text: no cancel button
{
    setWindowModality(Qt::WindowModal);
    setWindowFlag(Qt::WindowCloseButtonHint, false);
    // Reaching 100% must not hide the dialog: the workers report their last
    // step before the final database write.
    setAutoReset(false);
    setAutoClose(false);
    setMinimumDuration(0);
    setValue(0);
}

void BusyProgressDialog::finish()
{
    m_finished = true;
    close();
    deleteLater();
}

void BusyProgressDialog::reject()
{
    // Esc lands here; ignore it until the work is done.
    if (m_finished)
        QProgressDialog::reject();
}

void BusyProgressDialog::closeEvent(QCloseEvent *event)
{
    if (!m_finished) {
        event->ignore(); // Alt+F4 / system menu while busy
        return;
    }
    QProgressDialog::closeEvent(event);
}

} // namespace fm
