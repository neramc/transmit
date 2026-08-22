#include "format/codec/Codec.h"

#include <array>

#include "format/codec/DeflateCodec.h"
#include "format/codec/StoreCodec.h"
#include "format/codec/XzCodec.h"
#include "format/codec/ZstdCodec.h"

namespace transmit::format {
namespace {

const StoreCodec kStore;
const DeflateCodec kDeflate;
const ZstdCodec kZstd;
#ifdef TRANSMIT_HAVE_LZMA
const XzCodec kXz;
#endif

}  // namespace

std::string_view codecName(CodecId id) noexcept {
    switch (id) {
        case CodecId::Store:
            return "store";
        case CodecId::Deflate:
            return "deflate";
        case CodecId::Zstd:
            return "zstd";
        case CodecId::Xz:
            return "xz";
    }
    return "unknown";
}

std::string_view presetName(CompressionPreset preset) noexcept {
    switch (preset) {
        case CompressionPreset::Fast:
            return "fast";
        case CompressionPreset::Balanced:
            return "balanced";
        case CompressionPreset::Maximum:
            return "maximum";
        case CompressionPreset::Extreme:
            return "extreme";
    }
    return "maximum";
}

Result<CompressionPreset> presetFromName(std::string_view name) {
    if (name == "fast")
        return CompressionPreset::Fast;
    if (name == "balanced")
        return CompressionPreset::Balanced;
    if (name == "maximum")
        return CompressionPreset::Maximum;
    if (name == "extreme")
        return CompressionPreset::Extreme;
    return makeError(ErrorCode::InvalidArgument, "unknown compression preset '", std::string(name),
                     "' (expected fast, balanced, maximum or extreme)");
}

CompressionProfile CompressionProfile::fromPreset(CompressionPreset preset) noexcept {
    switch (preset) {
        case CompressionPreset::Fast:
            return {CodecId::Zstd, 6, false};
        case CompressionPreset::Balanced:
            return {CodecId::Zstd, 12, false};
        case CompressionPreset::Maximum:
            return {CodecId::Zstd, 22, true};
        case CompressionPreset::Extreme:
            return {CodecId::Xz, 9, true};
    }
    return {};
}

const ICodec* findCodec(CodecId id) noexcept {
    switch (id) {
        case CodecId::Store:
            return &kStore;
        case CodecId::Deflate:
            return &kDeflate;
        case CodecId::Zstd:
            return &kZstd;
        case CodecId::Xz:
#ifdef TRANSMIT_HAVE_LZMA
            return &kXz;
#else
            return nullptr;
#endif
    }
    return nullptr;
}

bool isCodecAvailable(CodecId id) noexcept {
    return findCodec(id) != nullptr;
}

}  // namespace transmit::format
