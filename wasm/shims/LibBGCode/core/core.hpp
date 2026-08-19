/* LibBGCode core pakaitalas WASM'ui.
 *
 * Binarinis G-code yra FDM dalykas. SLA keliui jis nereikalingas - i grandine
 * patenka tik per bendra `Config.cpp` (jis moka skaityti config'a is .bgcode
 * failo). Cia visi tipai yra tikri pagal forma, bet skaitymas visada grazina
 * ReadError, tad tas kelias tiesiog niekada nepasileidzia.
 */
#ifndef SLIC3R_WASM_BGCODE_CORE_HPP
#define SLIC3R_WASM_BGCODE_CORE_HPP
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstddef>
#include <cstdio>

namespace bgcode { namespace core {

enum class EResult : uint16_t { Success = 0, ReadError = 1, WriteError = 2, InvalidMagicNumber = 3 };
enum class EBlockType : uint16_t { FileMetadata = 0, GCode = 1, SlicerMetadata = 2, PrinterMetadata = 3, PrintMetadata = 4, Thumbnail = 5 };
enum class ECompressionType : uint16_t { None = 0 };
enum class EChecksumType : uint16_t { None = 0, CRC32 = 1 };
enum class EMetadataEncodingType : uint16_t { INI = 0 };
enum class EGCodeEncodingType : uint16_t { None = 0 };
enum class EThumbnailFormat : uint16_t { PNG = 0, JPG = 1, QOI = 2 };

inline std::string translate_result(EResult) { return "binarinis G-code sioje versijoje neskaitomas"; }

struct FileHeader { uint32_t magic = 0, version = 0; uint16_t checksum_type = 0; };
struct BinaryData { std::vector<uint8_t> bytes; };
struct BlockHeader {
    uint16_t type = 0, compression = 0;
    uint32_t uncompressed_size = 0, compressed_size = 0;
};
struct BaseMetadataBlock {
    uint16_t encoding_type = 0;
    std::vector<std::pair<std::string, std::string>> raw_data;
    EResult read_data(FILE &, const FileHeader &, const BlockHeader &) { return EResult::ReadError; }
};
struct SlicerMetadataBlock : BaseMetadataBlock {};
struct PrinterMetadataBlock : BaseMetadataBlock {};
struct PrintMetadataBlock : BaseMetadataBlock {};
struct FileMetadataBlock : BaseMetadataBlock {};

template<class... A> inline EResult is_valid_binary_gcode(FILE &, A &&...) { return EResult::ReadError; }
template<class... A> inline EResult read_header(FILE &, FileHeader &, A &&...) { return EResult::ReadError; }
template<class... A> inline EResult read_next_block_header(FILE &, const FileHeader &, BlockHeader &, A &&...) { return EResult::ReadError; }

}} // namespace bgcode::core
#endif
