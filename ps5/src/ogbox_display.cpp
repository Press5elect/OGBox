/*
 * OGBox - hakuX for PS5
 * Copyright (C) 2026 Press5elect
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Additional terms (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
 *
 * OGBox 0.0.7: 120 Hz and variable refresh. The driver offers a 119.88 Hz mode to a title whose
 * param.json declares it, and reads that file from /app0, which a title that has left its sandbox
 * does not see; PS5_VIDEOOUT_PARAM_JSON gives it the title folder's copy. At 60 Hz it is given an
 * empty file so the output is not put into the fast mode at all (the driver does that when the
 * display opens). The idea and the switches are Crux2's (ps5/frontend/ps5_host.cpp).
 *
 * The guest stays at 60 Hz: hakuX's vblank runs every (frame period / panel period) refreshes
 * (native/dev/nv2a.c next_vblank), so a 120 Hz panel shows each real frame twice.
 */
#include "ogbox_display.h"
#include "ogbox_settings.h"
#include "platform/ps5_bootlog.h"

#include <cstdlib>
#include <string>
#include <sys/stat.h>

namespace ogbox {

void configureDisplay()
{
	Settings s;
	s.load();
	const bool fast = s.value("output_refresh") == "120";
	if (!fast) {
		setenv("PS5_VIDEOOUT_59HZ", "1", 1);
	} else if (s.on("vrr")) {
		setenv("PS5_VIDEOOUT_VRR", "1", 1);
		pshakux_log("display: variable refresh is asked for (Settings > Graphics)");
	}
	std::string file = "/dev/null";
	bool there = true;
	if (fast) {
		file = std::string(pshakux_title_dir()) + "/sce_sys/param.json";
		struct stat st;
		there = stat(file.c_str(), &st) == 0;
	}
	if (there && setenv("PS5_VIDEOOUT_PARAM_JSON", file.c_str(), 1) == 0) {
		if (fast)
			pshakux_log("display: 120 Hz is asked for (the driver reads the title's declaration from %s)", file.c_str());
		else
			pshakux_log("display: 60 Hz, as set in Settings > Graphics: 120 Hz is not asked for");
	} else {
		pshakux_log("display: 120 Hz cannot be asked for (%s): the driver looks in /app0 by itself",
			there ? "PS5_VIDEOOUT_PARAM_JSON could not be set" : "the title folder has no sce_sys/param.json");
	}
}

} // namespace ogbox
