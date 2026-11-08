#pragma once

#include "core/SquadBuilder.h"

#include <QColor>
#include <QHash>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>

namespace fm {

class ThemeManager;

// Full-size tactical pitch with one box per slot: player name, rating, role
// and (optionally) playing time. Port of legacy display_tactic_grid.
// Optional per-slot colors turn it into the gap-analysis heat pitch.
class TacticPitchWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TacticPitchWidget(ThemeManager &theme, QWidget *parent = nullptr);

    // team: slot -> cell; positions: slot -> role abbr;
    // layout: stratum -> slots (Definitions::tacticLayouts()[tactic]).
    void setTeam(const QHash<QString, XiCell> &team, const QHash<QString, QString> &positions,
                 const QHash<QString, QStringList> &layout,
                 const QHash<QString, QString> &roleNames, bool showApt = true);

    // Optional background override per slot (gap heat map); empty = default.
    void setSlotColors(const QHash<QString, QColor> &colors);
    // Optional second line under the name (e.g. gap reason), per slot.
    void setSlotSubtexts(const QHash<QString, QString> &subtexts);

    void clearData();

    // Combined view of a two-phase tactic (FM27): one card per player at the
    // midpoint of his slot with and without the ball, both roles on the card
    // (with ball first). Free places of either line-up get their own marker
    // at their slot, keyed "ip:<slot>" / "oop:<slot>" for
    // emptySlotContextMenuRequested. Cards that would overlap are moved
    // apart and keep a thin line to their anchor.
    struct CombinedCard {
        QString uid;
        QString name;
        QString ipSlot;
        QString oopSlot;
        double ipRating = 0.0;  // DWRS of the role with ball
        double oopRating = 0.0; // ... and without
        double value = 0.0;     // weighted value over both line-ups
    };
    void setCombinedTeam(const QList<CombinedCard> &cards, const QHash<QString, QString> &ipLineUp,
                         const QHash<QString, QStringList> &ipLayout,
                         const QHash<QString, QString> &oopLineUp,
                         const QHash<QString, QStringList> &oopLayout);
    bool isCombined() const { return m_combined; }

    // Tactic editor mode: roles: slot -> role id of the slots in use;
    // roleNames: role id -> display name. Free slots are drawn as "+" boxes
    // while showFreeSlots is true (e.g. until ten outfield players stand).
    // Every box is clickable (slotClicked).
    void setEditorSlots(const QHash<QString, QString> &roles,
                        const QHash<QString, QString> &roleNames, bool showFreeSlots);

    // Role-dependent card shift (FM27: a pressing DM stands higher than a
    // screening one): percent of the pitch length, + towards the opponent
    // goal, for (role id, slot). Set once for every pitch (MainWindow);
    // display only.
    using ShiftSource = std::function<double(const QString &role, const QString &slot)>;
    static void setShiftSource(ShiftSource source);

    // Optional small portrait per player card (graphics folder, see
    // GraphicsLibrary). Returns a null pixmap when off or missing: the card
    // then looks as before. Set once (MainWindow).
    using PortraitSource = std::function<QPixmap(const QString &uid, int size)>;
    static void setPortraitSource(PortraitSource source);

    // FM version whose pitch geometry places the slots (empty = default).
    void setFmVersionId(const QString &versionId) { m_fmVersionId = versionId; }

signals:
    // Player interactions (M13): boxes are clickable.
    void playerDoubleClicked(const QString &uid);
    void playerContextMenuRequested(const QString &uid, const QPoint &globalPos);
    // Right-click on an empty place of a team (not in editor mode).
    void emptySlotContextMenuRequested(const QString &slot, const QPoint &globalPos);
    // Editor mode: a slot (in use or free) was clicked.
    void slotClicked(const QString &slot, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool hasHeightForWidth() const override { return true; }
    // Every page centres the pitch at its size hint, so it never gets wider
    // than that — but the layout asks for the height at the full cell width.
    // Answering with the plain ratio reserved an empty band below the pitch.
    int heightForWidth(int width) const override
    {
        return qMin(width, kHintWidth) * 105 / 68;
    }
    QSize sizeHint() const override { return {kHintWidth, kHintWidth * 105 / 68}; }

private:
    static constexpr int kHintWidth = 480;

    QString uidAt(const QPoint &pos) const;
    // Vertical offset in pixels for a role on a slot (negative = up).
    double shiftPixels(const QString &role, const QString &slot, double fieldHeight) const;
    QString slotAt(const QPoint &pos) const;

    ThemeManager &m_theme;
    QHash<QString, QRectF> m_boxRects; // slot -> painted rect (for hit tests)
    QHash<QString, XiCell> m_team;
    QHash<QString, QString> m_positions;
    QHash<QString, QStringList> m_layout;
    QString m_fmVersionId;
    QHash<QString, QString> m_roleNames;
    QHash<QString, QColor> m_slotColors;
    QHash<QString, QString> m_slotSubtexts;
    bool m_showApt = true;
    bool m_editor = false;
    bool m_showFreeSlots = true;
    QHash<QString, QString> m_editorRoles; // slot -> role id
    // Combined view (setCombinedTeam).
    void paintCombined(QPainter &painter, const QRectF &field);
    bool m_combined = false;
    QList<CombinedCard> m_combinedCards;
    QHash<QString, QString> m_ipLineUp;
    QHash<QString, QStringList> m_ipLayout;
    QHash<QString, QString> m_oopLineUp;
    QHash<QString, QStringList> m_oopLayout;
    QHash<QString, QString> m_combinedUids; // box key -> uid ("" = free place)
};

} // namespace fm
