// malloc.h — host compatibility shim for macOS/BSD.
//
// PAX includes <malloc.h> (a glibc-ism); on Darwin malloc/free/realloc live in
// <stdlib.h>. Added to PAX's include path only on Apple so vendored sources
// build unmodified. Linux already provides <malloc.h>.
#pragma once
#include <stdlib.h>
