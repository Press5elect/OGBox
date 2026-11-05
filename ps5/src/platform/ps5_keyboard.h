// OGBox - a USB keyboard for the on-screen keyboard's text fields (Crux2's, ps5_keyboard.cpp).
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
#pragma once

#include <string>

namespace PS5Keyboard
{
	/// Reads the keyboard (call it each frame while text is being typed; the console's keyboard
	/// module is loaded the first time). Returns what was typed since, as ui::Keyboard::input takes
	/// it: the text, and \b backspace, \n enter, \x1b escape, \x02 left, \x03 right.
	std::string Poll();
	/// A keyboard was there at the last read.
	bool IsConnected();
} // namespace PS5Keyboard
