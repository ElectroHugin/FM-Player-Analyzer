#pragma once

#include <QColor>

namespace fm {

// Color math for the club-colored parts of the UI (headless, testable).
// Club colors are arbitrary (white, black, neon yellow, navy …), so every
// place that puts text on a club color or a club color on the base derives
// its actual color from these helpers instead of using the raw value.

// Black or white, whichever reads better on the given fill.
QColor readableTextOn(const QColor &fill);

// The color with its lightness shifted (hue and saturation kept as far as
// possible) until it reaches `ratio` against `background`. Unchanged if it
// already does. Moves away from the background's luminance, so a dark navy
// becomes a lighter blue on a dark base and a neon yellow becomes an ochre
// on a light base.
QColor withContrast(const QColor &color, const QColor &background, double ratio);

// Linear mix: t = 0 -> a, t = 1 -> b.
QColor mixColors(const QColor &a, const QColor &b, double t);

// Colors of the club header: a tonal gradient of the club's first color (the
// second one becomes the accent stripe), darkened or lightened just enough
// that the header text (black or white) keeps at least `minContrast` against
// every point of the gradient.
struct ClubHeaderPalette {
    QColor start;  // left, behind the crest
    QColor end;    // right end of the colored block
    QColor stripe; // thin accent edge (the second club color, visible on the base)
    QColor text;   // black or white
    QColor mutedText; // text with reduced emphasis, same contrast floor (3:1)
};

ClubHeaderPalette clubHeaderPalette(const QColor &primary, const QColor &secondary,
                                    const QColor &base, double minContrast = 4.5);

} // namespace fm
