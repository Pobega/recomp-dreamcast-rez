#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dream/runtime/gdrom/disc.h"
#include "dream/runtime/sha1.h"

namespace {

constexpr const char* kExpectedBootSha1 = "62f27393c3170001dc987f3ae836461bd63dbd7a";
constexpr const char* kExpectedJpChdSha1 = "9711a0d0c21af3685672b9a42c72abf999fc7150";
constexpr const char* kExpectedEuChdSha1 = "01b7c1cc29a840b141a4ae713037eb1224fac233";

constexpr std::size_t kChunkBytes = 65536;
constexpr std::uint64_t kInitialCipherState = 0x62f27393c3170001ULL;
constexpr std::uint64_t kSplitMixGamma = 0x9e3779b97f4a7c15ULL;
constexpr std::uint64_t kSplitMixMul1 = 0xbf58476d1ce4e5b9ULL;
constexpr std::uint64_t kSplitMixMul2 = 0x94d049bb133111ebULL;

std::uint64_t read_le64(const std::uint8_t* ptr) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
        value |= static_cast<std::uint64_t>(ptr[i]) << (i * 8);
    }
    return value;
}

std::uint64_t next_keystream_word(std::uint64_t& state, const std::uint8_t* boot_data,
                                  std::size_t boot_size, std::size_t word_index) {
    const std::size_t aligned_boot_size = boot_size & ~std::size_t(7);
    const std::size_t byte_offset = (word_index * sizeof(std::uint64_t)) % aligned_boot_size;
    const std::uint64_t boot_word = read_le64(boot_data + byte_offset);

    state += kSplitMixGamma ^ boot_word;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * kSplitMixMul1;
    z = (z ^ (z >> 27)) * kSplitMixMul2;
    return z ^ (z >> 31);
}

bool read_file_bytes(const std::filesystem::path& path, std::vector<std::uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const std::streamsize size = file.tellg();
    if (size < 0)
        return false;
    out.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    return file.read(reinterpret_cast<char*>(out.data()), size).good();
}

int handle_extract_verify(const std::filesystem::path& chd_path,
                          const std::filesystem::path& out_boot_path) {
    std::string err;
    const std::string chd_sha1 = dream::gdrom::chd_header_sha1(chd_path, err);
    if (chd_sha1.empty()) {
        std::cerr << "rez-patch: error reading CHD header: " << err << "\n";
        return 1;
    }

    if (chd_sha1 != kExpectedJpChdSha1 && chd_sha1 != kExpectedEuChdSha1) {
        std::cerr << "rez-patch: unsupported disc image SHA-1: " << chd_sha1 << "\n"
                  << "  Expected Japanese (" << kExpectedJpChdSha1 << ")\n"
                  << "  or European (" << kExpectedEuChdSha1 << ")\n";
        return 1;
    }

    std::cout << "rez-patch: disc header SHA-1 verified ("
              << (chd_sha1 == kExpectedJpChdSha1 ? "Japanese" : "European") << ")\n";

    std::unique_ptr<dream::gdrom::Disc> disc = dream::gdrom::open_disc(chd_path, err);
    if (!disc) {
        std::cerr << "rez-patch: failed to open disc image: " << err << "\n";
        return 1;
    }

    std::vector<std::uint8_t> boot_bytes;
    std::string boot_name;
    if (!dream::gdrom::read_root_file(*disc, "", boot_bytes, &boot_name)) {
        std::cerr << "rez-patch: failed to read boot executable from disc image\n";
        return 1;
    }

    const std::string boot_sha1 = dream::sha1_hex(boot_bytes.data(), boot_bytes.size());
    if (boot_sha1 != kExpectedBootSha1) {
        std::cerr << "rez-patch: extracted boot file SHA-1 mismatch (" << boot_sha1
                  << ", expected " << kExpectedBootSha1 << ")\n";
        return 1;
    }

    std::cout << "rez-patch: extracted " << boot_name << " (" << boot_bytes.size()
              << " bytes, SHA-1 " << boot_sha1 << ")\n";

    std::error_code ec;
    if (out_boot_path.has_parent_path()) {
        std::filesystem::create_directories(out_boot_path.parent_path(), ec);
        if (ec) {
            std::cerr << "rez-patch: failed to create parent directories: " << ec.message() << "\n";
            return 1;
        }
    }

    std::ofstream out_file(out_boot_path, std::ios::binary);
    if (!out_file) {
        std::cerr << "rez-patch: failed to create " << out_boot_path << "\n";
        return 1;
    }

    out_file.write(reinterpret_cast<const char*>(boot_bytes.data()),
                   static_cast<std::streamsize>(boot_bytes.size()));
    if (!out_file.good()) {
        std::cerr << "rez-patch: failed to write boot executable to " << out_boot_path << "\n";
        return 1;
    }

    std::cout << "rez-patch: successfully saved boot executable to " << out_boot_path << "\n";
    return 0;
}

