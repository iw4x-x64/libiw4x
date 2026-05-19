#include <libiw4x/demonware/storage.hxx>

#include <filesystem>
#include <string>
#include <vector>

#include <tracy/Tracy.hpp>

#include <libiw4x/demonware/bit-buffer.hxx>
#include <libiw4x/demonware/connection.hxx>
#include <libiw4x/demonware/diagnostics.hxx>
#include <libiw4x/demonware/task.hxx>
#include <libiw4x/demonware/utility.hxx>

using namespace std;
using namespace std::filesystem;

namespace iw4x::demonware
{
  alignas (16) bd_storage_stub storage_stub_ {};

  namespace
  {
    // Publisher file context.
    //
    // The publisher file (playlists) is static, so we load it once from disk
    // and then serve it from memory for the rest of the session.
    //
    constexpr const char* publisher_filename ("playlists.patch2");

    vector<uint8_t> publisher_data;
    bool            publisher_loaded (false);

    // User file context.
    //
    // Tracks the player's stats (mpdata). Similar lazy-loading but with an
    // existence flag so we can gracefully handle new players who have not saved
    // anything yet.
    //
    constexpr const char* user_file_dir  ("players");
    constexpr const char* user_filename  ("mpdata");

    vector<uint8_t> user_data;
    bool            user_loaded (false);
    bool            user_exists (false);

    const vector<uint8_t>&
    ensure_publisher_data ()
    {
      if (!publisher_loaded)
      {
        publisher_data = read_file (publisher_filename, "publisher file");
        publisher_loaded = true;
      }

      return publisher_data;
    }

    void
    ensure_user_data ()
    {
      if (!user_loaded)
      {
        path p (path (user_file_dir) / user_filename);
        user_data   = read_file (p, "user file");
        user_exists = !user_data.empty ();
        user_loaded = true;
      }
    }

    // Write a bdLobbyFileHeader into a reply buffer.
    //
    // The layout is dictated by the bdBitBuffer wire format. We have to
    // write exactly these fields in exactly this order or the engine's
    // deserialization code will choke.
    //
    //  uint64(10) : file_id
    //  uint32(8)  : file_size
    //  uint32(8)  : create_time
    //  bool(1)    : has_data
    //  bool(1)    : visibility
    //  uint64(10) : owner_id
    //  string(16) : filename
    //
    void
    write_file_header (bit_buffer_writer& reply,
                       uint64_t           fid,
                       uint32_t           fsize,
                       const char*        fname)
    {
      reply.write_uint64 (fid);
      reply.write_uint32 (fsize);
      reply.write_uint32 (0);     // Create time (unused).
      reply.write_bool   (true);  // Has data.
      reply.write_bool   (false); // Visibility.
      reply.write_uint64 (0);     // Owner ID.
      reply.write_string (fname);
    }

