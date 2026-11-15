#pragma once
#include "core/DwrsColorScheme.h"
#include <QWidget>
class QDoubleSpinBox;
class QGridLayout;
class QPushButton;
namespace fm {
// Bands (threshold for outfield and goalkeeper roles, colour), soft border
// width and a preview of one DWRS colour scheme. Any edit turns the scheme
// into unsaved custom colours (id "custom"), as in the theme colours.
class DwrsColorSchemeEditor : public QWidget {
    Q_OBJECT
public:
    explicit DwrsColorSchemeEditor(QWidget *parent = nullptr);
    void setScheme(const DwrsColorScheme &scheme);
    const DwrsColorScheme &scheme() const { return m_scheme; }
signals:
    void edited();
private:
    void rebuild();
    void beginEdit();
    void finishEdit();
    DwrsColorScheme m_scheme;
    QGridLayout *m_grid = nullptr;
    QWidget *m_rows = nullptr;
    QDoubleSpinBox *m_blend = nullptr;
    QPushButton *m_add = nullptr;
    QPushButton *m_remove = nullptr;
    QWidget *m_preview = nullptr;
    bool m_loading = false;
};
} // namespace fm
