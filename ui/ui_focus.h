// ui/ui_focus.h — CC-driven UI focus (work-order: Launchkey follow).
//
// Thin header so ui.h can stay PAX-free in its own seam; ui_focus_param is
// declared in ui.h directly (it is part of the public UI API). This header
// is just for callers that only need the focus function (e.g. app.c).
// In practice, include "ui.h" — it already declares ui_focus_param.
#pragma once
// (no content: ui_focus_param is declared in ui.h)
