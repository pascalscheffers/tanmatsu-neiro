// malloc.h — host compatibility shim for macOS/BSD.
//
// PAX includes <malloc.h> (a glibc-ism); on Darwin malloc/free/realloc live in
// <stdlib.h>. Added to PAX's include path only on Apple so vendored sources
// build unmodified. Linux already provides <malloc.h>.
//
// TODO(upstream): minimal local workaround — see the PAX host-build item in
// specs/07-upstream-contributions.md; retire once PAX builds on macOS as-is.
#pragma once
#include <stdlib.h>
