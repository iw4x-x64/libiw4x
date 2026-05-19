#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace iw4x::demonware
{
  // Read a file into a byte vector.
  //
  // Returns an empty vector on failure. This is deliberate: a missing file
  // is often a legitimate state (for example, a brand-new player who has
  // not saved any stats yet). The caller decides whether an empty result is
  // an error or a perfectly fine default.
  //
  std::vector<uint8_t>
  read_file (const std::filesystem::path&, std::string_view label);

  // Write a byte vector to a file.
  //
  // Creates parent directories as needed. Returns true on success so the
  // caller knows whether it is safe to update any in-memory cache.
  //
  bool
  write_file (const std::filesystem::path&,
              const uint8_t* data,
              std::size_t size,
              std::string_view label);
}
