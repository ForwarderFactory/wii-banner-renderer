/*
Copyright (c) 2010 - Wii Banner Player Project

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

#ifndef WII_BNR_LZ77_H_
#define WII_BNR_LZ77_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <vector>

#include "Endian.h"
#include "Types.h"

enum : uint32_t
{
    BINARY_MAGIC_LZ77 = MAKE_FOURCC('L', 'Z', '7', '7')
};

namespace wb::lz77
{

// The 4-byte header is little-endian: low byte is (type << 4), the upper
// three bytes are the uncompressed size. Type 1 is the only one used here.
// The 'LZ77' fourcc in front of it is optional and often absent.
inline bool IsCompressed(const uint8_t* data, size_t size)
{
   return size >= 8 && (data[0] & 0xF0) == 0x10 &&
          ((uint32_t(data[3]) << 16) | (uint32_t(data[2]) << 8) | data[1]) != 0;
}

inline bool HasMagic(const uint8_t* data, size_t size)
{
   return size >= 12 && data[0] == 'L' && data[1] == 'Z' &&
          data[2] == '7' && data[3] == '7';
}

// Decompresses a headered (non-fourcc) LZ77 blob. Pass data+4 if the blob
// carries the 'LZ77' fourcc.
inline bool Decompress(const uint8_t* data, size_t size, std::vector<uint8_t>& out)
{
   out.clear();

   if (!IsCompressed(data, size))
      return false;

   if ((data[0] & 0x0F) != 0)
      return false;   // only compression type 1 is supported

   const uint32_t out_size =
      (uint32_t(data[3]) << 16) | (uint32_t(data[2]) << 8) | data[1];

   out.reserve(std::min<size_t>(out_size, 1u << 20));

   size_t pos = 4;

   while (out.size() < out_size)
   {
      if (pos >= size)
         return false;

      uint8_t flags = data[pos++];

      for (int f = 0; f != 8 && out.size() < out_size; ++f)
      {
         if (flags & 0x80)
         {
            if (pos + 1 >= size)
               return false;

            const uint16_t info = (uint16_t(data[pos]) << 8) | data[pos + 1];
            pos += 2;

            const uint32_t num = 3 + (info >> 12);
            const uint32_t disp = info & 0x0FFF;

            if (size_t(disp) + 1 > out.size())
               return false;

            const size_t src = out.size() - disp - 1;
            for (uint32_t p = 0; p != num && out.size() < out_size; ++p)
               out.push_back(out[src + p]);
         }
         else
         {
            if (pos >= size)
               return false;

            out.push_back(data[pos++]);
         }

         flags <<= 1;
      }
   }

   return true;
}

inline bool Decompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
   return Decompress(in.empty() ? nullptr : &in[0], in.size(), out);
}

}  // namespace wb::lz77

namespace wb {
   class LZ77Decompressor
   {
   public:
      LZ77Decompressor(std::istream& in)
      {
         constexpr uint32_t TYPE_LZ77 = 1;

         uint32_t magic;
         in >> BE >> magic;

         if (magic != BINARY_MAGIC_LZ77)
         {
            // LZ77 header not present
            in.seekg(-4, std::ios::cur);
            ret_stream = &in;
            return;
         }

         ret_stream = &data;

         uint32_t hdr;
         in >> LE >> hdr;

         const uint32_t uncompressed_length = hdr >> 8;
         const uint32_t compression_type = hdr >> 4 & 0xf;

         if (TYPE_LZ77 != compression_type)
            return;

         uint32_t written = 0;
         while (written != uncompressed_length)
         {
            uint8_t flags = in.get();
            for (int f = 0; f != 8; ++f)
            {
               if (flags & 0x80)
               {
                  uint16_t info;
                  in >> BE >> info;

                  const uint8_t num = 3 + (info >> 12);
                  const uint16_t disp = info & 0xFFF;
                  uint32_t ptr = written - disp - 1;

                  data.seekg(ptr, std::ios::beg);
                  for (uint8_t p = 0; p != num; ++p)
                  {
                     char c;
                     data.get(c);
                     data.put(c);
                     ++written;

                     if (written == uncompressed_length)
                        break;
                  }
               }
               else
               {
                  char c;
                  in.get(c);
                  data.put(c);
                  ++written;
               }

               flags <<= 1;

               if (written == uncompressed_length)
                  break;
            }
         }

         data.seekg(0, std::ios::beg);
      }

      std::istream& GetStream()
      {
         return *ret_stream;
      }

   private:
      std::stringstream data;
      std::istream* ret_stream;
   };
}
#endif
