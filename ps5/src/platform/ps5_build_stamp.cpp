// SPDX-FileCopyrightText: 2026 Press5elect
// SPDX-License-Identifier: GPL-3.0-or-later
// Additional term (GPL-3.0 section 7(b)): keep the attribution "Crux2 by Press5elect"; see NOTICE.
// Crux2 - which build this is (tools/build.sh writes both strings before each build),
// for the first lines of the boot log: a log then says which eboot.bin wrote it, and which ELF
// its crash addresses belong to.
//
// Kelvin: from Crux2 v1.0.15 (github.com/Press5elect/Crux2 228d0a5, ps5/frontend/ps5_build_stamp.cpp).
// Changed for Kelvin: the strings come from ps5_build_identity.h, which ps5/tools/build-identity.sh
// writes at every build (CMake target pshakux_identity), console and host reference build alike;
// the build number is Kelvin's revision. Kelvin by Press5elect.
//
// OGBox: from Kelvin 3061bf1; adds hakuX's revision and the CPU backend. OGBox by Press5elect.

#include "ps5_bootlog.h"

#include "ps5_build_identity.h"

extern "C" const char* pshakux_build_stamp(void)
{
  return PSHAKUX_BUILD_STAMP;
}

extern "C" const char* pshakux_build_identity(void)
{
  return PSHAKUX_BUILD_IDENTITY;
}

extern "C" const char* pshakux_build_version(void)
{
  return PSHAKUX_BUILD_VERSION;
}

extern "C" const char* pshakux_build_revision(void)
{
  return PSHAKUX_BUILD_REVISION;
}

extern "C" int pshakux_build_dirty(void)
{
  return PSHAKUX_BUILD_DIRTY;
}

extern "C" const char* pshakux_build_radv(void)
{
  return PSHAKUX_BUILD_RADV;
}

extern "C" const char* pshakux_build_sdk(void)
{
  return PSHAKUX_BUILD_SDK;
}

extern "C" const char* pshakux_build_hakux(void)
{
  return PSHAKUX_BUILD_HAKUX;
}

extern "C" const char* pshakux_build_cpu(void)
{
  return PSHAKUX_BUILD_CPU;
}
