#pragma once

#include <QWidget>

namespace fm {

// Left and right foot as plain facts, the way the game shows them: a small
// caption, the strength in words and a six-segment bar. No judgement about
// positions or roles. A foot without data is left out; the widget hides
// itself when neither foot is known.
class FootStrengthWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FootStrengthWidget(QWidget *parent = nullptr);

    // Texts as stored on the player ("Very Strong", "Weak", ...).
    void setFeet(const QString &leftFoot, const QString &rightFoot);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

    // Localized name of a level 1-6 (empty otherwise).
    static QString levelName(int level);

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct Foot {
        QString caption;
        QString valueText; // localized level name, or the raw text if unknown
        int level = 0;     // 0 = unknown level (text only, empty bar)
    };
    void rebuild();

    QString m_leftRaw;
    QString m_rightRaw;
    QList<Foot> m_feet;
};

} // namespace fm
