#include "ThemeColors.h"

#include "Utils.h"

#include <algorithm>
#include <initializer_list>

namespace fm {

namespace {

double minContrast(const QColor &text, std::initializer_list<QColor> fills)
{
    double result = 21.0;
    for (const QColor &fill : fills)
        result = std::min(result, contrastRatio(text, fill));
    return result;
}

// Lightness shifted by `delta` (HSL), clamped.
QColor shiftLightness(const QColor &color, double delta)
{
    float h = 0, s = 0, l = 0, a = 1;
    color.getHslF(&h, &s, &l, &a);
    l = std::clamp(l + static_cast<float>(delta), 0.0f, 1.0f);
    return QColor::fromHslF(h, s, l, a);
}

} // namespace

QColor readableTextOn(const QColor &fill)
{
    return contrastRatio(fill, QColorConstants::White) >= contrastRatio(fill, QColorConstants::Black)
               ? QColorConstants::White
               : QColorConstants::Black;
}

QColor withContrast(const QColor &color, const QColor &background, double ratio)
{
    if (!color.isValid() || !background.isValid())
        return color;
    if (contrastRatio(color, background) >= ratio)
        return color;
    // Away from the background: lighter on a dark base, darker on a light one.
    const bool lighten = relativeLuminance(background) < 0.18;
    const double step = lighten ? 0.01 : -0.01;
    QColor candidate = color;
    for (int i = 0; i < 100; ++i) {
        candidate = shiftLightness(candidate, step);
        if (contrastRatio(candidate, background) >= ratio)
            return candidate;
    }
    // Saturated hues can top out before the target (pure yellow on white):
    // fall back to plain black/white, which always reaches 4.5:1 on one side.
    return readableTextOn(background);
}

QColor mixColors(const QColor &a, const QColor &b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const auto mix = [t](double x, double y) { return x + (y - x) * t; };
    return QColor::fromRgbF(static_cast<float>(mix(a.redF(), b.redF())),
                            static_cast<float>(mix(a.greenF(), b.greenF())),
                            static_cast<float>(mix(a.blueF(), b.blueF())));
}

ClubHeaderPalette clubHeaderPalette(const QColor &primary, const QColor &secondary,
                                    const QColor &base, double minContrastRatio)
{
    ClubHeaderPalette palette;
    const QColor first = primary.isValid() ? primary : QColor(QStringLiteral("#6a1bf5"));
    const QColor second = secondary.isValid() ? secondary : first;

    // A tonal gradient inside the first club color (lighter -> deeper), never
    // from one hue to another: hue-to-hue gradients look muddy and generic.
    // The second club color shows as the stripe instead.
    const bool twoColors = contrastRatio(first, second) >= 1.35
                           || std::abs(first.hslHueF() - second.hslHueF()) > 0.08;
    QColor start = shiftLightness(first, 0.04);
    QColor end = shiftLightness(first, -0.08);
    // A white or very pale club color would glare in the dark chrome: on a
    // dark base it is toned down to a soft light grey of the same hue.
    if (relativeLuminance(base) < 0.18) {
        const auto tame = [](QColor color) {
            while (relativeLuminance(color) > 0.62)
                color = shiftLightness(color, -0.02);
            return color;
        };
        start = tame(start);
        end = tame(end);
    }

    // One text color for the whole block: the one that reads better on the
    // worse of both ends. Then pull both ends away from it until the floor
    // holds everywhere (the gradient is monotonic in between).
    const QColor white = QColorConstants::White;
    const QColor black = QColorConstants::Black;
    const QColor mid = mixColors(start, end, 0.5);
    palette.text = minContrast(white, {start, mid, end}) >= minContrast(black, {start, mid, end})
                       ? white
                       : black;
    start = withContrast(start, palette.text, minContrastRatio);
    end = withContrast(end, palette.text, minContrastRatio);
    palette.start = start;
    palette.end = end;

    // Secondary text (stadium, breadcrumb on the block): softer, 3:1 minimum.
    QColor muted = palette.text;
    for (double t = 0.35; t >= 0.0; t -= 0.05) {
        muted = mixColors(palette.text, mixColors(start, end, 0.5), t);
        if (minContrast(muted, {start, end}) >= 3.0)
            break;
    }
    palette.mutedText = muted;

    // The accent edge between the block and the neutral rest of the bar: the
    // second club color, made visible against the base where it meets it.
    const QColor stripe = twoColors ? second : shiftLightness(first, 0.18);
    palette.stripe = withContrast(stripe, base, 1.8);
    return palette;
}

} // namespace fm
