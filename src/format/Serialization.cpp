#include "format/Serialization.h"

#include <cstring>

namespace transmit::format {

void ByteWriter::putVarint(std::uint64_t value) {
    while (value >= 0x80u) {
        sink_.push_back(static_cast<Byte>((value & 0x7Fu) | 0x80u));
        value >>= 7;
    }
    sink_.push_back(static_cast<Byte>(value));
}

void ByteWriter::putSignedVarint(std::int64_t value) {
    // Zig-zag so small negative values stay short.
    const auto encoded = static_cast<std::uint64_t>((value << 1) ^ (value >> 63));
    putVarint(encoded);
}

void ByteWriter::putFixed32(std::uint32_t value) {
    appendLe<std::uint32_t>(sink_, value);
}

void ByteWriter::putFixed64(std::uint64_t value) {
    appendLe<std::uint64_t>(sink_, value);
}

void ByteWriter::putDouble(double value) {
    std::uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    putFixed64(raw);
}

void ByteWriter::putRaw(ByteView data) {
    sink_.insert(sink_.end(), data.begin(), data.end());
}

void ByteWriter::putTag(std::uint32_t field, WireType type) {
    putVarint((static_cast<std::uint64_t>(field) << 3) | static_cast<std::uint64_t>(type));
}

void ByteWriter::putUInt(std::uint32_t field, std::uint64_t value) {
    putTag(field, WireType::Varint);
    putVarint(value);
}

void ByteWriter::putInt(std::uint32_t field, std::int64_t value) {
    putTag(field, WireType::Varint);
    putSignedVarint(value);
}

void ByteWriter::putBool(std::uint32_t field, bool value) {
    putUInt(field, value ? 1u : 0u);
}

void ByteWriter::putString(std::uint32_t field, std::string_view value) {
    putBytes(field, asBytes(value));
}

void ByteWriter::putBytes(std::uint32_t field, ByteView value) {
    putTag(field, WireType::Bytes);
    putVarint(value.size());
    putRaw(value);
}

void ByteWriter::putDoubleField(std::uint32_t field, double value) {
    putTag(field, WireType::Fixed64);
    putDouble(value);
}

Result<std::uint64_t> ByteReader::getVarint() {
    std::uint64_t value = 0;
    unsigned shift = 0;
    while (true) {
        if (offset_ >= data_.size()) {
            return makeError(ErrorCode::EndOfStream, "truncated varint");
        }
        const auto byte = static_cast<std::uint8_t>(data_[offset_++]);
        if (shift > 63) {
            return makeError(ErrorCode::CorruptArchive, "varint exceeds 64 bits");
        }
        value |= static_cast<std::uint64_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) {
            return value;
        }
        shift += 7;
    }
}

Result<std::int64_t> ByteReader::getSignedVarint() {
    TRANSMIT_TRY(raw, getVarint());
    return static_cast<std::int64_t>((raw >> 1)) ^ -static_cast<std::int64_t>(raw & 1u);
}

Result<std::uint32_t> ByteReader::getFixed32() {
    TRANSMIT_TRY(raw, getRaw(sizeof(std::uint32_t)));
    return readLe<std::uint32_t>(raw);
}

Result<std::uint64_t> ByteReader::getFixed64() {
    TRANSMIT_TRY(raw, getRaw(sizeof(std::uint64_t)));
    return readLe<std::uint64_t>(raw);
}

Result<double> ByteReader::getDouble() {
    TRANSMIT_TRY(raw, getFixed64());
    double value = 0.0;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

Result<ByteView> ByteReader::getRaw(std::size_t count) {
    if (remaining() < count) {
        return makeError(ErrorCode::EndOfStream, "truncated record");
    }
    ByteView view = data_.subspan(offset_, count);
    offset_ += count;
    return view;
}

Result<ByteReader::Tag> ByteReader::getTag() {
    TRANSMIT_TRY(raw, getVarint());
    const auto type = static_cast<WireType>(raw & 0x07u);
    if (type > WireType::Fixed32) {
        return makeError(ErrorCode::CorruptArchive, "unknown wire type");
    }
    return Tag{static_cast<std::uint32_t>(raw >> 3), type};
}

Result<std::string> ByteReader::getString() {
    TRANSMIT_TRY(bytes, getBytes());
    return std::string(asText(bytes));
}

Result<ByteView> ByteReader::getBytes() {
    TRANSMIT_TRY(length, getVarint());
    return getRaw(static_cast<std::size_t>(length));
}

Result<bool> ByteReader::getBool() {
    TRANSMIT_TRY(value, getVarint());
    return value != 0;
}

Status ByteReader::skip(WireType type) {
    switch (type) {
        case WireType::Varint:
            TRANSMIT_CHECK(getVarint());
            return ok();
        case WireType::Fixed64:
            TRANSMIT_CHECK(getRaw(8));
            return ok();
        case WireType::Fixed32:
            TRANSMIT_CHECK(getRaw(4));
            return ok();
        case WireType::Bytes:
            TRANSMIT_CHECK(getBytes());
            return ok();
    }
    return makeError(ErrorCode::CorruptArchive, "cannot skip unknown wire type");
}

}  // namespace transmit::format