    // The main storage request handler.
    //
    // Dispatched by the task manager when the engine sends a request to service
    // ID 10 (bdStorage). We return true as long as we formulated a reply, even
    // if the requested file was not found.
    //
    bool
    storage_handler (uint8_t            service_id,
                     uint8_t            sub_function_id,
                     bit_buffer_reader& request,
                     bit_buffer_writer& reply)
    {
      ZoneScoped;

      log::trace_l1 ("demonware: service={} sub={}",
                     static_cast<int> (service_id),
                     static_cast<int> (sub_function_id));

      switch (sub_function_id)
      {
        // Get publisher file info.
        //
        // The game wants to know if the publisher file exists and how big
        // it is. If we don't have it, return a 0 size and 0 file count.
        //
        case storage_func::get_publisher_file_info:
        {
          auto& data (ensure_publisher_data ());

          if (data.empty ())
          {
            log::warning ("demonware: publisher file not available");
            reply.write_uint32 (0);
            reply.write_uint8 (0);
            return true;
          }

          auto fsize (static_cast<uint32_t> (data.size ()));

          log::info ("demonware: getPublisherFileInfo -> {} ({}B)",
                     publisher_filename, fsize);

          reply.write_uint32 (0);
          reply.write_uint8  (1);
          reply.write_uint32 (fsize);
          write_file_header  (reply, file_id::publisher, fsize,
                              publisher_filename);
          return true;
        }

        // Get user file info (existence check).
        //
        // If the file exists on disk, return its header so the game issues
        // a follow-up sub=5 to download the actual data. If it doesn't
        // exist, return no results and the game handles this gracefully by
        // treating the user as a new player.
        //
        case storage_func::get_user_file:
        {
          ensure_user_data ();

          if (!user_exists)
          {
            log::info ("demonware: getUserFile -> {} (not found, defaults)",
                       user_filename);
            reply.write_uint32 (0);
            reply.write_uint8 (0);
            return true;
          }

          auto fsize (static_cast<uint32_t> (user_data.size ()));

          log::info ("demonware: getUserFile -> {} ({}B)",
                     user_filename, fsize);

          reply.write_uint32 (0);
          reply.write_uint8 (1);
          reply.write_uint32 (fsize);
          write_file_header (reply, file_id::user, fsize, user_filename);
          return true;
        }

        // Download a file by file_id.
        //
        // The request format is a byte padding followed by the uint64
        // file_id (based on reversing 0x14031F300). This file_id was
        // assigned by our sub=8 or sub=7 response headers earlier and is
        // now simply echoed back to us.
        //
        case storage_func::get_file:
        {
          uint8_t  pad;
          uint64_t fid;

          if (!request.read_uint8 (pad) || !request.read_uint64 (fid))
          {
            log::warning ("demonware: getFile: parse error");
            reply.write_uint32 (0);
            reply.write_uint8 (0);
            return true;
          }

          log::trace_l1 ("demonware: getFile file_id={}", fid);

          if (fid == file_id::user)
          {
            ensure_user_data ();

            if (!user_exists || user_data.empty ())
            {
              log::warning ("demonware: user file not available for download");
              reply.write_uint32 (0);
              reply.write_uint8 (0);
              return true;
            }

            auto fsize (static_cast<uint32_t> (user_data.size ()));

            log::info ("demonware: getFile -> {} ({}B)",
                       user_filename, fsize);

            reply.write_uint32 (0);
            reply.write_uint8 (1);
            reply.write_uint32 (fsize);
            write_file_header (reply, file_id::user, fsize, user_filename);
            reply.write_blob (user_data.data (), user_data.size ());
            return true;
          }

          // Default route: assume the game wants the publisher playlists.
          //
          auto& data (ensure_publisher_data ());

          if (data.empty ())
          {
            log::warning ("demonware: publisher file not available for "
                       "download");
            reply.write_uint32 (0);
            reply.write_uint8 (0);
            return true;
          }

          auto fsize (static_cast<uint32_t> (data.size ()));

          log::info ("demonware: getFile -> {} ({}B)",
                     publisher_filename, fsize);

          reply.write_uint32 (0);
          reply.write_uint8 (1);
          reply.write_uint32 (fsize);
          write_file_header (reply, file_id::publisher, fsize,
                             publisher_filename);
          reply.write_blob (data.data (), data.size ());
          return true;
        }

        // Save a file by filename.
        //
        // The wire format (from reversing 0x14031F970):
        //
        //   byte(0) + bool(vis) + string(filename, no null) +
        //   raw_byte(0x00, no type tag) + bool(vis2) + blob(file_data).
        //
        // Our read_string() implementation automatically consumes the raw
        // 0x00 as the null terminator, so no manual skipping is needed.
        //
        case storage_func::set_file:
        {
          uint8_t pad;
          bool    vis;
          string  filename;

          if (!request.read_uint8 (pad) ||
              !request.read_bool (vis)  ||
              !request.read_string (filename, 127))
          {
            log::warning ("demonware: setFile: header parse error");
            reply.write_uint32 (0);
            reply.write_uint8 (0);
            return true;
          }

          bool             vis2;
          vector<uint8_t>  blob;

          if (!request.read_bool (vis2) || !request.read_blob (blob))
          {
            log::warning ("demonware: setFile: blob parse error");
            reply.write_uint32 (0);
            reply.write_uint8 (0);
            return true;
          }

          log::info ("demonware: setFile '{}' ({}B)",
                     filename, blob.size ());

          path p (path (user_file_dir) / filename);
          write_file (p, blob.data (), blob.size (), filename);

          reply.write_uint32 (0);
          reply.write_uint8 (0);
          return true;
        }

        // Save player stats (mpdata).
        //
        // The wire format (from reversing 0x14031F820):
        //
        //   byte(0) + uint64(owner_id) + blob(file_data).
        //
        // We write the file to disk and update our in-memory cache so
        // subsequent reads in this session reflect the new data.
        //
        case storage_func::set_user_file:
        {
          uint8_t          pad;
          uint64_t         owner_id;
          vector<uint8_t>  blob;

          if (!request.read_uint8 (pad)      ||
              !request.read_uint64 (owner_id) ||
              !request.read_blob (blob))
          {
            log::warning ("demonware: setUserFile: parse error");
            reply.write_uint32 (0);
            reply.write_uint8 (0);
            return true;
          }

          log::info ("demonware: setUserFile owner={} size={}",
                     owner_id, blob.size ());

          path p (path (user_file_dir) / user_filename);

          if (write_file (p, blob.data (), blob.size (), "user file"))
          {
            user_data   = move (blob);
            user_exists = true;
            user_loaded = true;
          }

          reply.write_uint32 (0);
          reply.write_uint8 (0);
          return true;
        }

        default:
        {
          log::warning ("demonware: unhandled sub={}",
                     static_cast<int> (sub_function_id));
          return false;
        }
      }
    }
  }

  void storage::
  init ()
  {
    ZoneScoped;

    storage_stub_.status = 0;
    storage_stub_.connection = &lobby_connection;

    task_manager::register_handler (service_id::storage, &storage_handler);
  }

  bd_storage_stub& storage::
  stub ()
  {
    return storage_stub_;
  }
}
