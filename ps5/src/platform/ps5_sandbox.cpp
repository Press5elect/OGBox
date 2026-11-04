// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "OGBox by Press5elect"; see NOTICE.
//
// OGBox 0.1.3 - leaving the sandbox, as Crux2 does (github.com/Press5elect/Crux2 08c3507,
// ps5/frontend/ps5_platform.cpp's EarlyInit and FindTitleFolder).
//
// A title's sandbox shows it only part of the console's file system: 0.1.0 could not open a disc
// at /data/ogbox/games/x.iso ("No such file or directory") while /data/homebrew/PPSA99251/...
// opened, and USB drives (/mnt/usb*) and extended storage (/mnt/ext*) are not there at all. The
// bundled helper (sandbox-elevator.elf, BlackBearReloaded's ps5-native-app-boilerplate, built by
// ps5/tools/build-elevator.sh) is sent to the payload loader on port 9021 (elfldr, which kstuff
// setups run); it gives this one title (PPSA99251, by its exact title ID) the console's whole file
// system. It changes the process's credentials, which is only safe while the process has one
// thread: pshakux_diag_start calls this first, before the boot log (whose watcher is a thread) is
// open, so each step is kept and written into the boot log once it is.
//
// Afterwards /app0 is gone (the process's root is the console's): the title folder is looked for
// where the title can be installed. A file named elevation-off.txt in the title folder keeps the
// title inside its sandbox.

#include "ps5_sandbox.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#if defined(__PROSPERO__)
#include "../../elevation/elevation.hpp"
#endif

namespace {

constexpr int MAX_STEPS = 24;
char s_steps[MAX_STEPS][168];
int s_step_count = 0;
int s_tried = 0;
int s_elevated = 0;
unsigned s_status = 5; // unavailable

void Keep(const char* line)
{
  if (s_step_count < MAX_STEPS)
  {
    std::snprintf(s_steps[s_step_count], sizeof(s_steps[0]), "%s", line);
    s_step_count++;
  }
}

#if defined(__PROSPERO__)
void KeepStep(void*, const char* line)
{
  char text[168];
  std::snprintf(text, sizeof(text), "  step: %s", line);
  Keep(text);
}
#endif

bool Exists(const char* path)
{
  struct stat st;
  return stat(path, &st) == 0;
}

bool IsTitleFolder(const char* directory)
{
  char probe[224];
  std::snprintf(probe, sizeof(probe), "%s/eboot.bin", directory);
  if (!Exists(probe))
    return false;
  std::snprintf(probe, sizeof(probe), "%s/sce_sys/param.json", directory);
  return Exists(probe);
}

const char* StatusText(unsigned status)
{
  switch (status)
  {
    case 0: return "ok: the console's whole file system is visible";
    case 1: return "the request was not valid";
    case 2: return "the helper speaks another protocol version";
    case 3: return "the helper does not give that capability";
    case 4: return "the helper serves another title";
    case 5: return "sandbox-elevator.elf is missing, or the kernel state is not available to the helper";
    case 6: return "the credentials could not be prepared";
    case 7: return "the helper could not apply the change";
    case 8: return "the helper could not undo a failed change";
    case 9: return "no payload loader answered on port 9021 (elfldr is not running)";
    case 10: return "the helper's answer was not understood";
    case 11: return "ok, the old way (the helper's own root was not the console's): the console's whole file system is visible, but "
                    "each exit releases two references on the root it never took, and enough of them bring the "
                    "console down";
  }
  return "unknown";
}

} // namespace

extern "C" int pshakux_sandbox_leave(char* title_dir, unsigned size)
{
#if defined(__PROSPERO__)
  if (Exists("/app0/elevation-off.txt"))
  {
    Keep("sandbox: elevation-off.txt is in the title folder: staying inside the sandbox");
    return 0;
  }
  s_tried = 1;
  Keep("sandbox: asking /app0/sandbox-elevator.elf (the payload loader, port 9021) for the console's file system");
  const elevation::Status status =
    elevation::request(elevation::Capability::filesystem, "/app0/sandbox-elevator.elf", &KeepStep, nullptr);
  s_status = static_cast<unsigned>(status);
  s_elevated = status == elevation::Status::ok || status == elevation::Status::ok_unbalanced;
  char line[200];
  std::snprintf(line, sizeof(line), "sandbox: elevation %s (status %u)%s", StatusText(s_status), s_status,
                s_elevated ? "" : "; going on inside the sandbox");
  Keep(line);

  // The title folder, under whichever name reaches it now.
  static constexpr const char* candidates[] = {
    "/app0",
    "/data/homebrew/PPSA99251",
    "/mnt/sandbox/PPSA99251_000/app0",
    "/user/app/PPSA99251",
    "/mnt/ext0/homebrew/PPSA99251",
    "/mnt/ext1/homebrew/PPSA99251",
    "/mnt/usb0/homebrew/PPSA99251",
    "/mnt/usb1/homebrew/PPSA99251",
  };
  for (const char* const candidate : candidates)
  {
    if (IsTitleFolder(candidate))
    {
      std::snprintf(title_dir, size, "%s", candidate);
      std::snprintf(line, sizeof(line), "sandbox: the title folder is %s", candidate);
      Keep(line);
      return 1;
    }
  }
  Keep("sandbox: the title folder was not found after leaving the sandbox (install it in /data/homebrew/PPSA99251, "
       "or put elevation-off.txt next to eboot.bin)");
  return 0;
#else
  (void)title_dir;
  (void)size;
  return 0;
#endif
}

extern "C" void pshakux_sandbox_log(void (*log)(const char* line))
{
  for (int i = 0; i < s_step_count; i++)
    log(s_steps[i]);
  if (s_step_count == 0)
    log("sandbox: not left (host reference build)");
}

extern "C" int pshakux_sandbox_elevated(void)
{
  return s_elevated;
}

extern "C" int pshakux_sandbox_tried(void)
{
  return s_tried;
}

#if defined(__PROSPERO__)
// The client of ps5-native-app-boilerplate (BlackBearReloaded, GPL-3.0-or-later), by way of Crux2.
#include "../../elevation/elevation.cpp"
#endif
