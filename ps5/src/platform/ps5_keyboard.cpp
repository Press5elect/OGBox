// OGBox - a USB keyboard, through the console's keyboard module.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.0.3: the keyboard part of Crux2 v1.0.14's ps5/frontend/ps5_keyboard.cpp (Crux2 by
// Press5elect), for the on-screen keyboard's text fields; the key events Crux2 passes to its
// input manager are left out, the typed text kept. libSceKeyboard is system module 0x0106: loaded
// with sceSysmoduleLoadModule while the title runs, its three functions looked up by name, then by
// NID (the title does not import them: a title importing what the console doesn't give it would
// not start). Read on the frame's thread, only while text is typed; a keyboard plugged in later is
// opened when it comes (tried every two seconds), and after three failed lookups it is given up,
// with one line in the boot log.
//
// Imports this adds (all in the SDK's stubs): libSceSysmodule's sceSysmoduleLoadModule and
// sceSysmoduleGetModuleHandleInternal.

#include "ps5_keyboard.h"

#include "ps5_keyboard_logic.h"
#include "ps5_modules.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

extern "C" {
int sceSysmoduleLoadModule(uint16_t id);
int sceSysmoduleGetModuleHandleInternal(uint32_t id, int32_t* handle);
int sceKernelDlsym(int32_t handle, const char* symbol, void** address);
int sceUserServiceGetInitialUser(int32_t* user_id);
void ps5px_log(const char* format, ...);
}

namespace PS5Keyboard
{
	namespace
	{
		constexpr uint16_t KEYBOARD_MODULE = 0x0106;
		constexpr int RESOLVE_TRIES = 3;
		constexpr double RETRY_SECONDS = 2.0;

		using InitFn = int (*)();
		using OpenFn = int (*)(int32_t user, int32_t type, int32_t index, void* param);
		using ReadStateFn = int (*)(int32_t handle, void* state);

		struct State
		{
			bool resolved = false, given_up = false, open_logged = false, read_logged = false, connected = false;
			int resolve_tries = 0;
			InitFn init = nullptr;
			OpenFn open = nullptr;
			ReadStateFn read_state = nullptr;
			int32_t handle = -1;
			double next_try = 0.0;
			PS5KeyboardLogic::Tracker tracker;
		};

		State& S()
		{
			static State s;
			return s;
		}

		double Now()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		void* Find(const char* function, const char* nid, int* load_result)
		{
			const int loaded = sceSysmoduleLoadModule(KEYBOARD_MODULE);
			if (load_result)
				*load_result = loaded;
			int32_t handle = -1;
			if (sceSysmoduleGetModuleHandleInternal(KEYBOARD_MODULE, &handle) != 0 || handle < 0)
			{
				handle = -1;
				if (sceSysmoduleGetModuleHandleInternal(0x80000000u | KEYBOARD_MODULE, &handle) != 0)
					handle = -1;
			}
			void* address = nullptr;
			if (handle >= 0 && sceKernelDlsym(handle, function, &address) == 0 && address)
				return address;
			address = nullptr;
			if (handle >= 0 && sceKernelDlsym(handle, nid, &address) == 0 && address)
				return address;
			return PS5SystemModules::FindFunction("libSceKeyboard", function, nid);
		}

		bool Resolve(State& s)
		{
			int loaded = 0;
			s.init = reinterpret_cast<InitFn>(Find("sceKeyboardInit", "wadT3QBCGY0", &loaded));
			s.open = reinterpret_cast<OpenFn>(Find("sceKeyboardOpen", "HJ+KnEHcaxI", nullptr));
			s.read_state = reinterpret_cast<ReadStateFn>(Find("sceKeyboardReadState", "6HpE68bzX6M", nullptr));
			if (s.init && s.open && s.read_state)
			{
				const int init = s.init();
				ps5px_log("keyboard: module 0x%04x ready (load 0x%x), init 0x%x", KEYBOARD_MODULE, static_cast<unsigned>(loaded),
					static_cast<unsigned>(init));
				return true;
			}
			if (++s.resolve_tries >= RESOLVE_TRIES)
			{
				s.given_up = true;
				ps5px_log("keyboard: the console's keyboard module cannot be used (load 0x%x; init %s, open %s, read %s): no USB keyboard",
					static_cast<unsigned>(loaded), s.init ? "found" : "missing", s.open ? "found" : "missing",
					s.read_state ? "found" : "missing");
			}
			return false;
		}

		void TryOpen(State& s)
		{
			int32_t user = -1;
			if (sceUserServiceGetInitialUser(&user) < 0 || user < 0)
				return;
			uint8_t param[8] = {};
			const int handle = s.open(user, 0, 0, param);
			if (handle >= 0)
			{
				s.handle = handle;
				ps5px_log("keyboard: opened for user 0x%x, handle %d", static_cast<unsigned>(user), handle);
			}
			else if (!s.open_logged)
			{
				s.open_logged = true;
				ps5px_log("keyboard: none to open yet (0x%x); tried again every %.0f s", static_cast<unsigned>(handle), RETRY_SECONDS);
			}
		}

		std::string Text(const PS5KeyboardLogic::Typed& t)
		{
			std::string out;
			for (int i = 0; i < t.backspace; i++)
				out += '\b';
			for (int i = 0; i < t.left; i++)
				out += '\x02';
			for (int i = 0; i < t.right; i++)
				out += '\x03';
			out += t.text;
			if (t.escape)
				out += '\x1b';
			else if (t.enter)
				out += '\n';
			return out;
		}
	} // namespace

	std::string Poll()
	{
		State& s = S();
		if (s.given_up)
			return std::string();
		if (!s.resolved || s.handle < 0)
		{
			const double now = Now();
			if (now < s.next_try)
				return std::string();
			s.next_try = now + RETRY_SECONDS;
			if (!s.resolved)
				s.resolved = Resolve(s);
			if (s.resolved && s.handle < 0)
				TryOpen(s);
			if (s.handle < 0)
				return std::string();
		}
		std::array<uint8_t, PS5KeyboardLogic::STATE_SIZE> state = {};
		PS5KeyboardLogic::Report report;
		const int rc = s.read_state(s.handle, state.data());
		if (rc == 0)
			report = PS5KeyboardLogic::ParseState(state.data(), state.size());
		else if (!s.read_logged)
		{
			s.read_logged = true;
			ps5px_log("keyboard: reading failed (0x%x)", static_cast<unsigned>(rc));
		}
		s.connected = report.connected;
		std::vector<PS5KeyboardLogic::KeyEvent> events;
		PS5KeyboardLogic::Typed typed;
		s.tracker.Take(report, Now(), &events, &typed);
		return Text(typed);
	}

	bool IsConnected()
	{
		return S().connected;
	}
} // namespace PS5Keyboard
