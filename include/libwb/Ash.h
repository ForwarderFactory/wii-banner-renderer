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

#ifndef WII_BNR_ASH_H_
#define WII_BNR_ASH_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ash
{

inline bool IsCompressed(const uint8_t* data, size_t size)
{
   return size > 0x10 && data[0] == 'A' && data[1] == 'S' && data[2] == 'H';
}

namespace detail
{

class BitReader
{
public:
   BitReader(const uint8_t* data, size_t size, size_t offset)
      : data(data), size(size), pos(offset), word(0), used(0), bad(false)
   {
      word = NextWord();
   }

   uint32_t ReadBit()
   {
      const uint32_t bit = word >> 31;

      if (used == 31)
      {
         word = NextWord();
         used = 0;
      }
      else
      {
         word <<= 1;
         ++used;
      }

      return bit;
   }

   uint32_t ReadBits(uint32_t count)
   {
      if (count == 0 || count > 32)
         return 0;

      const uint32_t total = used + count;
      uint32_t value = word >> (32 - count);

      if (total < 32)
      {
         word <<= count;
         used = total;
      }
      else if (total == 32)
      {
         word = NextWord();
         used = 0;
      }
      else
      {
         word = NextWord();
         used = total - 32;
         value |= word >> (32 - used);
         word <<= used;
      }

      return value;
   }

   bool Bad() const { return bad; }

private:
   uint32_t NextWord()
   {
      if (pos + 4 > size)
      {
         bad = true;
         return 0;
      }

      const uint32_t value =
         (uint32_t(data[pos + 0]) << 24) | (uint32_t(data[pos + 1]) << 16) |
         (uint32_t(data[pos + 2]) <<  8) |  uint32_t(data[pos + 3]);

      pos += 4;
      return value;
   }

   const uint8_t* data;
   size_t size;
   size_t pos;
   uint32_t word;
   uint32_t used;
   bool bad;
};

struct Tree
{
   std::vector<uint16_t> zero;
   std::vector<uint16_t> one;
   uint32_t root;
};

inline bool ReadTree(BitReader& in, uint32_t leaf_count, uint32_t symbol_bits, Tree& out)
{
   const uint32_t node_limit = leaf_count * 2 - 1;

   out.zero.assign(node_limit, 0);
   out.one.assign(node_limit, 0);
   out.root = 0;

   uint32_t next_node = leaf_count;
   uint32_t value = 0;

   std::vector<uint32_t> pending;
   pending.reserve(leaf_count);

   const uint32_t ONE_SIDE = 0x80000000u;

   for (;;)
   {
      if (in.Bad())
         return false;

      if (in.ReadBit())
      {
         if (next_node >= node_limit)
            return false;

         const uint32_t node = next_node++;
         pending.push_back(node | ONE_SIDE);
         pending.push_back(node);
         continue;
      }

      value = in.ReadBits(symbol_bits);

      for (;;)
      {
         if (pending.empty())
         {
            out.root = value;
            return !in.Bad();
         }

         const uint32_t slot = pending.back();
         pending.pop_back();
         const uint32_t node = slot & ~ONE_SIDE;

         if (slot & ONE_SIDE)
         {
            out.one[node] = uint16_t(value);
            value = node;
            continue;
         }

         out.zero[node] = uint16_t(value);
         break;
      }
   }
}

inline bool Walk(BitReader& in, const Tree& tree, uint32_t leaf_count, uint32_t& symbol)
{
   uint32_t node = tree.root;
   uint32_t guard = 0;

   while (node >= leaf_count)
   {
      if (node >= tree.zero.size() || in.Bad() || ++guard > leaf_count)
         return false;

      node = in.ReadBit() ? tree.one[node] : tree.zero[node];
   }

   symbol = node;
   return true;
}

}  // namespace detail

inline bool Decompress(const uint8_t* data, size_t size, std::vector<uint8_t>& out)
{
   out.clear();

   if (!IsCompressed(data, size))
      return false;

   const uint32_t out_size =
      ((uint32_t(data[4]) << 24) | (uint32_t(data[5]) << 16) |
       (uint32_t(data[6]) <<  8) |  uint32_t(data[7])) & 0x00FFFFFF;

   const uint32_t dist_offset =
      (uint32_t(data[8]) << 24) | (uint32_t(data[9]) << 16) |
      (uint32_t(data[10]) << 8) |  uint32_t(data[11]);

   if (out_size == 0 || dist_offset + 4 > size)
      return false;

   detail::BitReader sym_bits(data, size, 0x0C);
   detail::BitReader dist_bits(data, size, dist_offset);

   const uint32_t SYMBOL_COUNT = 0x200;
   const uint32_t DIST_COUNT = 0x800;

   detail::Tree sym_tree, dist_tree;
   if (!detail::ReadTree(sym_bits, SYMBOL_COUNT, 9, sym_tree))
      return false;
   if (!detail::ReadTree(dist_bits, DIST_COUNT, 11, dist_tree))
      return false;

   out.resize(out_size);
   size_t pos = 0;

   while (pos < out_size)
   {
      uint32_t symbol;
      if (!detail::Walk(sym_bits, sym_tree, SYMBOL_COUNT, symbol))
         return false;

      if (symbol < 0x100)
      {
         out[pos++] = uint8_t(symbol);
         continue;
      }

      uint32_t distance;
      if (!detail::Walk(dist_bits, dist_tree, DIST_COUNT, distance))
         return false;

      uint32_t length = symbol - 0xFD;
      if (size_t(distance) + 1 > pos)
         return false;

      size_t src = pos - distance - 1;
      if (length > out_size - pos)
         length = uint32_t(out_size - pos);

      for (uint32_t i = 0; i != length; ++i)
         out[pos + i] = out[src + i];

      pos += length;
   }

   return !sym_bits.Bad() && !dist_bits.Bad();
}

inline bool Decompress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
   return Decompress(in.empty() ? NULL : &in[0], in.size(), out);
}

}

#endif
