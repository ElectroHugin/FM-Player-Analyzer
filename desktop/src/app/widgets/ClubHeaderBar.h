#pragma once

#include <QFrame>
#include <QPixmap>

class QLabel;

namespace fm {

class ThemeManager;

// The club header above every page: crest, club name and stadium on a block
// in the club's colors, then navigation, search and import on the neutral
// rest of the bar.
//
// "gradient" (default): the block runs from the first to the second club
// color and ends in a slanted edge with a thin stripe in the second color,
// like a kit sash; optionally a faint, oversized crest silhouette sits in the
// block. Text color and both gradient ends come from clubHeaderPalette(), so
// name and stadium keep 4.5:1 / 3:1 whatever the club colors are; controls
// sit on the neutral part and use the normal theme colors.
// "solid": the whole bar in the header color (look up to 2.0.x).
class ClubHeaderBar : public QFrame
{
    Q_OBJECT

public:
    explicit ClubHeaderBar(ThemeManager &theme, QWidget *parent = nullptr);

    // Space the layout must leave after identityWidget() for slant + stripe.
    static constexpr int kBlockTail = 46;

    // Crest + name + subtitle; the caller puts it first in the bar's layout.
    QWidget *identityWidget() const { return m_identity; }

    // crest: empty pixmap -> monogram badge from the title's initials.
    void setIdentity(const QPixmap &crest, const QString &title, const QString &subtitle);

    // Renders the block of the current theme into `size` (settings preview).
    static QPixmap renderPreview(const ThemeManager &theme, const QSize &size,
                                 const QPixmap &crest, const QString &title,
                                 const QString &subtitle, qreal dpr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void applyColors();
    static QString initials(const QString &title);

    ThemeManager &m_theme;
    QWidget *m_identity = nullptr;
    QLabel *m_crest = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_subtitle = nullptr;
    QPixmap m_crestPixmap;
    QString m_titleText;
};

} // namespace fm
