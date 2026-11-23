#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace WorldRPG::Utf8 {
// JNI NewStringUTF accepts modified UTF-8, not ordinary UTF-8 (notably emoji).
// Decode explicitly, replacing malformed, overlong and surrogate sequences.
inline std::u16string ToUtf16(std::string_view text) {
    std::u16string out;
    for (size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        uint32_t code = lead;
        size_t count = 1;
        bool valid = true;
        if (lead < 0x80) {}
        else if (lead >= 0xC2 && lead <= 0xDF) { count = 2; code = lead & 0x1F; }
        else if (lead >= 0xE0 && lead <= 0xEF) { count = 3; code = lead & 0x0F; }
        else if (lead >= 0xF0 && lead <= 0xF4) { count = 4; code = lead & 0x07; }
        else valid = false;
        if (i + count > text.size()) valid = false;
        for (size_t j = 1; valid && j < count; ++j) {
            const auto next = static_cast<unsigned char>(text[i + j]);
            if ((next & 0xC0) != 0x80) valid = false;
            else code = (code << 6) | (next & 0x3F);
        }
        if ((count == 2 && code < 0x80) || (count == 3 && code < 0x800) || (count == 4 && code < 0x10000) ||
            code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) valid = false;
        if (!valid) { out.push_back(u'\uFFFD'); ++i; continue; }
        if (code >= 0x10000) {
            code -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (code >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
        } else out.push_back(static_cast<char16_t>(code));
        i += count;
    }
    return out;
}

// Android input is UTF-16. Pair surrogates before encoding ordinary UTF-8;
// GetStringUTFChars would encode each surrogate separately (modified UTF-8).
inline std::string FromUtf16(std::u16string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t code = text[i];
        if (code >= 0xD800 && code <= 0xDBFF) {
            if (i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
                code = 0x10000 + ((code - 0xD800) << 10) + (text[++i] - 0xDC00);
            else code = 0xFFFD;
        } else if (code >= 0xDC00 && code <= 0xDFFF) code = 0xFFFD;
        if (code < 0x80) out.push_back(static_cast<char>(code));
        else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    return out;
}
}