int handle_crypt(const std::filesystem::path& boot_path, const std::filesystem::path& in_path,
                 const std::string& out_path_or_dash) {
    std::vector<std::uint8_t> boot_bytes;
    if (!read_file_bytes(boot_path, boot_bytes)) {
        std::cerr << "rez-patch: failed to read boot file: " << boot_path << "\n";
        return 1;
    }

    if (boot_bytes.size() < sizeof(std::uint64_t)) {
        std::cerr << "rez-patch: boot file too small\n";
        return 1;
    }

    const std::string boot_sha1 = dream::sha1_hex(boot_bytes.data(), boot_bytes.size());
    if (boot_sha1 != kExpectedBootSha1) {
        std::cerr << "rez-patch: invalid boot executable SHA-1 (" << boot_sha1
                  << ", expected " << kExpectedBootSha1 << ")\n";
        return 1;
    }

    std::ifstream in_file(in_path, std::ios::binary);
    if (!in_file) {
        std::cerr << "rez-patch: failed to open input file: " << in_path << "\n";
        return 1;
    }

    FILE* out_fp = nullptr;
    const bool write_to_stdout = (out_path_or_dash == "-");
    if (write_to_stdout) {
        out_fp = stdout;
    } else {
        out_fp = std::fopen(out_path_or_dash.c_str(), "wb");
        if (!out_fp) {
            std::cerr << "rez-patch: failed to create output file: " << out_path_or_dash << "\n";
            return 1;
        }
    }

    std::uint64_t state = kInitialCipherState;
    std::size_t stream_pos = 0;
    std::vector<std::uint8_t> chunk(kChunkBytes);
    std::uint64_t current_word = 0;

    while (in_file) {
        in_file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        const std::size_t bytes_read = static_cast<std::size_t>(in_file.gcount());
        if (bytes_read == 0)
            break;

        for (std::size_t i = 0; i < bytes_read; ++i) {
            const std::size_t pos = stream_pos + i;
            if ((pos & 7) == 0) {
                current_word = next_keystream_word(state, boot_bytes.data(), boot_bytes.size(), pos / 8);
            }
            chunk[i] ^= static_cast<std::uint8_t>((current_word >> ((pos & 7) * 8)) & 0xFF);
        }
        stream_pos += bytes_read;

        if (std::fwrite(chunk.data(), 1, bytes_read, out_fp) != bytes_read) {
            std::cerr << "rez-patch: write error\n";
            if (!write_to_stdout)
                std::fclose(out_fp);
            return 1;
        }
    }

    if (!write_to_stdout)
        std::fclose(out_fp);

    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc >= 4 && std::string_view(argv[1]) == "--extract-verify") {
        return handle_extract_verify(argv[2], argv[3]);
    }
    if (argc >= 5 && std::string_view(argv[1]) == "--crypt") {
        return handle_crypt(argv[2], argv[3], argv[4]);
    }

    std::cerr << "Usage:\n"
              << "  rez-patch --extract-verify <disc.chd> <out_1st_read.bin>\n"
              << "  rez-patch --crypt <1st_read.bin> <in_file> <out_file_or_dash>\n";
    return 1;
}
