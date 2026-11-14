# Making a release

A release is the title folder as a ZIP on a GitHub release whose tag is the commit the ZIP was
built from. **Only when the owner asks.** The owner creates the tag and the release; the agent
prepares the branch, the pull request, the build, the ZIP and the notes.

## Never in a release

Games or game files of any kind, saves, BIOS or firmware files, keys, the console's address,
captured logs, screenshots of games. The release check (`stage-notices.py --check`) refuses a
build whose notices are incomplete or dirty. The release's assets are the ZIP and its `.sha256`
only: **no source archive is attached** (the source is the repository at the tag, plus hakuX at
the pin).

## Steps

1. **The core.** Cut `tiersN` in hakux-native from the tested work branch and push it (with the
   owner's word). `ps5/tools/hakux-patch.sh ../hakux-native tiersN`, commit `ps5.patch`
   ([`hakux-fork.md`](hakux-fork.md)).
2. **The version**, in one commit:
   - `ps5/version.txt` (`X.Y.Z`);
   - `ps5/sce_sys/param.json`'s `contentVersion`: `0X.(Y*100+Z as three digits).000` (1.0.2 is
     `01.002.000`; `build-identity.sh` checks it);
   - `PROVENANCE.md`: "current to OGBox X.Y.Z", the hakux-native branch, what the release added;
   - `ps5/notices/components.json` if a third-party part was added or its revision moved;
   - `docs/releases/CHANGELOG.md`: the release's short entry at the top;
   - `docs/releases/README.md`: its row;
   - the README, where a feature changed what it says.
3. **PC tests**: `tests/run-pc-tests.sh --fetch` must pass.
4. **Push the branch and open the pull request** to `main` (the owner's word covers both when they
   asked for the release). Merge it (a merge commit).
5. **The release build, from the merge commit, clean tree:**

   ```bash
   git fetch origin main && git checkout --detach origin/main
   OGBOX_RELEASE=1 PS5_CLANG=/usr/lib/llvm-20/bin/clang bash ps5/tools/build.sh
   python3 ps5/tools/stage-notices.py --check dist/PPSA99251
   python3 ps5/tools/make-release-zip.py vX.Y.Z --date YYYY-MM-DD
   cd dist/release-vX.Y.Z && sha256sum OGBox-vX.Y.Z.zip > OGBox-vX.Y.Z.zip.sha256
   ```

   The build identity it prints, and the ZIP's SHA-256, go into the notes. Check the ZIP has the
   startup video (`assets/intro.ivf`, `intro.wav`) and no mod folder that was withdrawn.
6. **The notes**: `docs/releases/vX.Y.Z.md`, in the form of the last release's page:
   Added, Fixed, Tested (on a PS5, from test builds: what the owner confirmed and what the logs
   measured; nothing about what wasn't tested), Installation, Reporting problems, Source and
   licensing, Build versions (identity, commit, component table, the ZIP's SHA-256). Title IDs
   only. Commit them (on the session branch; they reach `main` with the next merge, as before).
7. **Hand over** to the owner: the ZIP, the `.sha256`, the notes file, and the tag to create
   (`vX.Y.Z` on the merge commit). The owner publishes.
8. **Read back** once published: fetch the release, compare the asset's SHA-256 with the notes,
   and check the body names no game.

## After a release

The session branch is merged: restart it from `main` for new work. Test builds go back to
developer builds (`+dirty` in the identity is normal for them).
