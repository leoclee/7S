#pragma once

#include <Arduino.h>

class TzDbLookup {
public:
  static const char* getPosix(const char* iana);
};