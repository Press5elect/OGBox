/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.7: the display's mode (120 Hz, variable refresh), asked of the driver before the display
 * opens, as Crux2 does it (ps5/frontend/ps5_host.cpp; PS5_Mesa patches 0015-0017).
 */
#ifndef OGBOX_DISPLAY_H
#define OGBOX_DISPLAY_H

namespace ogbox {
/* Reads the settings ("output_refresh", "vrr") and sets the driver's switches. Call after
 * platform_init and before Vulkan is initialised; a change needs a restart of the title. */
void configureDisplay();
}

#endif
