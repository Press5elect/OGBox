# Front-end fix plan

The plan for the defects two code reviews of `ps5/src` and the PS5 parts of `ps5/hakux` found after
1.0.2 (110 findings). Nothing here is built yet. A row leaves this file when its fix is on main; a row
that turns out wrong is deleted with a line in `open-work.md`'s decisions.

## Rules for this work

- **State** says how far a finding is proven: *checked* (re-read in the code by the orchestrating
  session), *traced* (a reviewer followed the whole path), *plausible* (depends on something not
  verified). A plausible row is proven or dropped before any code changes for it.
- One batch is one test build with one question. A batch that shows a regression on the console is
  taken out again.
- Where a PC test can show the defect, the test is written first and fails before the fix.
- No behaviour changes beyond the defect: no redesigns, no new settings.
- The line numbers are 1.0.2's (`3abbec1`).

## Order

| Batch | What | Why this place |
| --- | --- | --- |
| 1 | Crashes and memory safety | Anyone can hit them; small, local fixes |
| 2 | The data folder | The owner's reported fault (a move stops at its last file) |
| 3 | Losing data: saves, settings, states, shares, the updater | Silent loss |
| 4 | Stuck and frozen screens | |
| 5 | Menus and wrong behaviour | |
| 6 | The network file layer | Needs a share to test |
| 7 | Mods, patches, trainers | |
| 8 | Diagnostics, low and latent | Last; no effect on play |

## Batch 1: crashes and memory safety

Test build question: *does everything still start and play as in 1.0.2?* (the faults themselves are
shown on the PC).

| # | Where | Defect | Fix | State |
| --- | --- | --- | --- | --- |
| 1.1 | `ogbox_main.cpp:1635-1674` | `collectListing` unlocks a mutex of a listing it has just reset | Keep the shared pointer alive to the end of the scope | checked |
| 1.2 | `core/ogbox_patches.c:1011-1050` | `undo[8]`/`saved[8]` overflow on a ninth entry | Bound the count; refuse the patch with a log line | checked |
| 1.3 | `ogbox_ui_home.cpp:328-336` | `home.scroll` is never reset: a shorter folder gives a negative vector size (throws) | Clamp scroll to the list on every change of list | checked |
| 1.4 | `hakux/hle/present_host.c:331-347` | After a 2 s fence timeout the image, command buffer and fence are reused while the GPU has them | Wait until done (log each timeout); no reuse before the fence signals | checked |
| 1.5 | `present_host.c:289` | The display image is taken before the lock against a display re-init | Take it under the lock | plausible |
| 1.6 | `ogbox_main.cpp:3422` (`ensurePost`) | A descriptor to a destroyed image after a picture-size change | Rewrite the descriptor whenever the image is made again | plausible |
| 1.7 | `core/ogbox_mods.c:~1347`, `core/ogbox_patches.c:~812` | XBE header offsets wrap in 32 bits | Check in 64 bits against the file size | traced |
| 1.8 | `ogbox_ui_settings.cpp:~705` | The keyboard's cursor can pass the text's end | Clamp after every edit | traced |
| 1.9 | `ogbox_main.cpp:8498` | A What's New line of only spaces | Treat as empty | traced |
| 1.10 | `ogbox_library.cpp` (chunk parser) | A chunk length wraps | Check against the bytes left | traced |
| 1.11 | `ogbox_ui_screens.cpp:209` | `ms[32]` is one byte short (label cut) | Size the buffer for the text | checked |
| 1.12 | `main.cpp:124` | A garbled `test-run.txt` throws at every start (`stof`) | Parse without exceptions | traced |
| 1.13 | `platform/vfs/ps5_vfs.cpp:223, 1263` | A handle freed by a close while another thread reads through it | Reference-count the handle across the read | plausible |
| 1.14 | `platform/vfs/ps5_vfs_ftp.cpp:118, 599` | Socket writes can raise SIGPIPE | `MSG_NOSIGNAL` / `SO_NOSIGPIPE` | plausible |
| 1.15 | `ogbox_ui.cpp:394` | Text past a paragraph's last line is drawn below it | Stop at the last allowed line | traced |

