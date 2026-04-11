#include "TzDbLookup.h"
#include "tz_data.h"

const char* TzDbLookup::getPosix(const char* iana) {
  for (size_t i = 0; i < tzCount; ++i) {
    if (strcasecmp(iana, tzTable[i].iana) == 0) {
      return tzTable[i].posix;
    }
  }
  return nullptr;
}