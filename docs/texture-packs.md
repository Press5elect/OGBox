# HD texture packs

OGBox can save the textures a game uses and draw your own in their place (1.0.2). Both switches
are in **Settings > Picture > Textures**, and each can be set for one game (the game's page >
Game settings).

| Setting | Default | What it does |
| --- | --- | --- |
| **HD texture packs** | On | Replaces textures with the files in the game's texture folder |
| **Dump textures** | Off | Saves each texture the game uses, once, into the folder's `dump` |

## Where the files go

```
<data folder>/textures/<title ID>/          your replacements (any subfolders)
<data folder>/textures/<title ID>/dump/     what Dump textures writes; never read as replacements
```

The data folder is `/data/ogbox` unless you chose another (Settings > Data folder); the `textures`
folder moves with it. The title ID is the eight characters the game's page shows (`4D530039`).

## Making a pack

1. Turn **Dump textures** on for the game and play through what you want to change. Each texture is
   written once, on a thread of its own; textures already in `dump` are not written again.
   - Compressed textures (DXT1, DXT3, DXT5) are saved as `.dds` with the game's own data and every
     mip level.
   - Everything else is saved as `.png`, as the game shows it (palettes and 16-bit formats expanded).
   - The names are `tex_<code>_<width>x<height>_<format>`. The 16-digit code identifies the texture:
     it is computed from the texture's contents (every mip level), its size, its format and its
     mip level count, and a palette texture's palette. It is the same on every console and in
     every run.
2. Edit or upscale a dumped file. A replacement must be **1 to 8 times** the original's size (2x,
   4x, 8x...) with the same shape; another size is refused.
3. Put it in the game's texture folder (not in `dump`), in any subfolder, under any name that still
   contains `tex_<code>`: `tex_25dec892e2392bb1_wall_hd.png` works.
4. Start the game with **HD texture packs** on.

| Replacement | Limits | Mip levels |
| --- | --- | --- |
| PNG (RGBA) | up to 2048 x 2048 | made from the picture (a box filter) |
| DDS, DXT1 / DXT3 / DXT5 (or DX10's BC1 to BC3) | its mip chain up to 24 MiB (a 4096 x 4096 DXT5 fits) | the file's own |

If a texture has several files, the newest wins (a DDS over a PNG of the same time). A PNG can
replace a compressed texture and the other way round.

## What is and isn't covered

Dumped and replaceable: the game's ordinary 2D textures, 4 x 4 and larger, in colour formats.

Not covered (the game's own texture is drawn): textures the game renders into, cube maps and
volume textures, linear-layout textures (mostly movie frames and screen effects), bump-map and
depth formats.

## Checking that it worked

The boot log (Settings > Advanced > Boot log, or **Save the Log** in the quick menu) says:

- `texture replacements: N in <folder> (P PNG, D DDS)` at the game's start;
- `replacement <code>: <file>, WxH (Nx), L levels` for the first few used;
- `replacement <file>: not used: <reason>` for each one refused.

A replacement is sent to the GPU once and shared by every texture with its code; up to 1 GiB of
replacements stay on the GPU, the least recently used going first.

## How it works (for developers)

`hw/xbox/hle/tex/texpack.c` in hakux-native, hooked into the NV2A texture upload
(`tex_define`, `hw/xbox/hle/pb/pbres.c`); the GPU side (one shared image a replacement) is in
`hw/xbox/hle/vk/draw.c`. OGBox passes the folders as `NC_HLE_TEXPACK_DIR` and
`NC_HLE_TEXDUMP_DIR` (`ps5/src/ogbox_main.cpp`) and registers the PNG codec (stb, in
`ps5/src/ogbox_library.cpp`).
