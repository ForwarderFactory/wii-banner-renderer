#include <vector>
#include <fstream>
#include <filesystem>
#include <format>
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <cstring>

#include <libwb/Wad.h>

#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/aes.h>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

static const u8 WII_COMMON_KEY[16] = {
    0xEB, 0xE4, 0x2A, 0x22, 0x5E, 0x85, 0x93, 0xE4,
    0x48, 0xD9, 0xC5, 0x45, 0x73, 0x81, 0xAA, 0xF7
};

struct TmdContent {
    u32 id;
    u16 index;
    u16 type;
    u64 size;
};

struct WadSections {
    std::vector<u8> cert;
    std::vector<u8> tik;
    std::vector<u8> tmd;
    std::vector<u8> app;
    std::vector<u8> trailer;

    u32 cert_len{};
    u32 tik_len{};
    u32 tmd_len{};
    u32 app_len{};
    u32 trailer_len{};
};

std::vector<u8> aes_cbc_decrypt(const std::vector<u8>& data, const u8 key[16], const u8 iv[16]) {
    if (data.size() % AES_BLOCK_SIZE != 0)
        throw std::runtime_error("ciphertext not block-aligned");

    Aes aes;
    int ret = wc_AesInit(&aes, nullptr, INVALID_DEVID);
    if (ret != 0) throw std::runtime_error("wc_AesInit failed");

    ret = wc_AesSetKey(&aes, key, 16, iv, AES_DECRYPTION);
    if (ret != 0) {
        wc_AesFree(&aes);
        throw std::runtime_error("wc_AesSetKey failed");
    }

    std::vector<u8> out(data.size());
    if (!data.empty()) {
        ret = wc_AesCbcDecrypt(&aes, out.data(), data.data(), (u32)data.size());
        if (ret != 0) {
            wc_AesFree(&aes);
            throw std::runtime_error("wc_AesCbcDecrypt failed");
        }
    }

    wc_AesFree(&aes);
    return out;
}

u32 be32(const u8* p) {
    return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}

u64 be64(const u8* p) {
    return (u64(be32(p)) << 32) | be32(p + 4);
}

size_t round_up(size_t x, size_t align) {
    return (x + align - 1) & ~(align - 1);
}

std::vector<u8> read_block(std::ifstream& in, size_t len) {
    size_t padded = round_up(len, 0x40);
    std::vector<u8> buf(padded);

    if (len > 0) {
        in.read(reinterpret_cast<char*>(buf.data()), padded);
        if (!in)
            throw std::runtime_error("failed to read block");
    }

    return buf;
}

void write_file(const std::filesystem::path& path, const std::vector<u8>& data, size_t real_size) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error("failed to open: " + path.string());

    out.write(reinterpret_cast<const char*>(data.data()), real_size);
}

std::vector<TmdContent> parse_tmd_contents(const std::vector<u8>& tmd) {
    u16 count = (tmd[0x1DE] << 8) | tmd[0x1DF];

    std::vector<TmdContent> out;
    size_t offset = 0x1E4;

    for (u16 i = 0; i < count; i++) {
        const u8* e = tmd.data() + offset + i * 36;

        TmdContent c{};
        c.id    = be32(e + 0x00);
        c.index = (e[4] << 8) | e[5];
        c.type  = (e[6] << 8) | e[7];

        c.size =
            (u64(e[8]) << 56) | (u64(e[9]) << 48) |
            (u64(e[10]) << 40) | (u64(e[11]) << 32) |
            (u64(e[12]) << 24) | (u64(e[13]) << 16) |
            (u64(e[14]) << 8)  |  u64(e[15]);

        out.push_back(c);
    }

    return out;
}

std::array<u8, 16> get_title_key(const std::vector<u8>& tik)
{
    const u8* enc_key = tik.data() + 0x1BF;
    const u8* title_id = tik.data() + 0x1DC;

    u8 iv[16]{};
    std::memcpy(iv, title_id, 8);

    std::vector<u8> encrypted(enc_key, enc_key + 16);

    auto dec = aes_cbc_decrypt(
        encrypted,
        WII_COMMON_KEY,
        iv
    );

    std::array<u8, 16> key{};

    if (!dec.empty())
        std::memcpy(key.data(), dec.data(), 16);

    return key;
}

WadSections parse_install(std::ifstream& in, const std::vector<u8>& header) {
    if (be32(header.data()) != 0x20)
        throw std::runtime_error("invalid install header");

    WadSections s;

    s.cert_len    = be32(header.data() + 0x08);
    s.tik_len     = be32(header.data() + 0x10);
    s.tmd_len     = be32(header.data() + 0x14);
    s.app_len     = be32(header.data() + 0x18);
    s.trailer_len = be32(header.data() + 0x1C);

    s.cert    = read_block(in, s.cert_len);
    s.tik     = read_block(in, s.tik_len);
    s.tmd     = read_block(in, s.tmd_len);
    s.app     = read_block(in, s.app_len);
    s.trailer = read_block(in, s.trailer_len);

    return s;
}

