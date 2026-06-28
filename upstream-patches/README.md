# Upstream patches

Fixes to our **dependencies** (PAX, badge-bsp, …) that we intend to contribute
upstream, kept here as tracked patch files so they are version-controlled,
self-documenting, and PR-ready. This is the concrete mechanism behind the
upstream-first policy in [`../specs/07-upstream-contributions.md`](../specs/07-upstream-contributions.md).

## Why patches (not branches / not vendor-forks)

The dependency sources live in `managed_components/`, which is **gitignored** and
owned by the ESP-IDF component manager — we can't branch or commit into it, and we
must not vendor-fork a maintained dependency (CLAUDE.md). So a fix lives as a
*patch* against the pinned component, tracked in git here, and re-applied to the
local build tree at build time. The patch file **is** the future PR.

## Layout

```
upstream-patches/
  <component-key>/
    NNNN-short-title.patch
```

`<component-key>` maps to a `managed_components/` dir in
`tools/apply-upstream-patches.sh` (e.g. `pax-graphics` → `robotman2412__pax-gfx`).

## Patch format

A unified diff (`-p1`, paths `a/… b/…` relative to the component root) preceded by
a prose header that explains **why the patch exists** — the bug, who it affects,
why we carry it, and the intended upstream target. The header makes the file a
ready-to-submit PR description; GNU `patch` ignores the prose and applies the diff.

## How they're applied

`tools/apply-upstream-patches.sh` (invoked by `make patches`, and automatically by
`make host` and `make build`) applies every patch onto `managed_components/`. It is
**idempotent** — an already-applied patch is detected and skipped — so it's safe to
run on every build. `.component_hash` is left intact: the component manager's
default integrity check compares the lock hash to that file, not to the file
contents, so a patched component passes cleanly and is never reverted.

> Fresh clone: `managed_components/` is populated by the IDF component manager on
> the first `make build` / reconfigure. Run that once; thereafter `make host` and
> `make build` keep the patches applied.

## Lifecycle

1. Hit a dependency bug/gap → add a documented patch here + log it in spec 07.
2. Carry it locally (auto-applied) until we coordinate with the maintainer.
3. Submit the patch upstream (e.g. `git am` it into a clone of the dep's repo).
4. When it merges and we bump the dependency, **delete the patch file**.
