// Single implementation TU for the vendored miniz (public domain, v1.15).
// Only the zlib-subset inflate/deflate APIs are needed for Fable's
// CompiledDefs *.bin chunk streams.
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#include "miniz.h"
