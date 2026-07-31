#!/usr/bin/env python3
"""Measures the ritual design's two VU dials' own dB -> needle-angle tick
tables directly from brand/mocks/ritual/master-01-base.png (the design's
shipped production faceplate background, embedded here as
resources/gui/master_ritual.png - this repo's own master and the brand
mock's own master are the SAME render generation, unlike aureate's tubecomp
design which measures against an earlier generation than it ships).

Not part of the build - a one-off measurement tool. Its output (the two
`ticks` tables printed at the end) is hand-transcribed, with a citation back
to this script, into PluginEditor.cpp's vuLeftTicks/vuRightTicks tables and
tests/gui/HubNeedleTests.cpp's own copy of the left table. Re-run this
whenever the ritual master render is ever replaced.

Method
------
Each dial's own tick marks are short RADIAL dashes crossing a printed
digit's own tick position; the actual print pixels closest to the pivot
(near the dial's own baseline rail, radius ~112-122px from the pivot) are
dominated by the printed digit glyphs and (on the right/red-zone side) the
painted red overload arc, both of which are NOT the tick line itself and
would confuse a naive single-radius darkness scan across that whole band.

This script instead profiles, at each candidate angle, the MINIMUM
luminance found along the dedicated tick-mark radius band (112-122px from
the pivot - found once by a direct radius profile straight up from the
pivot, where the true "0" tick's own single, isolated dark sample sits
exactly at r=116, cleanly separated from background on both sides) within a
NARROW per-label angular search window (+/-4deg, found by the same visual
cross-check process the tubecomp sibling script uses - overlaying candidate
radial lines on a zoomed crop and reading which one tracks the printed
tick's own length axis, see this revision's own interactive development
images, analysis/verify_*.png, kept for reference/provenance). The window
being narrow enough to contain only ONE tick (never a neighbouring
major/minor tick) is what makes a simple argmin-of-darkness robust here,
exactly like the tubecomp sibling script's own per-label SEARCH_WINDOWS.

Convention: 0deg = straight up (-y from the pivot), positive = clockwise.
Matches components/needle-{left,right}.json's own bakedAngleConvention
field, so the tick table and each needle sprite's own bakedAngleDeg are
directly comparable without any sign/offset conversion.
"""

import math

import numpy as np
from PIL import Image

MASTER_PATH = "/Users/yves/Development/Audio/brand/mocks/ritual/master-01-base.png"

# components/needle-{left,right}.json pivotXInMaster/pivotYInMaster - the
# needle's own measured HUB CENTRE, not the dial's optical centre (which
# sits well above the pivot on this design - see PluginEditorLayout.h).
PIVOTS = {
    "left": (473.54, 384.0),
    "right": (926.84, 384.0),
}

# Tick-mark radius band (master px from the pivot) - see this file's own
# docstring for how this was found (a direct radius profile straight up
# from the pivot; the "0" tick's own ink sits cleanly at r=116, isolated
# from background at r=112 and r=120).
TICK_RADII = np.arange(112.0, 122.0, 0.2)

# Per-label search windows (degrees), found by the same visual overlay
# cross-check the tubecomp sibling script's own SEARCH_WINDOWS use - wide
# enough to contain the whole tick mark plus its own feathered AA edge,
# narrow enough to never capture a neighbouring (major or minor) tick.
# Identical for both dials (the two dials' own printed tick fans are close
# enough in angle that one set of windows safely brackets both - the actual
# measured centres inside each window do differ slightly per dial, which is
# exactly the real, independently-measured asymmetry this script exists to
# capture rather than assume away).
SEARCH_WINDOWS = {
    -20: (-47.0, -43.0),
    -10: (-32.0, -28.0),
    -7: (-22.0, -18.0),
    -5: (-12.0, -8.0),
    -3: (-2.0, 2.0),
    0: (12.0, 16.0),
    1: (18.0, 22.0),
    2: (26.0, 30.0),
    3: (36.0, 40.0),
}

# Background reference colour per dial (sampled once, well away from any
# tick/digit/red-arc pixel, e.g. r=95-110 at a clear angle) - used only to
# skip near-background noise below MIN_CONTRAST_DIST in darkness_at_angle().
BACKGROUND_RGB = {
    "left": np.array([212.0, 178.0, 110.0]),
    "right": np.array([210.0, 175.0, 108.0]),
}

MIN_CONTRAST_DIST = 40.0


def bilinear_sample(arr: np.ndarray, px: float, py: float) -> np.ndarray:
    x0, y0 = int(math.floor(px)), int(math.floor(py))
    fx, fy = px - x0, py - y0
    if x0 < 0 or y0 < 0 or x0 + 1 >= arr.shape[1] or y0 + 1 >= arr.shape[0]:
        return np.array([255.0, 255.0, 255.0])
    c00, c10 = arr[y0, x0], arr[y0, x0 + 1]
    c01, c11 = arr[y0 + 1, x0], arr[y0 + 1, x0 + 1]
    return (c00 * (1 - fx) + c10 * fx) * (1 - fy) + (c01 * (1 - fx) + c11 * fx) * fy


def luminance(rgb: np.ndarray) -> float:
    return 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]


def darkest_luminance_at_angle(arr: np.ndarray, pivot: tuple[float, float], deg: float) -> float:
    """The minimum luminance sample found along TICK_RADII at this angle -
    low wherever a tick line's own ink (or, in the red zone, the tick drawn
    on top of the painted arc) crosses this ray, high (background/arc-only)
    elsewhere. Using the MINIMUM (not a summed contrast, unlike the
    tubecomp sibling script) is what lets a tick be found even where it
    partially overlaps the red arc's own broad, continuously-elevated
    darkness in the +1..+3 span - see this file's own docstring."""
    rad = math.radians(deg)
    best = 255.0
    for r in TICK_RADII:
        px = pivot[0] + r * math.sin(rad)
        py = pivot[1] - r * math.cos(rad)
        v = bilinear_sample(arr, px, py)
        best = min(best, luminance(v))
    return best


def measure_tick_angle(arr: np.ndarray, pivot: tuple[float, float], lo: float, hi: float, step: float = 0.05) -> float:
    degs = np.arange(lo, hi, step)
    vals = np.array([darkest_luminance_at_angle(arr, pivot, d) for d in degs])
    return float(degs[int(np.argmin(vals))])


def main() -> None:
    img = Image.open(MASTER_PATH).convert("RGB")
    arr = np.array(img).astype(float)

    tables: dict[str, dict[int, float]] = {}

    for dial in ("left", "right"):
        pivot = PIVOTS[dial]
        print(f"Measured ritual VU dial ({dial}) dB -> angle table")
        print(f"(0deg = straight up, positive = clockwise; pivot = {pivot})\n")

        table: dict[int, float] = {}
        for db, (lo, hi) in SEARCH_WINDOWS.items():
            deg = measure_tick_angle(arr, pivot, lo, hi)
            table[db] = deg
            print(f"  {db:+3d} dB -> {deg:+7.2f} deg")

        tables[dial] = table
        print()

    for dial in ("left", "right"):
        print(f"C++ table ({dial}, PluginEditor.cpp vu{dial.capitalize()}Ticks):")
        for db in sorted(tables[dial]):
            sign = "+" if db >= 0 else ""
            print(f"    {{ {sign}{db}.0f, {tables[dial][db]:+.1f}f }},")
        print()


if __name__ == "__main__":
    main()
