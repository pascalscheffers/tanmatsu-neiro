#!/usr/bin/env bash
# apply-upstream-patches.sh — apply tracked upstream fixes to the local build tree.
#
# We develop fixes to our dependencies (PAX, badge-bsp, …) as patches tracked in
# upstream-patches/, so they are version-controlled, self-documenting, and ready
# to submit upstream (see specs/07-upstream-contributions.md). The dependency
# sources themselves live in the gitignored, package-manager-owned
# managed_components/, so we re-apply the patches onto that tree at build time.
#
# Idempotent: a patch already applied is detected and skipped, so this is safe to
# run on every build. Each patch's first line names its target component dir.
#
# Mapping is by convention: upstream-patches/<component-key>/*.patch applies to
# managed_components/<managed-dir> where <managed-dir> is resolved below.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PATCH_ROOT="$ROOT/upstream-patches"
MANAGED="$ROOT/managed_components"

# component-key (dir under upstream-patches/) -> managed_components/ subdir.
# A case statement (not an associative array) so this runs on macOS's bash 3.2.
resolve_target() {
    case "$1" in
        pax-graphics) echo "robotman2412__pax-gfx" ;;
        badge-bsp) echo "badgeteam__badge-bsp" ;;
        *) echo "" ;;
    esac
}

[ -d "$PATCH_ROOT" ] || { echo "no upstream-patches/, nothing to do"; exit 0; }

applied=0 skipped=0 missing=0
for keydir in "$PATCH_ROOT"/*/; do
    key="$(basename "$keydir")"
    sub="$(resolve_target "$key")"
    if [ -z "$sub" ]; then
        echo "WARN: no managed_components mapping for '$key' — skipping" >&2
        continue
    fi
    dir="$MANAGED/$sub"
    if [ ! -d "$dir" ]; then
        # managed_components is populated by the IDF component manager on the
        # first device build; until then there is nothing to patch.
        echo "skip $key: $dir not present yet (run a device build first)"
        missing=$((missing + 1))
        continue
    fi
    for patch in "$keydir"*.patch; do
        [ -e "$patch" ] || continue
        name="$(basename "$patch")"
        if patch -p1 -d "$dir" -R --dry-run --force <"$patch" >/dev/null 2>&1; then
            echo "skip $key/$name: already applied"
            skipped=$((skipped + 1))
        elif patch -p1 -d "$dir" --forward <"$patch" >/dev/null; then
            echo "apply $key/$name"
            # Leave .component_hash intact: the IDF component manager's default
            # (non-strict) integrity check compares the lock hash against that
            # file's stored value, not against the actual file contents — so a
            # patched component with its original hash file passes cleanly and is
            # never reverted. (Deleting the file would make the check fatal.)
            applied=$((applied + 1))
        else
            echo "ERROR: $key/$name failed to apply to $dir" >&2
            exit 1
        fi
    done
done
echo "upstream patches: $applied applied, $skipped already-applied, $missing components-not-present"
