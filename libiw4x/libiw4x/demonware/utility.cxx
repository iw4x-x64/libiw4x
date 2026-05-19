#include <libiw4x/demonware/utility.hxx>

#include <fstream>

#include <tracy/Tracy.hpp>

#include <libiw4x/demonware/diagnostics.hxx>

using namespace std;
using namespace std::filesystem;

namespace iw4x::demonware
{
  vector<uint8_t>
  read_file (const path& p, string_view label)
  {
    ZoneScoped;
    ZoneText (p.string ().c_str (), p.string ().size ());

    if (!exists (p) || !is_regular_file (p))
    {
      log::trace_l1 ("demonware: {}: not found: {}", label, p.string ());
      return {};
    }

    auto sz (file_size (p));

    if (sz == 0)
    {
      log::trace_l1 ("demonware: {}: empty: {}", label, p.string ());
      return {};
    }

    vector<uint8_t> data (static_cast<size_t> (sz));
    ifstream f (p, ios::binary);

    if (!f.read (reinterpret_cast<char*> (data.data ()),
                 static_cast<streamsize> (sz)))
    {
      log::warning ("demonware: {}: read failed: {}", label, p.string ());
      return {};
    }

    log::info ("demonware: {}: loaded {} ({}B)", label, p.string (), sz);
    return data;
  }

  bool
  write_file (const path& p,
              const uint8_t* data,
              size_t size,
              string_view label)
  {
    ZoneScoped;
    ZoneText (p.string ().c_str (), p.string ().size ());

    create_directories (p.parent_path ());

    ofstream f (p, ios::binary | ios::trunc);

    if (!f.write (reinterpret_cast<const char*> (data),
                  static_cast<streamsize> (size)))
    {
      log::warning ("demonware: {}: write failed: {}", label, p.string ());
      return false;
    }

    log::info ("demonware: {}: saved {} ({}B)", label, p.string (), size);
    return true;
  }
}
