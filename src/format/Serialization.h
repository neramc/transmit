#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "format/Bytes.h"
#include "format/Result.h"

namespace transmit::format {

/// A compact tag/length encoding for the manifest.
///
/// Records are written as a sequence of (fieldNumber, wireType) varint tags
/// followed by the payload. A reader that meets an unknown field skips it by
/// wire type, so manifests written by a newer Transmit stay readable by an
/// older one. This is why the manifest is not plain JSON: an environment
/// capture routinely holds hundreds of thousands of entries, and the tagged
/// binary form is an order of magnitude smaller and faster to parse while
/// keeping forward compatibility.
enum class WireType : std::uint8_t {
    Varint = 0,   ///< unsigned/zig-zag integers, booleans, enums
    Fixed64 = 1,  ///< doubles and 64-bit fixed fields
    Bytes = 2,    ///< strings, blobs and nested records
    Fixed32 = 3,
};

class ByteWriter {
public:
    explicit ByteWriter(ByteBuffer& sink) : sink_(sink) {}

    void putVarint(std::uint64_t value);
    void putSignedVarint(std::int64_t value);
    void putFixed32(std::uint32_t value);
    void putFixed64(std::uint64_t value);
    void putDouble(double value);
    void putRaw(ByteView data);

    void putTag(std::uint32_t field, WireType type);
    void putUInt(std::uint32_t field, std::uint64_t value);
    void putInt(std::uint32_t field, std::int64_t value);
    void putBool(std::uint32_t field, bool value);
    void putString(std::uint32_t field, std::string_view value);
    void putBytes(std::uint32_t field, ByteView value);
    void putDoubleField(std::uint32_t field, double value);

    /// Writes a nested record: the callback fills a temporary buffer which is
    /// then emitted as a length-delimited field.
    template <typename Fn>
    void putRecord(std::uint32_t field, Fn&& fill) {
        ByteBuffer nested;
        ByteWriter writer(nested);
        fill(writer);
        putBytes(field, nested);
    }

    [[nodiscard]] std::size_t size() const noexcept { return sink_.size(); }

private:
    ByteBuffer& sink_;
};

class ByteReader {
public:
    explicit ByteReader(ByteView data) : data_(data) {}

    [[nodiscard]] bool atEnd() const noexcept { return offset_ >= data_.size(); }
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - offset_; }

    Result<std::uint64_t> getVarint();
    Result<std::int64_t> getSignedVarint();
    Result<std::uint32_t> getFixed32();
    Result<std::uint64_t> getFixed64();
    Result<double> getDouble();
    Result<ByteView> getRaw(std::size_t count);

    struct Tag {
        std::uint32_t field = 0;
        WireType type = WireType::Varint;
    };

    Result<Tag> getTag();
    Result<std::string> getString();
    Result<ByteView> getBytes();
    Result<bool> getBool();

    /// Skips the payload of a field whose number the caller does not know.
    Status skip(WireType type);

private:
    ByteView data_;
    std::size_t offset_ = 0;
};

}  // namespace transmit::format
