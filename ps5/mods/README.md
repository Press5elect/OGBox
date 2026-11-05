# OGBox's mods

Copyright (C) 2026 Press5elect. Any use of these mods or their source, including through AI tools,
requires credit to Press5elect.

OGBox's own mods, beside hakuX's (its `mods` folder): the build copies this folder over hakuX's
into the title folder's `mods` (`ps5/tools/build.sh`), so a game's folder gets these beside
hakuX's, and a mod of the same name replaces hakuX's. This file is not copied.

The layout and the file formats are hakuX's (`mods/README.md` in hakuX's source):

```
mods/
  <TITLE ID>.<VERSION>/    the disc's game key (45410083.2)
    cheats.txt             its cheats (optional)
    <mod>/                 one mod per folder (the folder name is its id)
      mod.txt              its name, description and XBE patches
      files/               disc files it replaces or adds (optional)
```

## Game fixes (OGBox 0.0.3)

A mod whose `mod.txt` has the line `gamefix: true` is a game fix: something the game needs to run
well on the PS5. It's always on, whatever `mods.txt` says. The Mods screen lists it first, under
"Game fixes", greyed with its switch on, and it can't be turned off. The release check still
applies: a game fix made for another release is left out.

## The release check (OGBox 0.0.2)

A title ID and version can stand for more than one release of a game (a USA and a European
disc). OGBox checks what it can against the disc before a mod or a cheat can be turned on:

- A mod's `old -> new` lines: the disc's XBE must hold `old` at each address. If any line's
  bytes differ, or its address is not in the XBE, the mod was made for another release: the Mods
  screen shows it greyed out (it can't be turned on), and a game starting with it on leaves it
  out whole (the boot log says which line). Write mods with `old -> new` lines: a line without
  `old` can't be checked, and a mod with none is marked "release not checked".
- `cheats.txt`: an optional line before the first cheat (hakuX skips it there),

  ```
  crc: 1A2B3C4D
  ```

  the CRC-32 of the `default.xbe` the cheats' addresses are for (several, space-separated, for
  releases that share them; the boot log and the Mods screen's details show a disc's). On
  another release the cheats are greyed out and left out at the start. Without it they are
  marked "release not checked".

## The mods

- `45410083.2/60fps-test`: BLACK (USA), the 60 fps test without injected code (Press5elect).
