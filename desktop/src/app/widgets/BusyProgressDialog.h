#pragma once

#include <QProgressDialog>

namespace fm {

// Window-modal progress dialog for background work that must not be dismissed
// while it runs. A plain QProgressDialog without a cancel button still closes
// on Esc (QDialog::reject) and via the title-bar X, and by default hides itself
// as soon as the value reaches the maximum — each of those re-enables the main
// window while the worker is still using the database and the engines. This
// one stays up until the completion handler calls finish().
class BusyProgressDialog : public QProgressDialog
{
public:
    BusyProgressDialog(const QString &labelText, QWidget *parent);

    // Ends the busy state, closes the dialog and schedules its deletion.
    void finish();
    bool isFinished() const { return m_finished; }

    void reject() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    bool m_finished = false;
};

} // namespace fm
