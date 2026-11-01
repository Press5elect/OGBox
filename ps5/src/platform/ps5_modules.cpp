// Crux2 - a function of a system module, found while the title runs: the runtime of ps5_modules.h.
// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0+
// Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
//
// Kelvin: the PS5SystemModules part of Crux2 v1.0.14's ps5/frontend/ps5_keyboard.cpp
// (github.com/Press5elect/Crux2 710d573), unchanged but for what it leaves out (the keyboard,
// LoadInternal, FindInHandle, FindInSystemModule). Kelvin by Press5elect.
//
// Imports this adds (all in the SDK's stubs): libkernel's sceKernelDlsym, sceKernelGetModuleList
// and sceKernelGetModuleInfo.

#include "ps5_modules.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
int sceKernelDlsym(int32_t handle, const char* symbol, void** address);
int sceKernelGetModuleList(int32_t* handles, size_t capacity, size_t* count);
int sceKernelGetModuleInfo(int32_t handle, void* info);
void ps5px_log(const char* format, ...);
}

// ------------------------------------------------------------------------------ system modules

namespace PS5SystemModules
{
	namespace
	{
		// The module list's entry: the size of the structure first, the module's name at 8.
		constexpr size_t MODULE_INFO_SIZE = 0x160;
		constexpr size_t MODULE_NAME_OFFSET = 8;
		constexpr size_t MODULE_NAME_MAX = 256;

		bool NameMatches(const char* name, const char* wanted)
		{
			const size_t n = std::strlen(wanted);
			if (std::strncmp(name, wanted, n) != 0)
				return false;
			return name[n] == '\0' || name[n] == '.';
		}

		/// The process's modules (handles), as the console lists them.
		std::vector<int32_t> ModuleList()
		{
			std::array<int32_t, 256> handles;
			size_t count = 0;
			if (sceKernelGetModuleList(handles.data(), handles.size(), &count) != 0)
				return {};
			return std::vector<int32_t>(handles.begin(), handles.begin() + static_cast<std::ptrdiff_t>(std::min(count, handles.size())));
		}

		/// A module's name ("libkernel.sprx"), or empty.
		std::string ModuleName(int32_t handle)
		{
			alignas(8) uint8_t info[MODULE_INFO_SIZE] = {};
			const uint64_t size = sizeof(info);
			std::memcpy(info, &size, sizeof(size));
			if (sceKernelGetModuleInfo(handle, info) != 0)
				return {};
			char name[MODULE_NAME_MAX + 1] = {};
			std::memcpy(name, info + MODULE_NAME_OFFSET, MODULE_NAME_MAX);
			return name;
		}

		/// The names a module goes by in a process: the kernel's library is libkernel in one
		/// process, libkernel_sys or libkernel_web in another.
		std::vector<const char*> NamesOf(const char* module_name)
		{
			if (std::strcmp(module_name, "libkernel") == 0)
				return {"libkernel", "libkernel_sys", "libkernel_web"};
			return {module_name};
		}

		int32_t FindModule(const char* module_name)
		{
			const std::vector<int32_t> modules = ModuleList();
			for (const char* wanted : NamesOf(module_name))
			{
				for (const int32_t handle : modules)
				{
					if (NameMatches(ModuleName(handle).c_str(), wanted))
						return handle;
				}
			}
			return -1;
		}

		void* FindIn(int32_t module, const char* function, const char* nid)
		{
			if (module < 0)
				return nullptr;
			void* address = nullptr;
			if (sceKernelDlsym(module, function, &address) == 0 && address)
				return address;
			address = nullptr;
			if (nid && sceKernelDlsym(module, nid, &address) == 0 && address)
				return address;
			return dlsym(reinterpret_cast<void*>(static_cast<intptr_t>(module)), function);
		}

		std::mutex s_said_mutex;
		bool s_said_modules = false;
	} // namespace

	void* FindFunction(const char* module_name, const char* function, const char* nid)
	{
		// The module by its name(s) first; then every module of the process, since a function can be
		// given by another module than the one its documentation names.
		const int32_t named = FindModule(module_name);
		if (void* address = FindIn(named, function, nid))
			return address;
		// The kernel's library is not in the list a title sees: its fixed handles.
		if (std::strcmp(module_name, "libkernel") == 0)
		{
			for (const int32_t handle : {int32_t{1}, int32_t{0x2001}})
			{
				if (void* address = FindIn(handle, function, nid))
					return address;
			}
		}
		const std::vector<int32_t> modules = ModuleList();
		for (const int32_t handle : modules)
		{
			if (handle == named)
				continue;
			if (void* address = FindIn(handle, function, nid))
			{
				ps5px_log("system modules: %s found in %s (handle %d), not in %s", function, ModuleName(handle).c_str(), handle, module_name);
				return address;
			}
		}
		// The system's modules are not in the list a title sees, but they have handles: the small
		// numbers the loader gives in load order, and the 0x2000 range. Each is asked in turn (an
		// unused handle answers at once).
		for (int32_t handle = 0; handle < 0x2040; handle = (handle == 511) ? 0x2000 : handle + 1)
		{
			if (handle == named || handle == 1 || handle == 0x2001 ||
				std::find(modules.begin(), modules.end(), handle) != modules.end())
				continue;
			if (void* address = FindIn(handle, function, nid))
			{
				ps5px_log("system modules: %s found in the module of handle %#x", function, static_cast<unsigned>(handle));
				return address;
			}
		}
		// Not found anywhere: once, what the process has, so the boot log says where to look.
		const std::lock_guard lock(s_said_mutex);
		if (!s_said_modules)
		{
			s_said_modules = true;
			std::string names;
			for (const int32_t handle : modules)
			{
				names += names.empty() ? "" : ", ";
				names += ModuleName(handle);
			}
			ps5px_log("system modules: %s not found (%s was %s); the process has %zu module(s): %s", function, module_name,
				named >= 0 ? "found" : "not found", modules.size(), names.c_str());
		}
		else
		{
			ps5px_log("system modules: %s not found", function);
		}
		return nullptr;
	}
} // namespace PS5SystemModules
