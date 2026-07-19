#!/usr/bin/env python3
"""Generate a coverage badge (badge.svg) and machine-readable summary.json
from an lcov ``.info`` file. Intended to run in CI right after the
``coverage`` CMake target, before the report is uploaded / deployed to Pages.

Usage::

    python3 coverage_badge.py <coverage.info> <out_dir>

Writes ``<out_dir>/badge.svg`` (flat-square shields-style, colored by total
line-coverage threshold) and ``<out_dir>/summary.json``.
"""
import json
import os
import sys


def parse_lcov(path):
    """Sum LF (instrumented lines) and LH (hit lines) across all records."""
    lf = lh = 0
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("LF:"):
                try:
                    lf += int(line[3:].strip())
                except ValueError:
                    pass
            elif line.startswith("LH:"):
                try:
                    lh += int(line[3:].strip())
                except ValueError:
                    pass
    return lh, lf


def color_for(pct):
    if pct >= 90:
        return "#4c1"       # brightgreen
    if pct >= 80:
        return "#97ca00"    # green
    if pct >= 70:
        return "#dfb317"    # yellow
    if pct >= 60:
        return "#fe7d37"    # orange
    return "#e05d44"        # red


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def badge_svg(label, value, color):
    # Rough monospace width estimate so the badge auto-fits the text.
    label_w = max(10, len(label) * 6 + 11)
    value_w = max(10, len(value) * 7 + 11)
    total = label_w + value_w
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{total}" height="20" '
        f'role="img" aria-label="{esc(label)}: {esc(value)}">'
        f'<linearGradient id="s" x2="0" y2="100%">'
        f'<stop offset="0" stop-color="#bbb" stop-opacity=".1"/>'
        f'<stop offset="1" stop-opacity=".1"/></linearGradient>'
        f'<clipPath id="r"><rect width="{total}" height="20" rx="3" fill="#fff"/></clipPath>'
        f'<g clip-path="url(#r)">'
        f'<rect width="{total}" height="20" fill="#555"/>'
        f'<rect x="{label_w}" width="{value_w}" height="20" fill="{color}"/>'
        f'<rect width="{total}" height="20" fill="url(#s)"/></g>'
        f'<g fill="#fff" text-anchor="middle" '
        f'font-family="Verdana,DejaVu Sans,Geneva,sans-serif" font-size="11" '
        f'text-rendering="geometricPrecision">'
        f'<text x="{label_w // 2}" y="15" fill="#010101" fill-opacity=".3">{esc(label)}</text>'
        f'<text x="{label_w // 2}" y="14">{esc(label)}</text>'
        f'<text x="{label_w + value_w // 2}" y="15" fill="#010101" fill-opacity=".3">{esc(value)}</text>'
        f'<text x="{label_w + value_w // 2}" y="14">{esc(value)}</text>'
        f'</g></svg>'
    )


def main():
    if len(sys.argv) != 3:
        print("usage: coverage_badge.py <coverage.info> <out_dir>", file=sys.stderr)
        sys.exit(2)
    info_path, out_dir = sys.argv[1], sys.argv[2]
    lh, lf = parse_lcov(info_path)
    pct = 100.0 * lh / lf if lf else 0.0
    pct_str = f"{pct:.1f}%".replace(",", ".")  # force dot decimal regardless of locale
    color = color_for(pct)

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "badge.svg"), "w", encoding="utf-8") as f:
        f.write(badge_svg("coverage", pct_str, color))
    with open(os.path.join(out_dir, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(
            {"coverage": pct_str, "percent": round(pct, 1),
             "lines_hit": lh, "lines_total": lf},
            f, indent=2,
        )
    print(f"coverage {pct_str} ({lh}/{lf} lines) -> {os.path.join(out_dir, 'badge.svg')}")


if __name__ == "__main__":
    main()
