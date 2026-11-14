#!/usr/bin/env bash
# OGBox by Press5elect, GPL-3.0-or-later. Every test that runs on a PC, one after another; each
# builds into build/<name>-tests. The front end (ogbox_main.cpp) is type-checked: it only builds for
# the console. The updater's needs libzip and RapidJSON in .deps/src
# (ps5/tools/build-deps.sh, or --fetch to fetch only those two at build-deps.sh's revisions).
#   tests/run-pc-tests.sh [--fetch]
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
if [[ ${1:-} == --fetch ]]; then
	grep -E '^fetch (libzip|rapidjson) ' ps5/tools/build-deps.sh | while read -r _ name url revision; do
		[[ -d .deps/src/$name/.git ]] && continue
		mkdir -p ".deps/src/$name"
		git -C ".deps/src/$name" init -q
		git -C ".deps/src/$name" remote add origin "$url"
		git -C ".deps/src/$name" fetch -q --depth 1 origin "$revision"
		git -C ".deps/src/$name" -c advice.detachedHead=false checkout -q FETCH_HEAD
	done
fi
failed=()
run() {
	local name=$1
	shift
	echo "== $name"
	"$@" || failed+=("$name")
}
run patches tests/patches/run-patches-tests.sh build/patches-tests
run xpr tests/xpr/run-xpr-tests.sh build/xpr-tests
run pad tests/pad/run-pad-tests.sh build/pad-tests
run shares tests/shares/run-shares-tests.sh build/shares-tests
run update tests/update/run-update-tests.sh build/update-tests
run fixes tests/fixes/run-fixes-tests.sh build/fixes-tests
run ui tests/ui/run-ui-tests.sh build/ui-tests
run frontend tests/ui/check-frontend.sh
if ((${#failed[@]})); then
	echo "failed: ${failed[*]}"
	exit 1
fi
echo "all PC tests passed"