PC checks: unit tests for 1.2, 1.7, 1.10, 1.12; a list-size test for 1.3; `tests/run-pc-tests.sh`.

## Batch 2: the data folder

Test build question: *does a move of the data folder finish, and does everything use the new folder
without a restart?*

| # | Where | Defect | Fix | State |
| --- | --- | --- | --- | --- |
| 2.1 | `ogbox_storage.cpp:413-480` | After the last file the move deletes the copied sources with no progress: the screen stands at the last file (the owner's fault, in the code; not seen in a log) | A "removing the old copy" phase with its own progress; the job reports done only after it | traced |
| 2.2 | `ogbox_main.cpp:2126` | `boot.hdd` is set once: after a move or "Start there empty" the hard disk manager, sync, imports and deletes use the old folder | Derive it from the storage root at each use, or set it again when the root changes | checked |
| 2.3 | `ogbox_storage.cpp:161` | The note of the new root is written unchecked, then the old data is deleted | Write to a temporary file, sync, rename, check; delete nothing unless it succeeded | checked |
| 2.4 | `ogbox_storage.cpp` (StorageMove) | A target inside the source isn't refused | Refuse, by resolved paths | traced |
| 2.5 | `ogbox_main.cpp:5468` | A move can start during a save sync | Refuse while a sync or another job runs (and the reverse) | plausible |
| 2.6 | `ogbox_storage.cpp:361` | Symlinks are skipped by the copy, then deleted with the source | Copy them as links, or keep the source when any entry was skipped | plausible |
| 2.7 | `ogbox_storage.cpp:377-381` | A pending copy is redone without a look at what is there | Compare size and date before redoing | traced |

PC checks: a move test in a temporary tree (last file, target inside source, failed note write, a
symlink, an interrupted move resumed).

## Batch 3: losing data

Test build question: *do saves, save states, settings and synced saves behave as before?*

| # | Where | Defect | Fix | State |
| --- | --- | --- | --- | --- |
| 3.1 | `ogbox_saves.cpp:483-498` | `hddImportSave` removes the save before the copy | Copy beside it, then swap | checked |
| 3.2 | `ogbox_save_sync.cpp:71` | `collect()` takes a failed listing as "nothing there" | Carry the failure; no deletes from a failed listing | checked |
| 3.3 | `ogbox_save_sync.cpp` (`sigAt`) | A failed listing counts as "not there" | As 3.2 | traced |
| 3.4 | `ogbox_save_sync.cpp` | Whole files are read into memory | Stream in blocks | traced |
| 3.5 | `ogbox_settings.cpp:585-592` | The settings write is unchecked | Temporary file, check, rename | checked |
| 3.6 | `ogbox_main.cpp:6078` | "Close OGBox" after an update skips the settings flush | Flush first | traced |
| 3.7 | `ogbox_main.cpp:5910, 5829` | A failed save-state rename is reported as "Saved"; the picture can be the previous state's | Check the rename; write the picture with the state | traced |
| 3.8 | `ogbox_main.cpp:587` (`writeBmp`) | Unchecked writes | Check; remove a truncated file | traced |
| 3.9 | `ogbox_main.cpp:6399` | Quit-and-save with an empty run key | Refuse with a notice | traced |
| 3.10 | `ogbox_main.cpp:7065` | "Bring them all in" ignores failures | Count and report them | traced |
| 3.11 | `ogbox_resign.cpp` (cache) | A file that failed is recorded as done | Record only files read to the end | traced |
| 3.12 | `ps5_vfs_smb.cpp:362`, `ps5_vfs_ftp.cpp:636` | A store deletes the existing file, then renames: a failed rename loses both | Rename over; where the server refuses, move the old one aside and put it back on failure | checked |
| 3.13 | `platform/ps5_update.cpp:1348` | A new download wipes `.update`, an interrupted install's backups included | Keep the backups until an install finished or was undone | checked |
| 3.14 | `ps5_update.cpp:2033` | Each file is moved aside before the new one is moved in | Rename over where the file system allows; recover at start when a file is missing | plausible |
| 3.15 | `ogbox_library.cpp:159-203` | A cover cut off midway is saved | Check the length and the image's end; write through a temporary file | traced |

PC checks: tests for 3.1, 3.2, 3.5, 3.11 and the updater's recovery (3.13, 3.14) in temporary trees.

## Batch 4: stuck and frozen screens

Test build question: *do the hard disk manager's jobs show progress, and does a refused restart or a
dead share leave the screen usable?*

| # | Where | Defect | Fix | State |
| --- | --- | --- | --- | --- |
| 4.1 | `ogbox_main.cpp:2033`, `:5954` | A refused guest relaunch is ignored: the game stays frozen, `qm.busy` stays set | Check the result; resume the game with a notice | checked |
| 4.2 | `ogbox_main.cpp:6156` | `qm.result` is never reset | Reset when the menu opens | checked |
| 4.3 | `ogbox_main.cpp:6946-7121` | The hard disk manager's jobs run on the UI thread | Run them as jobs with progress, like the move | traced |
| 4.4 | `ogbox_main.cpp:9090` | A pending share test ignores every button | CIRCLE abandons the test | plausible |
| 4.5 | `ogbox_main.cpp:1782, 2173` | Share reads on the UI thread at a game's start and at OGBox's | Move to the listing job; show "Connecting" | traced / plausible |
| 4.6 | `ogbox_main.cpp` (game start) | A game's start waits on a stalled sync | A time limit, then start with a notice | traced |
| 4.7 | `ogbox_main.cpp:729` area | A job thread that can't be created leaves the sync "running" | Clear the state on failure | traced |
| 4.8 | `platform/ps5_bootlog.cpp:1067` | "Save the Log" writes while holding the log lock | Copy the ring under the lock, write outside it | plausible |
| 4.9 | `core/ogbox_trainers.c:357` | The index is written under the lock the Mods screen needs | Write outside the lock | traced |
| 4.10 | `ogbox_intro.cpp:431` | A lost wake-up can hang the intro's close | Set the flag under the mutex | traced |
| 4.11 | `hakux/apu/out_ps5.c:105` | The audio thread spins when output fails | Sleep a buffer's length on failure; reopen | traced |
| 4.12 | `ogbox_main.cpp:1466` | With the texture slots full a cover is decoded and dropped each frame | Remember the refusal until a slot frees | plausible |

## Batch 5: menus and wrong behaviour

Test build question: *a walk through the menus on a list (written with the build).*

| # | Where | Defect | Fix | State |
| --- | --- | --- | --- | --- |
| 5.1 | `ogbox_main.cpp:7673` | The home cache doesn't see a re-sort: names beside the wrong covers, X starts another game | Key the cache on the list's order | traced |
| 5.2 | `ogbox_main.cpp:8424, 8619` | "Default" and "Reset" in a running game's settings apply at the next start only | Apply as a changed row does | traced |
| 5.3 | `ogbox_main.cpp:8775` | Network's "Reset section" removes the shares | Leave the shares | traced |
| 5.4 | `ogbox_main.cpp:9231` | Cancelling a share edit leaves it unmounted | Mount it again | traced |
| 5.5 | `ogbox_main.cpp:9201-9291` | A failed "Connect and save" leaks a mount; a retry saves as `name-2` | Unmount on failure | traced |
| 5.6 | `ogbox_main.cpp:6865` | `hdd.selT` is stale after a rescan | Clamp after each scan | traced |
| 5.7 | `ogbox_main.cpp:8585` | A dead "Saves and caches" row in a game's settings | Remove or wire it | traced |
| 5.8 | `ogbox_main.cpp:4843` | No screenshot with frame generation | Take it from the frame before generation | traced |
| 5.9 | `ogbox_main.cpp:2019` | `boot.txt`'s options are sent again on an XBE switch | Send once per game | traced |
| 5.10 | `ogbox_main.cpp:1047` | The cover decoder evicts by name | Evict by key | traced |
| 5.11 | `ogbox_main.cpp:9370, 9375` | "Import Saves" shown although saves exist (letter case); not refreshed after an import | Compare without case; refresh | plausible / traced |
| 5.12 | `ogbox_main.cpp:9428` | After deleting the "where I left off" state the highlight shifts | Keep the action's index | traced |
| 5.13 | `ogbox_main.cpp:3672` | One sheared frame when the picture shrinks | Take the size and the image together | plausible |
| 5.14 | `platform.c:287` | A pad that disconnects keeps its buttons held | Clear its state on a failed read | plausible |
| 5.15 | `platform/ps5_keyboard.cpp:134` | Backspace and arrows applied before letters of the same poll; a replugged keyboard not found | Keep the event order; open again on failure | traced / plausible |
| 5.16 | `ogbox_ui_settings.cpp:526, 535, 690` | A second keyboard open drops the first callback; number prompts take any text; invalid UTF-8 can be inserted | Refuse a second open; filter digits; check the bytes | plausible / traced / traced |
| 5.17 | `ogbox_intro.cpp:138, 262` | A mismatched or undecodable intro video shears or blanks | Check the size; skip the intro on a decode failure | plausible |
| 5.18 | `platform/ps5_sandbox.cpp:129` | Run from USB with a stale copy in `/data/homebrew`, the updater, logs and mods use the stale copy | Take the title folder from where it runs | plausible |
| 5.19 | `ogbox_library.cpp:775, 709, 575, 944` | A redirect pauses every cover for 60 s; the prefetch skips games; two games share and rename a cover; "Covers 0 of N" offline | Per-cover back-off; finish the queue; key covers per game; say "offline" | plausible / plausible / plausible / cosmetic |

## Batch 6: the network file layer

Test build question: *do games and saves on a share work as before (SMB, NFS, FTP)?* Needs the
owner's shares; each row is shown on the PC against a local server first where one exists.

| # | Where | Defect | Fix | State |
| --- | --- | --- | --- | --- |
| 6.1 | `ps5_vfs.cpp:917`, `ogbox_shares.cpp:168` | `domain;user` is encoded away | Keep the semicolon | traced |
| 6.2 | `ps5_vfs_ftp.cpp:649` | The first delete after an idle time-out fails | Reconnect and retry once, as reads do | traced |
| 6.3 | `ps5_vfs_smb.cpp:330` | A full disk reads as a lost connection; the `.part` stays | Report the error; remove the `.part` | traced |
| 6.4 | `ps5_vfs.cpp:1578` | A read that fails partway returns a short success | Return the error | traced |
| 6.5 | `ps5_vfs_nfs.cpp:492` | Symlinked folders show as small files | Resolve links in listings | plausible |
| 6.6 | `ps5_vfs.cpp:85, 941` | Unmounted backends are never destroyed | Destroy when the last handle closes | traced |
| 6.7 | `ps5_vfs.cpp:947` | A mount-point string can dangle | Return a copy | plausible |
| 6.8 | `ps5_vfs_smb.cpp:238`, `ps5_vfs_nfs.cpp:488` | Listing names with `/` or `..` pass | Drop them in the listing | plausible |
| 6.9 | `StoreFrom` in SMB, NFS, FTP | The share's lock is held for a whole upload | Lock per block, or a second connection | traced |
| 6.10 | `ps5_vfs.cpp:1539` | A disc close holds the global lock through a network close | Close outside the lock | traced |
| 6.11 | `ps5_vfs_nfs.cpp:310` | A name lookup with no time limit under the lock | Look up outside the lock, with a limit | plausible |
| 6.12 | `ps5_vfs_ftp.cpp:184, 479, 165, 236` | Unbounded reply and listing reads; an unchecked passive port; a time overflow | Limits and checks | plausible |
| 6.13 | `ps5_vfs.cpp:346` | Seek and read-size arithmetic unchecked | Check in 64 bits | latent |
| 6.14 | `ogbox_shares.cpp:110, 199, 222, 355` | Long lines split; newlines in values; no sync on write; discovery names not sanitised | Read whole lines; refuse control characters; sync; sanitise | low |
| 6.15 | `platform/ps5_update.cpp` | The updater follows a redirect to http | https only | traced |

## Batch 7: mods, patches, trainers

Test build question: *do the mods and patches of the owner's games still show and apply?* On main
these files are live for every console: the checks of `game-fixes.md` apply.

| # | Where | Defect | Fix | State |
| --- | --- | --- | --- | --- |
| 7.1 | `core/ogbox_mods.c:1495` | An unreadable `mods.txt` is replaced by one line | Tell "missing" from "unreadable"; never rewrite the second | plausible |
| 7.2 | `core/ogbox_mods.c:~1430` | A `mods.txt` shadows another | One place, in a fixed order | traced |
| 7.3 | `core/ogbox_mods.c:525` | Four XBE names cached per game; a fifth marks valid mods "for another release" | Size by the game's XBEs | traced |
| 7.4 | `core/ogbox_mods.c:811, 898` | A tab or newline in a mod file's name garbles the manifest | Refuse such names | plausible |
| 7.5 | `core/ogbox_patches.c:365` | A record over 8191 characters is cut and can be applied in part | Refuse a record that didn't fit | traced |
| 7.6 | `core/ogbox_patches.c:369` | Malformed records are skipped, the patch still reports success | Fail the patch | traced |
| 7.7 | `core/ogbox_patches.c:430` | `../` in a record's file name (a read only) | Refuse | traced |
| 7.8 | `core/ogbox_trainers.c:216, 59` | A passing read error marks a trainer broken until its file changes; a NULL check after use | Don't cache read errors; check first | traced |

PC checks: `tests/` cases for 7.1, 7.3, 7.5, 7.6, 7.7 and the whole `patches.db` through the changed
parser (no record may change its result).

## Batch 8: diagnostics, low and latent

No test build of its own: goes with another batch.

| # | Where | Defect | State |
| --- | --- | --- | --- |
| 8.1 | `platform/ps5_crash.cpp:928` | The stall report's "inside signal" part can't work (a 256-byte read cap) | traced |
| 8.2 | `ps5_crash.cpp:614, 518` | Two crashing threads interleave; calls that may take locks in the handler | plausible |
| 8.3 | `ps5_bootlog.cpp:841` | Thread-name slots leak (64, then unnamed) | traced |
| 8.4 | `ps5_bootlog.cpp:221` | stderr doesn't reach the kernel log in the default mode (may be meant) | plausible |
| 8.5 | `platform/ps5_takeover.cpp:244-679` | Three tables overflow without a log line; a read before the fault guard | plausible |
| 8.6 | `platform/ps5_clock.c:74` | A spin with no yield | latent |
| 8.7 | `platform/ps5_libc.c:367, 478` | A shared static buffer; a poll that swallows interrupts | latent |
| 8.8 | `hakux/compat/hakux_ps5.c:279, 430` | An event slot table that never frees; a NULL check after use | latent |
| 8.9 | `ogbox_ui.cpp:472` | A values map that only grows | latent |

## Not checked

- No finding was reproduced on a console; none was seen in a log. The states above are from reading
  the code.
- 2.1 matches the owner's description of the move, but the screen wasn't observed: if the move
  still stops at its last file after batch 2, the cause is elsewhere and a log is needed.