void extract_contents_decrypted(const WadSections& s, const std::filesystem::path& dir) {
    auto contents = parse_tmd_contents(s.tmd);

    auto title_key = get_title_key(s.tik);

    size_t offset = 0;

    for (const auto& c : contents) {
        size_t padded = round_up(c.size, 0x40);

        if (offset + padded > s.app.size())
            throw std::runtime_error("Content exceeds app blob");

        std::vector<u8> encrypted(
            s.app.begin() + offset,
            s.app.begin() + offset + padded
        );

        u8 content_index[16] = {};
        content_index[0] = (c.index >> 8) & 0xFF;
        content_index[1] = c.index & 0xFF;

        auto decrypted = aes_cbc_decrypt(encrypted, title_key.data(), content_index);

        decrypted.resize(c.size); // trim padding

        std::filesystem::path path = dir / std::format("{:08x}.app", c.id);

        write_file(path, decrypted, decrypted.size());

        std::cout << "  -> " << path
                  << " (decrypted, " << c.size << " bytes)\n";

        offset += padded;
    }
}

void extract_contents_decrypted(
    const WadSections& s,
    std::vector<Wad::ExtractedFile>& files)
{
    auto contents = parse_tmd_contents(s.tmd);
    auto title_key = get_title_key(s.tik);

    size_t offset = 0;

    for (const auto& c : contents) {
        size_t padded = round_up(c.size, 0x40);

        if (offset + padded > s.app.size())
            throw std::runtime_error("Content exceeds app blob");

        std::vector<u8> encrypted(
            s.app.begin() + offset,
            s.app.begin() + offset + padded
        );

        u8 content_index[16] = {};
        content_index[0] = (c.index >> 8) & 0xFF;
        content_index[1] = c.index & 0xFF;

        auto decrypted = aes_cbc_decrypt(
            encrypted,
            title_key.data(),
            content_index
        );

        // Remove AES/WAD padding.
        decrypted.resize(c.size);

        files.push_back({
            std::format("{:08x}.app", c.id),
            std::move(decrypted)
        });

        offset += padded;
    }
}

void Wad::extract_wad(std::ifstream& in, const std::string& out_dir) {
    std::vector<u8> header(0x80);

    in.read(reinterpret_cast<char*>(header.data()), 0x40);
    if (!in) throw std::runtime_error("failed to read WAD header");

    u32 header_len = be32(header.data());

    if (header_len >= 0x80)
        throw std::runtime_error("header too large");

    if (header_len >= 0x40) {
        in.read(reinterpret_cast<char*>(header.data() + 0x40), 0x40);
        if (!in)
            throw std::runtime_error("failed to read extended header");
    }

    u32 type = be32(header.data() + 4);

    if (type != 0x49730000 && type != 0x69620000)
        throw std::runtime_error(std::format("unknown header type {:08x}", type));

    auto s = parse_install(in, header);

    u64 title_id = be64(s.tmd.data() + 0x18C);
    std::filesystem::path outdir = std::format("{:016x}", title_id);

    if (!outdir.empty()) {
        outdir = out_dir;
    }

    std::filesystem::create_directories(outdir);

    write_file(outdir / std::format("{:016x}.cert", title_id), s.cert, s.cert_len);
    write_file(outdir / std::format("{:016x}.tik",  title_id), s.tik,  s.tik_len);
    write_file(outdir / std::format("{:016x}.tmd",  title_id), s.tmd,  s.tmd_len);

    if (s.trailer_len > 0) {
        write_file(outdir / std::format("{:016x}.trailer", title_id),
                   s.trailer, s.trailer_len);
    }

    extract_contents_decrypted(s, outdir);
    std::cout << "Extracted: " << outdir << "\n";
}

std::vector<Wad::ExtractedFile> Wad::extract_wad(std::ifstream& in)
{
    std::vector<u8> header(0x80);

    in.read(reinterpret_cast<char*>(header.data()), 0x40);
    if (!in)
        throw std::runtime_error("failed to read WAD header");

    u32 header_len = be32(header.data());

    if (header_len >= 0x80)
        throw std::runtime_error("header too large");

    if (header_len >= 0x40) {
        in.read(reinterpret_cast<char*>(header.data() + 0x40), 0x40);

        if (!in)
            throw std::runtime_error("failed to read extended header");
    }

    u32 type = be32(header.data() + 4);

    if (type != 0x49730000 && type != 0x69620000)
        throw std::runtime_error(
            std::format("unknown header type {:08x}", type));

    auto s = parse_install(in, header);

    u64 title_id = be64(s.tmd.data() + 0x18C);

    std::vector<ExtractedFile> files;

    files.push_back({
        std::format("{:016x}.cert", title_id),
        s.cert
    });

    files.push_back({
        std::format("{:016x}.tik", title_id),
        s.tik
    });

    files.push_back({
        std::format("{:016x}.tmd", title_id),
        s.tmd
    });

    if (s.trailer_len > 0) {
        files.push_back({
            std::format("{:016x}.trailer", title_id),
            s.trailer
        });
    }

    extract_contents_decrypted(s, files);

    return files;
}