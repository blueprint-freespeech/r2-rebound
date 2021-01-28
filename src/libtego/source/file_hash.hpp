#pragma once

//
// Tego File Hash
//

struct tego_file_hash
{
    tego_file_hash();
    // hash a blob of memory
    tego_file_hash(uint8_t const* begin, uint8_t const* end);
    // hash en entire stream, reads bytes into blocks and incrementally hashes
    tego_file_hash(std::istream& stream);

    size_t string_size() const;
    const std::string& to_string() const;

    // 512 bytes, 8 bits per byte
    constexpr static size_t SHA3_512_DIGEST_SIZE = 512 / 8;
    constexpr static size_t DIGEST_SIZE = SHA3_512_DIGEST_SIZE;
    std::array<uint8_t, DIGEST_SIZE> data;
    mutable std::string hex;
};