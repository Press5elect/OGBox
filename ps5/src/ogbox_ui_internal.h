/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * What the menus' files share among themselves and nobody else needs (ogbox_ui.h is the API).
 */
#ifndef OGBOX_UI_INTERNAL_H
#define OGBOX_UI_INTERNAL_H

#include "ogbox_ui.h"

namespace ui {
namespace detail {

/* A rounded rectangle's outline drawn off outside it (a focus ring). */
void ring(ImDrawList *dl, ImVec2 p0, ImVec2 p1, float r, float off, ImU32 col, float t);
/* An image drawn to fill a box, cut to keep its shape, its corners rounded. */
void imageFill(ImDrawList *dl, const Tex &t, ImVec2 p0, ImVec2 p1, float r, ImU32 col = IM_COL32_WHITE);
/* A diagonal fall from white to a third (a placeholder's face, drawn tinted). */
Tex grad();
/* A game's face: its cover, or a tile of its colour with its name. */
void drawCover(ImDrawList *dl, const HomeGame &g, const Tex &cover, ImVec2 p0, ImVec2 p1, float focus, float a, bool big);
/* The letter L2 / R2 jumped to, in a box at c. */
void jumpLetter(ImDrawList *dl, ImVec2 c, char letter, float a);
/* The largest size from max down to min (in steps) at which s fits w in lines at most. */
float fitSize(F f, const std::string &s, float w, float max, float min, int lines, float lineGap = 1.05f);

} // namespace detail
} // namespace ui

#endif
