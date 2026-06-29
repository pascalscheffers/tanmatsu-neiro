// ui/ui_icons.cpp — badge button shape icon drawing (WO-8).
//
// Draws the six Tanmatsu badge button shapes as PAX vector outlines.
// No engine or model knowledge — pure rendering (ADR 0008).
#include "ui_icons.h"
#include "pax_gfx.h"

// All shapes are stroked / outline so they read at ~12–14px hint-strip size.
//   r = outer radius for shapes that use a single circle (circle, diamond, trilobe)
//   h = half-side for shapes sized from the centre (cross, square)
//   The constants below express r and h as fractions of `size`.

extern "C" void ui_icon_draw(pax_buf_t* fb, UiIconShape shape, float cx, float cy, float size, uint32_t color) {
    float r = size * 0.45f;  // outer radius / half-diagonal
    float h = size * 0.40f;  // half-side (cross arm length, square half-side)

    switch (shape) {
        case UI_ICON_CROSS:
            // Two diagonal lines through the centre.
            pax_draw_line(fb, color, cx - h, cy - h, cx + h, cy + h);
            pax_draw_line(fb, color, cx - h, cy + h, cx + h, cy - h);
            break;

        case UI_ICON_TRIANGLE:
            // Apex-up equilateral-ish triangle.
            pax_outline_tri(fb, color, cx, cy - r, cx - r, cy + r, cx + r, cy + r);
            break;

        case UI_ICON_SQUARE:
            // Axis-aligned outline square.
            pax_outline_rect(fb, color, cx - h, cy - h, 2.0f * h, 2.0f * h);
            break;

        case UI_ICON_CIRCLE:
            // Simple outline circle.
            pax_outline_circle(fb, color, cx, cy, r);
            break;

        case UI_ICON_TRILOBE: {
            // Three overlapping circles arranged in a trefoil / clover pattern.
            // One lobe points up, two point down-left and down-right.
            float lr = size * 0.26f;  // lobe radius
            float dy = size * 0.20f;  // top lobe: cy offset upward
            float dx = size * 0.22f;  // bottom lobes: cx offset
            float by = size * 0.14f;  // bottom lobes: cy offset downward
            pax_outline_circle(fb, color, cx, cy - dy, lr);
            pax_outline_circle(fb, color, cx - dx, cy + by, lr);
            pax_outline_circle(fb, color, cx + dx, cy + by, lr);
            break;
        }

        case UI_ICON_DIAMOND:
            // Four lines connecting the cardinal points (rotated square / rhombus).
            pax_draw_line(fb, color, cx, cy - r, cx + r, cy);
            pax_draw_line(fb, color, cx + r, cy, cx, cy + r);
            pax_draw_line(fb, color, cx, cy + r, cx - r, cy);
            pax_draw_line(fb, color, cx - r, cy, cx, cy - r);
            break;

        default:
            break;
    }
}
