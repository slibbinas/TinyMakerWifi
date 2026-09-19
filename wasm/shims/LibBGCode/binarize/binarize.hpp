/* Tuscias pakaitalas: LibBGCode yra BINARINIS G-CODE (FDM). SLA keliui jis
   nereikalingas - jis ateina tik per bendra Config.hpp include grandine. */
#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace bgcode { namespace binarize {
enum class ECompressionType : uint16_t { None = 0, Deflate = 1, Heatshrink_11_4 = 2, Heatshrink_12_4 = 3 };
enum class EChecksumType : uint16_t { None = 0, CRC32 = 1 };
enum class EMetadataEncodingType : uint16_t { INI = 0 };
enum class EGCodeEncodingType : uint16_t { None = 0, MeatPack = 1, MeatPackComments = 2 };
struct BinarizerConfig {
    struct Compression {
        ECompressionType file_metadata{ECompressionType::None};
        ECompressionType printer_metadata{ECompressionType::None};
        ECompressionType print_metadata{ECompressionType::None};
        ECompressionType slicer_metadata{ECompressionType::None};
        ECompressionType gcode{ECompressionType::None};
    };
    Compression compression;
    EGCodeEncodingType gcode_encoding{EGCodeEncodingType::None};
    EMetadataEncodingType metadata_encoding{EMetadataEncodingType::INI};
    EChecksumType checksum{EChecksumType::CRC32};
};
}} // namespace bgcode::binarize

#include <LibBGCode/core/core.hpp>
namespace bgcode { namespace binarize {
using BinaryData = ::bgcode::core::BinaryData;
using EThumbnailFormat = ::bgcode::core::EThumbnailFormat;
struct ThumbnailParams { uint16_t format = 0, width = 0, height = 0; };
struct ThumbnailBlock {
    ThumbnailParams params;
    std::vector<uint8_t> data;
};

class Binarizer {
public:
    bool is_enabled() const { return false; }
    void set_enabled(bool) {}
    BinaryData &get_binary_data() { return d_; }
    const BinaryData &get_binary_data() const { return d_; }
    ::bgcode::core::EResult initialize(FILE &, const BinarizerConfig &) { return ::bgcode::core::EResult::Success; }
    ::bgcode::core::EResult append_gcode(const std::string &) { return ::bgcode::core::EResult::Success; }
    ::bgcode::core::EResult finalize() { return ::bgcode::core::EResult::Success; }
private:
    BinaryData d_;
};
}}
