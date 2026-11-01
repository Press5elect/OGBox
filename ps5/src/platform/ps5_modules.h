// Crux2 - a function of a system module, found while the title runs.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0+
// Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
//
// Kelvin: the PS5SystemModules part of Crux2 v1.0.14's ps5/frontend/ps5_keyboard.h
// (github.com/Press5elect/Crux2 710d573), FindFunction only (the keyboard, LoadInternal,
// FindInHandle and FindInSystemModule are left out). Kelvin by Press5elect.

#pragma once

namespace PS5SystemModules
{
	/// A function of a module this process holds, found while the title runs: module_name is the
	/// start of the module's file name ("libScePad" matches "libScePad.sprx", not
	/// "libScePadTracker"); the function is looked up by its name, then by its NID (the console's
	/// loader names exports by NID; ps5/tools/check-imports.py derives them), then with dlsym.
	/// The process's module list shows a title only its own modules (eboot.bin, libc.prx), not
	/// the system's; the kernel's library ("libkernel") is reached by the handles the console
	/// gives it in every process (1, or 0x2001 in another kind of process). A function found in
	/// no module is said once in the boot log, with the modules the list has.
	void* FindFunction(const char* module_name, const char* function, const char* nid);
} // namespace PS5SystemModules
