#pragma once

#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

#include <libiw4x/logger.hxx>

namespace iw4x::demonware
{
  // Base exception for all Demonware emulation failures.
  //
  class demonware_error: public std::runtime_error
  {
  public:
    explicit
    demonware_error (const std::string& what)
      : std::runtime_error (what) {}

    explicit
    demonware_error (const char* what)
      : std::runtime_error (what) {}
  };

  // A wire-format parse error.
  //
  // Thrown when a request payload cannot be deserialized. This typically means
  // the engine sent us something we don't understand, which is either a bug in
  // our emulation or an unexpected engine version.
  //
  class parse_error: public demonware_error
  {
  public:
    uint8_t service;
    uint8_t sub_function;

    explicit
    parse_error (uint8_t svc, uint8_t sub, const std::string& detail)
      : demonware_error (
          std::format ("demonware: parse error: service {} sub {}: {}",
                       static_cast<int> (svc),
                       static_cast<int> (sub),
                       detail)),
        service (svc),
        sub_function (sub) {}
  };

  // A service dispatch error.
  //
  // Thrown when we receive a request for a service or sub-function that nobody
  // has registered a handler for.
  //
  class service_error: public demonware_error
  {
  public:
    uint8_t service;
    uint8_t sub_function;

    explicit
    service_error (uint8_t svc, uint8_t sub)
      : demonware_error (
          std::format ("demonware: unhandled service {} sub {}",
                       static_cast<int> (svc),
                       static_cast<int> (sub))),
        service (svc),
        sub_function (sub) {}
  };

  // A storage I/O error.
  //
  // Thrown when a file read or write fails in a way that we cannot recover from
  // gracefully. Note that a missing file is not an error. That is, it's a
  // legitimate state for new players.
  //
  class storage_io_error: public demonware_error
  {
  public:
    std::string path;

    explicit
    storage_io_error (const std::string& p, const std::string& detail)
      : demonware_error (
          std::format ("demonware: storage I/O error: {}: {}", p, detail)),
        path (p) {}
  };
}
