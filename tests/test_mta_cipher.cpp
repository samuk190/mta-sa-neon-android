#include "MtaCipher.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
using namespace WorldRPG::Net::Mta;
static void Check(bool value, const char* name) { if (!value) throw std::runtime_error(name); }
static Bytes Hex(const std::string& hex) {
    Bytes bytes;
    for (std::size_t i = 0; i < hex.size(); i += 2) bytes.push_back(std::stoul(hex.substr(i, 2), nullptr, 16));
    return bytes;
}
int main() {
    try {
        Bytes key(16), plain(32);
        std::iota(key.begin(), key.end(), 0);
        std::iota(plain.begin(), plain.end(), 0);
        const auto cipher = Cipher::Create(key);
        Check(cipher.has_value(), "synthetic key accepted");
        // Independent Python reference calculated on Oracle. Synthetic key,
        // not captured session material. CRC also checked against binascii.
        const std::array<const char*,3> vectors{
            "d066002cba1c523476f1a79bdd7eb25f856937af654b7d8ab6b137472472456cba803a",
            "1b390579139dc23b32db4063b80f7894963966d5aff7e25b0dbaeedcf90dd3a531ad30",
            "cddfffbd0eb9e7a6d6f9833752f53070fba36b5e8233352878c36dc6c2a53ff9049a76"};
        const std::array<std::uint8_t,3> selectors{0,5,255};
        for (std::size_t i = 0; i < vectors.size(); ++i) {
            const auto vector = Hex(vectors[i]);
            Check(cipher->Seal(plain, selectors[i]) == vector, "known-answer encryption and CRC");
            auto decoded = cipher->Open(vector);
            Check(std::get<Bytes>(decoded) == plain, "known-answer decode");
            auto corrupt = vector; corrupt.back() ^= 1;
            Check(std::get<CipherError>(cipher->Open(corrupt)) == CipherError::ChecksumMismatch, "reject corruption");
            Bytes prefix{0x11,0x22}; auto prefixed = vector; prefixed.insert(prefixed.begin(), prefix.begin(), prefix.end());
            Check(std::get<Bytes>(cipher->Open(prefixed, prefix)) == plain, "negotiated input prefix");
            prefixed[0] ^= 1;
            Check(std::get<CipherError>(cipher->Open(prefixed, prefix)) == CipherError::PrefixMismatch, "wrong prefix");
            // Per-packet transform does not mutate the permutation.
            Check(cipher->Seal(plain, selectors[i]) == vector, "repeat selector is deterministic");
        }
        const std::string standard = "123456789";
        Check(Cipher::Checksum({reinterpret_cast<const std::uint8_t*>(standard.data()), standard.size()}) == 0x31c3, "CRC XMODEM check value");
        Check(Cipher::Checksum({}) == 0, "empty CRC");
        Check(std::get<Bytes>(cipher->Open(cipher->Seal({}, 7))).empty(), "empty payload");
        Check(std::holds_alternative<CipherError>(cipher->Open(Bytes(2))), "truncated envelope");
        Check(std::holds_alternative<CipherError>(cipher->Open(Bytes(65508))), "oversize envelope");
        Check(!Cipher::Create(Bytes(65)), "key length bound");
        const auto emptyKey = Cipher::Create({});
        Check(emptyKey->Seal(plain, 0) == Hex("2a7a000003070a0a111d24243f4b465665798888bbafc2f2e9053c5c47638eaecdf110"), "empty key follows native no-KSA path");
        std::cout << "PASS MTA cipher known-answer and malformed envelope tests\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
