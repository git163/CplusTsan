#include "core_api.h"

namespace {

#ifdef TSAN_BUILD
const char kVersion[] = "core-v1-tsan";
#else
const char kVersion[] = "core-v1";
#endif

}  // namespace

const char* core_version() { return kVersion; }