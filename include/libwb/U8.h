/*
Copyright (c) 2026 - Jacob Nilsson

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/

#ifndef WII_BNR_U8_ARCHIVE_H_
#define WII_BNR_U8_ARCHIVE_H_

#include <cstddef>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <istream>
#include <map>
#include <string>
#include <vector>

#include "Ash.h"
#include "Yaz0.h"
#include "LZ77.h"

namespace u8archive
{

namespace detail
{

inline uint32_t ReadBE32(const uint8_t* p)
{
   return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
          (uint32_t(p[2]) <<  8) |  uint32_t(p[3]);
}

inline std::string ToLower(const std::string& s)
{
   std::string out(s);
   for (size_t i = 0; i != out.size(); ++i)
      out[i] = char(std::tolower((unsigned char)out[i]));

   return out;
}

inline std::string NormalizePath(const std::string& path)
{
   std::string out;
   out.reserve(path.size());

   for (size_t i = 0; i != path.size(); ++i)
   {
      const char c = path[i];
      if (c == '\\' || c == '/')
      {
         if (!out.empty() && out[out.size() - 1] != '/')
            out += '/';
      }
      else
      {
         out += char(std::tolower((unsigned char)c));
      }
   }

   if (!out.empty() && out[0] == '/')
      out.erase(0, 1);
   if (!out.empty() && out[out.size() - 1] == '/')
      out.erase(out.size() - 1);

   return out;
}

}  // namespace detail

const uint32_t U8_MAGIC = 0x55AA382D;

enum Compression
{
   COMPRESSION_NONE,
   COMPRESSION_ASH,
   COMPRESSION_YAZ0,
   COMPRESSION_LZ77,
   COMPRESSION_LZ77_MAGIC   // 'LZ77' fourcc in front of the header
};

inline const char* CompressionName(Compression c)
{
   switch (c)
   {
   case COMPRESSION_ASH:         return "ASH0";
   case COMPRESSION_YAZ0:        return "Yaz0";
   case COMPRESSION_LZ77:        return "LZ77";
   case COMPRESSION_LZ77_MAGIC:  return "LZ77 (fourcc)";
   default:                      return "uncompressed";
   }
}

inline Compression IdentifySkippingIMD5(const uint8_t*& data, size_t& size)
{
   if (!data)
      return COMPRESSION_NONE;

   if (size > 0x40 && data[0] == 'I' && data[1] == 'M' &&
       data[2] == 'D' && data[3] == '5')
   {
      data += 0x20;
      size -= 0x20;
   }

   if (ash::IsCompressed(data, size))
      return COMPRESSION_ASH;

   if (yaz0::IsCompressed(data, size))
      return COMPRESSION_YAZ0;

   if (lz77::HasMagic(data, size))
      return COMPRESSION_LZ77_MAGIC;

   if (lz77::IsCompressed(data, size))
      return COMPRESSION_LZ77;

   return COMPRESSION_NONE;
}

inline Compression Identify(const uint8_t* data, size_t size)
{
   return IdentifySkippingIMD5(data, size);
}

inline bool DecompressFileData(const uint8_t* data, size_t size,
                               std::vector<uint8_t>& out, Compression* used = NULL)
{
   out.clear();

   if (!data)
      return false;

   const Compression c = IdentifySkippingIMD5(data, size);
   if (used)
      *used = c;

   switch (c)
   {
   case COMPRESSION_ASH:
      return ash::Decompress(data, size, out);

   case COMPRESSION_YAZ0:
      return yaz0::Decompress(data, size, out);

   case COMPRESSION_LZ77_MAGIC:
      return lz77::Decompress(data + 4, size - 4, out);

   case COMPRESSION_LZ77:
      return lz77::Decompress(data, size, out);

   default:
      out.assign(data, data + size);
      return true;
   }
}

inline long FindU8Tag(const uint8_t* data, size_t size)
{
   if (!data || size < 0x20)
      return -1;

   if (detail::ReadBE32(data) == U8_MAGIC)
      return 0;

   if (size > 0x620 && detail::ReadBE32(data + 0x600) == U8_MAGIC)
      return 0x600;

   if (size > 0x660 && detail::ReadBE32(data + 0x640) == U8_MAGIC)
      return 0x640;

   return -1;
}

class Archive
{
public:
   struct Entry
   {
      bool directory;
      std::string path;
      uint32_t offset;
      uint32_t length;
   };

   Archive() : tag_offset(0), fst_offset(0), name_offset(0), entry_count(0) {}

   bool OpenFile(const std::string& filename)
   {
      std::ifstream file(filename.c_str(), std::ios::binary);
      if (!file.is_open())
         return false;

      return OpenStream(file, 0, 0);
   }

   bool OpenStream(std::istream& in, std::streamoff offset, size_t length = 0,
                   Compression* used = NULL)
   {
      std::vector<uint8_t> bytes;

      in.clear();
      if (length == 0)
      {
         in.seekg(0, std::ios::end);
         const std::streamoff end = in.tellg();
         if (end <= offset)
            return false;

         length = size_t(end - offset);
      }

      in.seekg(offset, std::ios::beg);
      if (!in)
         return false;

      bytes.resize(length);
      in.read(reinterpret_cast<char*>(&bytes[0]), std::streamsize(length));
      if (in.gcount() <= 0)
         return false;

      bytes.resize(size_t(in.gcount()));
      in.clear();

      return SetCompressedData(&bytes[0], bytes.size(), used);
   }

   bool OpenStream(std::istream& in)
   {
      std::vector<uint8_t> bytes;

      in.seekg(0, std::ios::end);
      const std::streampos end = in.tellg();
      in.seekg(0, std::ios::beg);

      if (end <= 0)
         return false;

      bytes.resize(size_t(end));
      in.read(reinterpret_cast<char*>(&bytes[0]), std::streamsize(bytes.size()));
      if (!in)
         return false;

      return SetData(bytes);
   }

   bool SetData(const std::vector<uint8_t>& bytes)
   {
      Clear();
      buffer = bytes;
      return Parse();
   }

   bool SetCompressedData(const uint8_t* data, size_t size,
                          Compression* used = NULL)
   {
      const uint8_t* payload = data;
      size_t payload_size = size;

      const Compression c = IdentifySkippingIMD5(payload, payload_size);
      if (used)
         *used = c;

      if (c == COMPRESSION_NONE)
         return SetData(payload, payload_size);

      std::vector<uint8_t> plain;
      if (!DecompressFileData(data, size, plain))
         return false;

      return SetData(plain);
   }

   bool SetData(const uint8_t* data, size_t size)
   {
      Clear();
      if (!data || size == 0)
         return false;

      buffer.assign(data, data + size);
      return Parse();
   }

   void Clear()
   {
      buffer.clear();
      entries.clear();
      index.clear();
      tag_offset = fst_offset = name_offset = entry_count = 0;
   }

   bool IsOpen() const { return entry_count != 0; }

   const std::vector<Entry>& Entries() const { return entries; }

   long FindFile(const std::string& path) const
   {
      std::map<std::string, size_t>::const_iterator it =
         index.find(detail::NormalizePath(path));

      if (it == index.end())
         return -1;

      return long(it->second);
   }

   bool ReadFileRaw(size_t entry, const uint8_t** data, uint32_t* size) const
   {
      if (entry >= entries.size() || entries[entry].directory)
         return false;

      const Entry& e = entries[entry];
      if (size_t(tag_offset) + e.offset + e.length > buffer.size())
         return false;

      if (data)
         *data = &buffer[tag_offset + e.offset];
      if (size)
         *size = e.length;

      return true;
   }

   bool ReadFile(size_t entry, std::vector<uint8_t>& out) const
   {
      const uint8_t* data = NULL;
      uint32_t size = 0;

      if (!ReadFileRaw(entry, &data, &size))
         return false;

      return DecompressFileData(data, size, out);
   }

   bool ReadFile(const std::string& path, std::vector<uint8_t>& out) const
   {
      const long entry = FindFile(path);
      if (entry < 0)
         return false;

      return ReadFile(size_t(entry), out);
   }

private:
   bool Parse()
   {
      const long tag = FindU8Tag(buffer.empty() ? NULL : &buffer[0], buffer.size());
      if (tag < 0)
         return false;

      tag_offset = uint32_t(tag);

      const uint8_t* base = &buffer[tag_offset];
      const size_t avail = buffer.size() - tag_offset;

      if (avail < 0x20)
         return false;

      const uint32_t root = detail::ReadBE32(base + 0x04);
      const uint32_t fst_size = detail::ReadBE32(base + 0x08);

      if (root < 0x20 || size_t(root) + 12 > avail)
         return false;

      fst_offset = root;
      entry_count = detail::ReadBE32(base + root + 8);

      if (entry_count == 0 || size_t(root) + size_t(entry_count) * 12 > avail)
         return false;

      name_offset = fst_offset + entry_count * 12;

      // fst_size covers the FST plus the name table; clamp the name table to
      // whatever we actually have.
      size_t name_end = avail;
      if (fst_size != 0 && size_t(root) + fst_size <= avail)
         name_end = size_t(root) + fst_size;

      entries.reserve(entry_count);

      // Stack of (end index, path prefix) for the directories we are inside.
      std::vector<std::pair<uint32_t, std::string> > dirs;
      dirs.push_back(std::make_pair(entry_count, std::string()));

      Entry root_entry;
      root_entry.directory = true;
      root_entry.path = "";
      root_entry.offset = 0;
      root_entry.length = entry_count;
      entries.push_back(root_entry);

      for (uint32_t i = 1; i < entry_count; ++i)
      {
         const uint8_t* raw = base + fst_offset + i * 12;
         const uint32_t word = detail::ReadBE32(raw);
         const uint32_t type = word >> 24;
         const uint32_t name_at = word & 0x00FFFFFF;

         while (dirs.size() > 1 && i >= dirs.back().first)
            dirs.pop_back();

         Entry e;
         e.directory = (type != 0);
         e.offset = detail::ReadBE32(raw + 4);
         e.length = detail::ReadBE32(raw + 8);

         const size_t name_pos = size_t(name_offset) + name_at;
         if (name_pos >= name_end)
            return false;

         std::string name;
         for (size_t p = name_pos; p < name_end && base[p]; ++p)
            name += char(base[p]);

         e.path = dirs.back().second + name;
         entries.push_back(e);

         if (e.directory)
         {
            if (e.length > entry_count)
               return false;

            dirs.push_back(std::make_pair(e.length, e.path + "/"));
         }
         else
         {
            index[detail::ToLower(e.path)] = i;
         }
      }

      return true;
   }

   std::vector<uint8_t> buffer;
   std::vector<Entry> entries;
   std::map<std::string, size_t> index;

   uint32_t tag_offset;
   uint32_t fst_offset;
   uint32_t name_offset;
   uint32_t entry_count;
};

class MemoryStream : public std::istream
{
public:
   explicit MemoryStream(const std::vector<uint8_t>& data)
      : std::istream(NULL), buf(data)
   {
      rdbuf(&buf);
   }

private:
   class Buffer : public std::streambuf
   {
   public:
      explicit Buffer(const std::vector<uint8_t>& data)
      {
         char* begin = const_cast<char*>(
            reinterpret_cast<const char*>(data.empty() ? NULL : &data[0]));
         setg(begin, begin, begin + data.size());
      }

   protected:
      pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                       std::ios_base::openmode which)
      {
         char* base = eback();
         char* target = (dir == std::ios_base::cur) ? gptr()
                      : (dir == std::ios_base::end) ? egptr() : base;

         target += off;
         if (target < base || target > egptr())
            return pos_type(off_type(-1));

         if (which & std::ios_base::in)
            setg(base, target, egptr());

         return pos_type(target - base);
      }

      pos_type seekpos(pos_type pos, std::ios_base::openmode which)
      {
         return seekoff(off_type(pos), std::ios_base::beg, which);
      }
   };

   Buffer buf;
};

}  // namespace u8archive

#endif
