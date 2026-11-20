#include "FirstRunDialog.h"

#include "core/Version.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace fm {

FirstRunDialog::FirstRunDialog(const QString &defaultDir, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("%1 — Initial setup").arg(appName()));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        tr("Welcome! Choose the folder where the Analyzer stores its data\n"
           "(databases, settings, role definitions, backups).\n"
           "You can change the folder later in the settings."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *row = new QHBoxLayout;
    m_pathEdit = new QLineEdit(QDir::toNativeSeparators(defaultDir), this);
    auto *browse = new QPushButton(tr("Browse…"), this);
    row->addWidget(m_pathEdit, 1);
    row->addWidget(browse);
    layout->addLayout(row);

    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Choose data folder"), m_pathEdit->text());
        if (!dir.isEmpty())
            m_pathEdit->setText(QDir::toNativeSeparators(dir));
    });

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Get Started"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Quit"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString FirstRunDialog::chosenDataDir() const
{
    return QDir::fromNativeSeparators(m_pathEdit->text().trimmed());
}

} // namespace fm
