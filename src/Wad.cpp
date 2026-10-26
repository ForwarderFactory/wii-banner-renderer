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

namespace wb {
    static const uint8_t WII_COMMON_KEY[16] = {
        0xEB, 0xE4, 0x2A, 0x22, 0x5E, 0x85, 0x93, 0xE4,
        0x48, 0xD9, 0xC5, 0x45, 0x73, 0x81, 0xAA, 0xF7
    };

    struct TmdContent {
        uint32_t id;
        uint16_t index;
        uint16_t type;
        uint64_t size;
    };

    struct WadSections {
        std::vector<uint8_t> cert;
        std::vector<uint8_t> tik;
        std::vector<uint8_t> tmd;
        std::vector<uint8_t> app;
        std::vector<uint8_t> trailer;

        uint32_t cert_len{};
        uint32_t tik_len{};
        uint32_t tmd_len{};
        uint32_t app_len{};
        uint32_t trailer_len{};
    };

    std::vector<uint8_t> aes_cbc_decrypt(const std::vector<uint8_t>& data, const uint8_t key[16], const uint8_t iv[16]) {
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

        std::vector<uint8_t> out(data.size());
        if (!data.empty()) {
            ret = wc_AesCbcDecrypt(&aes, out.data(), data.data(), (uint32_t)data.size());
            if (ret != 0) {
                wc_AesFree(&aes);
                throw std::runtime_error("wc_AesCbcDecrypt failed");
            }
        }

        wc_AesFree(&aes);
        return out;
    }

    uint32_t be32(const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }

    uint64_t be64(const uint8_t* p) {
        return (uint64_t(be32(p)) << 32) | be32(p + 4);
    }

    size_t round_up(size_t x, size_t align) {
        return (x + align - 1) & ~(align - 1);
    }

    std::vector<uint8_t> read_block(std::ifstream& in, size_t len) {
        size_t padded = round_up(len, 0x40);
        std::vector<uint8_t> buf(padded);

        if (len > 0) {
            in.read(reinterpret_cast<char*>(buf.data()), padded);
            if (!in)
                throw std::runtime_error("failed to read block");
        }

        return buf;
    }

    void write_file(const std::filesystem::path& path, const std::vector<uint8_t>& data, size_t real_size) {
        std::ofstream out(path, std::ios::binary);
        if (!out)
            throw std::runtime_error("failed to open: " + path.string());

        out.write(reinterpret_cast<const char*>(data.data()), real_size);
    }

    std::vector<TmdContent> parse_tmd_contents(const std::vector<uint8_t>& tmd) {
        uint16_t count = (tmd[0x1DE] << 8) | tmd[0x1DF];

        std::vector<TmdContent> out;
        size_t offset = 0x1E4;

        for (uint16_t i = 0; i < count; i++) {
            const uint8_t* e = tmd.data() + offset + i * 36;

            TmdContent c{};
            c.id    = be32(e + 0x00);
            c.index = (e[4] << 8) | e[5];
            c.type  = (e[6] << 8) | e[7];

            c.size =
                (uint64_t(e[8]) << 56) | (uint64_t(e[9]) << 48) |
                (uint64_t(e[10]) << 40) | (uint64_t(e[11]) << 32) |
                (uint64_t(e[12]) << 24) | (uint64_t(e[13]) << 16) |
                (uint64_t(e[14]) << 8)  |  uint64_t(e[15]);

            out.push_back(c);
        }

        return out;
    }

    std::array<uint8_t, 16> get_title_key(const std::vector<uint8_t>& tik)
    {
        const uint8_t* enc_key = tik.data() + 0x1BF;
        const uint8_t* title_id = tik.data() + 0x1DC;

        uint8_t iv[16]{};
        std::memcpy(iv, title_id, 8);

        std::vector<uint8_t> encrypted(enc_key, enc_key + 16);

        auto dec = aes_cbc_decrypt(
            encrypted,
            WII_COMMON_KEY,
            iv
        );

        std::array<uint8_t, 16> key{};

        if (!dec.empty())
            std::memcpy(key.data(), dec.data(), 16);

        return key;
    }

    WadSections parse_install(std::ifstream& in, const std::vector<uint8_t>& header) {
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

            std::vector<uint8_t> encrypted(
                s.app.begin() + offset,
                s.app.begin() + offset + padded
            );

            uint8_t content_index[16] = {};
            content_index[0] = (c.index >> 8) & 0xFF;
            content_index[1] = c.index & 0xFF;

            auto decrypted = aes_cbc_decrypt(encrypted, title_key.data(), content_index);

            decrypted.resize(c.size); // trim padding

            std::filesystem::path path = dir / std::format("{:08x}.app", c.id);

            write_file(path, decrypted, decrypted.size());

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

            std::vector<uint8_t> encrypted(
                s.app.begin() + offset,
                s.app.begin() + offset + padded
            );

            uint8_t content_index[16] = {};
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
        std::vector<uint8_t> header(0x80);

        in.read(reinterpret_cast<char*>(header.data()), 0x40);
        if (!in) throw std::runtime_error("failed to read WAD header");

        uint32_t header_len = be32(header.data());

        if (header_len >= 0x80)
            throw std::runtime_error("header too large");

        if (header_len >= 0x40) {
            in.read(reinterpret_cast<char*>(header.data() + 0x40), 0x40);
            if (!in)
                throw std::runtime_error("failed to read extended header");
        }

        uint32_t type = be32(header.data() + 4);

        if (type != 0x49730000 && type != 0x69620000)
            throw std::runtime_error(std::format("unknown header type {:08x}", type));

        auto s = parse_install(in, header);

        uint64_t title_id = be64(s.tmd.data() + 0x18C);
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
    }

    std::vector<Wad::ExtractedFile> Wad::extract_wad(std::ifstream& in)
    {
        std::vector<uint8_t> header(0x80);

        in.read(reinterpret_cast<char*>(header.data()), 0x40);
        if (!in)
            throw std::runtime_error("failed to read WAD header");

        uint32_t header_len = be32(header.data());

        if (header_len >= 0x80)
            throw std::runtime_error("header too large");

        if (header_len >= 0x40) {
            in.read(reinterpret_cast<char*>(header.data() + 0x40), 0x40);

            if (!in)
                throw std::runtime_error("failed to read extended header");
        }

        uint32_t type = be32(header.data() + 4);

        if (type != 0x49730000 && type != 0x69620000)
            throw std::runtime_error(
                std::format("unknown header type {:08x}", type));

        auto s = parse_install(in, header);

        uint64_t title_id = be64(s.tmd.data() + 0x18C);

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
}