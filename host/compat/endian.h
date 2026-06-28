// endian.h — host compatibility shim for macOS/BSD.
//
// PAX includes <endian.h> for the BYTE_ORDER / LITTLE_ENDIAN / BIG_ENDIAN
// macros, but Darwin has no such header — they live in <machine/endian.h>.
// This shim is added to PAX's include path only on Apple targets so the
// vendored sources compile unmodified. Linux already provides <endian.h>.
#pragma once
#include <machine/endian.h>
