#pragma once
#include <cassert>
#include <core/gates.hpp>
#include <core/wire.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <utils/num_formatting.hpp>

using namespace SILICON::core;

inline BusValue valueFor(const Bus& bus, const std::uint64_t value)
{
  return busValueFromInteger(value, bus.size());
}

inline BusValue valueFor(const std::size_t width, const std::uint64_t value)
{
  return busValueFromInteger(value, width);
}

extern "C" {
inline void __ubsan_on_report()
{
  FAIL() << "Encountered an undefined behavior sanitizer error";
}

inline void __asan_on_error()
{
  FAIL() << "Encountered an address sanitizer error";
}

inline void __tsan_on_report()
{
  FAIL() << "Encountered a thread sanitizer error";
}
}  // extern "C"

void PrintTo(const State& s, std::ostream* os)
{
  switch (s) {
    case State::HIGH: *os << "HIGH"; break;
    case State::LOW: *os << "LOW"; break;
    case State::ERROR: *os << "ERROR"; break;
    case State::UNKNOWN: *os << "UNKNOWN"; break;
    default: assert(false);
  }
}
