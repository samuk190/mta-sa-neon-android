#pragma once

// Port of the desktop SharedUtil crypto helpers used by CLuaCryptDefs
// (mta_neon_source/Shared/sdk/SharedUtil.Crypto.h, SharedUtil.Hash.hpp:513-551, 745-910).
// The bodies use the same vendored libraries as desktop: Crypto++ 8.9.0 and
// crypt_blowfish (third_party/cryptopp, third_party/bcrypt). Crypto++ headers stay in
// SharedCrypto.cpp so that only one translation unit pays for them.
//
// Functions that desktop lets throw CryptoPP::Exception throw CryptoError here, with
// the same message (Exception::GetWhat), so the Lua layer can log it as desktop does.

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace WorldRPG::Crypto {

struct CryptoError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct KeyPair {
    std::string publicKey, privateKey;
};

// ZLibFormat / ZLibStrategy (SharedUtil.Hash.h:71-85).
enum class ZLibFormat { ZRAW = -15, ZLIB = 15, GZIP = 31 };
enum class ZLibStrategy { DEFAULT, FILTERED, HUFFMAN_ONLY, RLE, FIXED };

// Base64 / Base32 with the desktop variants ("URL", "HEX"). Throw CryptoError.
std::string Base64encode(const std::string& data, const std::string& variant = std::string());
std::string Base64decode(const std::string& data, const std::string& variant = std::string());
std::string Base32encode(const std::string& data, const std::string& variant = std::string());
std::string Base32decode(const std::string& data, const std::string& variant = std::string());

// RSA, DER keys, RSAES_OAEP_SHA. Throw CryptoError.
KeyPair GenerateRsaKeyPair(unsigned int size);
std::string RsaEncode(const std::string& data, const std::string& publicKey);
std::string RsaDecode(const std::string& data, const std::string& privateKey);

// AES-128 CTR with a random IV. Empty strings on failure, as desktop.
std::pair<std::string, std::string> Aes128encode(const std::string& data, const std::string& key);
std::string Aes128decode(const std::string& data, const std::string& key, std::string iv);

// Raw XTEA blocks (no base64), as SharedUtil::TeaEncode / TeaDecode.
void TeaEncode(const std::string& str, const std::string& key, std::string* out);
void TeaDecode(const std::string& str, const std::string& key, std::string* out);

// bcrypt $2y$ (SharedUtil.Hash.hpp:511-551). Empty string on failure.
std::string BcryptHash(const std::string& password, std::string salt = std::string(), std::size_t cost = 10);
bool BcryptVerify(const std::string& password, const std::string& hash);

// zlib (SharedUtil.Crypto.h:257-333). Return the zlib status; Z_STREAM_END is success.
bool StringToZLibFormat(const std::string& format, int& outResult);
int ZLibCompress(const std::string& input, std::string& output, int windowBits = static_cast<int>(ZLibFormat::GZIP),
                 int compression = 9, ZLibStrategy strategy = ZLibStrategy::DEFAULT);
int ZLibUncompress(const std::string& input, std::string& output, int windowBits = 0);

}  // namespace WorldRPG::Crypto
