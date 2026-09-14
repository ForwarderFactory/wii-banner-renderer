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

#ifndef WII_BNR_YAZ0_H_
#define WII_BNR_YAZ0_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace yaz0
{

inline bool IsCompressed(const uint8_t* data, size_t size)
{
   return size > 0x10 && data[0] == 'Y' && data[1] == 'a' &&
          data[2] == 'z' && data[3] == '0';
}

inline bool Decompress(const uint8_t* data, size_t size, std::vector<uint8_t>& out)
{
   out.clear();

   if (!IsCompressed(data, size))
      return false;

   const uint32_t out_size =
      (uint32_t(data[4]) << 24) | (uint32_t(data[5]) << 16) |
      (uint32_t(data[6]) <<  8) |  uint32_t(data[7]);

   out.reserve(std::min<size_t>(out_size, 1u << 20));

   size_t pos = 0x10;
   uint8_t code = 0;
   uint32_t code_bits = 0;

   while (out.size() < out_size)
   {
      if (code_bits == 0)
      {
         if (pos >= size)
            return false;

         code = data[pos++];
         code_bits = 8;
      }

      if (code & 0x80)
      {
         if (pos >= size)
            return false;

         out.push_back(data[pos++]);
      }
      else
      {
         if (pos + 1 >= size)
            return false;

         const uint8_t b0 = data[pos++];
         const uint8_t b1 = data[pos++];

         const size_t distance = ((size_t(b0 & 0x0F) << 8) | b1) + 1;
         size_t count = b0 >> 4;

         if (count == 0)
         {
            if (pos >= size)
               return false;

            count = size_t(data[pos++]) + 0x12;
         }
         else
         {
            count += 2;
         }

         if (distance > out.size())
            return false;

         size_t src = out.size() - distance;
         for (size_t i = 0; i != count && out.size() < out_size; ++i)
            out.push_back(out[src + i]);
      }

      code <<= 1;
      --code_bits;
   }

   return true;
}

inline bool Decompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
   return Decompress(in.empty() ? NULL : &in[0], in.size(), out);
}

}  // namespace yaz0

#endif
