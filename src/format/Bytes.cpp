#include "format/Bytes.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace transmit::format {

void secureZero(void* data, std::size_t count) noexcept {
#if defined(_WIN32)
    ::SecureZeroMemory(data, count);
#else
    auto* p = static_cast<volatile unsigned char*>(data);
    for (std::size_t i = 0; i < count; ++i) {
        p[i] = 0;
    }
#endif
}

std::string toHex(ByteView bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (Byte b : bytes) {
        const auto value = static_cast<std::uint8_t>(b);
        out.push_back(kDigits[value >> 4]);
        out.push_back(kDigits[value & 0x0Fu]);
    }
    return out;
}

}  // namespace transmit::format
