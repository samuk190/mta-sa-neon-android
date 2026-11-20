#include "SharedCrypto.h"

// Bodies follow mta_neon_source/Shared/sdk/SharedUtil.Crypto.h and SharedUtil.Hash.hpp
// line for line. Differences: SString -> std::string, and CryptoPP::Exception is
// rethrown as CryptoError so callers do not need the Crypto++ headers.

#include <cryptopp/aes.h>
#include <cryptopp/base32.h>
#include <cryptopp/base64.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/osrng.h>
#include <cryptopp/rsa.h>

#include <zlib.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>

namespace bcrypt {
extern "C" {
#include <bcrypt/ow-crypt.h>
}
}  // namespace bcrypt

namespace WorldRPG::Crypto {
namespace {

// Runs a Crypto++ call and converts its exception (desktop catches CryptoPP::Exception
// and reports GetWhat()).
template <class F>
auto Guard(F&& body) -> decltype(body()) {
    try {
        return body();
    } catch (const CryptoPP::Exception& ex) {
        throw CryptoError(ex.GetWhat());
    }
}

// SharedUtil.Hash.hpp:745-773.
void encodeXtea(unsigned int* v, unsigned int* w, unsigned int* k) {
    unsigned int v0 = v[0], v1 = v[1], i, sum = 0;
    unsigned int delta = 0x9E3779B9;
    for (i = 0; i < 32; i++) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]);
        sum += delta;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]);
    }
    w[0] = v0;
    w[1] = v1;
}

void decodeXtea(unsigned int* v, unsigned int* w, unsigned int* k) {
    unsigned int v0 = v[0], v1 = v[1], i, sum = 0xC6EF3720;
    unsigned int delta = 0x9E3779B9;
    for (i = 0; i < 32; i++) {
        v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + k[(sum >> 11) & 3]);
        sum -= delta;
        v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[sum & 3]);
    }
    w[0] = v0;
    w[1] = v1;
}

}  // namespace

std::string Base64encode(const std::string& data, const std::string& variant) {
    return Guard([&] {
        std::string result;
        if (variant == "URL")
            CryptoPP::StringSource ss(data, true, new CryptoPP::Base64URLEncoder(new CryptoPP::StringSink(result), false));
        else
            CryptoPP::StringSource ss(data, true, new CryptoPP::Base64Encoder(new CryptoPP::StringSink(result), false));
        return result;
    });
}

std::string Base64decode(const std::string& data, const std::string& variant) {
    return Guard([&] {
        std::string result;
        if (variant == "URL")
            CryptoPP::StringSource ss(data, true, new CryptoPP::Base64URLDecoder(new CryptoPP::StringSink(result)));
        else
            CryptoPP::StringSource ss(data, true, new CryptoPP::Base64Decoder(new CryptoPP::StringSink(result)));
        return result;
    });
}

std::string Base32encode(const std::string& data, const std::string& variant) {
    return Guard([&] {
        std::string result;
        if (variant == "HEX")
            CryptoPP::StringSource ss(data, true, new CryptoPP::Base32HexEncoder(new CryptoPP::StringSink(result), false));
        else
            CryptoPP::StringSource ss(data, true, new CryptoPP::Base32Encoder(new CryptoPP::StringSink(result), false));
        return result;
    });
}

std::string Base32decode(const std::string& data, const std::string& variant) {
    return Guard([&] {
        std::string result;
        if (variant == "HEX")
            CryptoPP::StringSource ss(data, true, new CryptoPP::Base32HexDecoder(new CryptoPP::StringSink(result)));
        else
            CryptoPP::StringSource ss(data, true, new CryptoPP::Base32Decoder(new CryptoPP::StringSink(result)));
        return result;
    });
}

KeyPair GenerateRsaKeyPair(unsigned int size) {
    return Guard([&] {
        KeyPair rsaKeyPair;
        CryptoPP::AutoSeededRandomPool asrp;
        CryptoPP::InvertibleRSAFunction params;
        params.GenerateRandomWithKeySize(asrp, size);
        CryptoPP::RSA::PrivateKey rsaPrivateKey(params);
        CryptoPP::RSA::PublicKey rsaPublicKey(params);
        CryptoPP::StringSink rawPrivateKeySink(rsaKeyPair.privateKey);
        rsaPrivateKey.DEREncode(rawPrivateKeySink);
        CryptoPP::StringSink rawPublicKeySink(rsaKeyPair.publicKey);
        rsaPublicKey.DEREncode(rawPublicKeySink);
        return rsaKeyPair;
    });
}

std::string RsaEncode(const std::string& data, const std::string& publicKey) {
    return Guard([&] {
        std::string result;
        CryptoPP::RSA::PublicKey rsaPublicKey;
        CryptoPP::AutoSeededRandomPool asrp;
        CryptoPP::StringSource rsaPublicKeySource(publicKey, true);
        rsaPublicKey.BERDecode(rsaPublicKeySource);
        CryptoPP::RSAES_OAEP_SHA_Encryptor rsaEncryptor(rsaPublicKey);
        CryptoPP::StringSource ss(data, true, new CryptoPP::PK_EncryptorFilter(asrp, rsaEncryptor, new CryptoPP::StringSink(result)));
        return result;
    });
}

std::string RsaDecode(const std::string& data, const std::string& privateKey) {
    return Guard([&] {
        std::string result;
        CryptoPP::RSA::PrivateKey rsaPrivateKey;
        CryptoPP::AutoSeededRandomPool asrp;
        CryptoPP::StringSource rsaPrivateKeySource(privateKey, true);
        rsaPrivateKey.BERDecode(rsaPrivateKeySource);
        CryptoPP::RSAES_OAEP_SHA_Decryptor rsaDecryptor(rsaPrivateKey);
        CryptoPP::StringSource ss(data, true, new CryptoPP::PK_DecryptorFilter(asrp, rsaDecryptor, new CryptoPP::StringSink(result)));
        return result;
    });
}

// SharedUtil.Crypto.h:176-217. The desktop malloc probe exists to bypass MTA's Windows
// OOM handler; it is kept so a huge input fails the same way instead of aborting.
std::pair<std::string, std::string> Aes128encode(const std::string& sData, const std::string& sKey) {
    using namespace CryptoPP;
    constexpr size_t kMaxProbeSize = SIZE_MAX / 3 - 256;
    if (sData.size() > kMaxProbeSize) return {std::string(), std::string()};
    void* probe = std::malloc(sData.size() * 3 + 256);
    if (!probe) return {std::string(), std::string()};
    std::free(probe);
    try {
        AutoSeededRandomPool rnd;
        std::string result;
        std::string sIv;
        result.reserve(sData.size());
        sIv.resize(AES::BLOCKSIZE);
        rnd.GenerateBlock(reinterpret_cast<CryptoPP::byte*>(sIv.data()), sIv.size());
        CTR_Mode<AES>::Encryption aesEncryption;
        aesEncryption.SetKeyWithIV(reinterpret_cast<const CryptoPP::byte*>(sKey.data()), sKey.size(),
                                   reinterpret_cast<const CryptoPP::byte*>(sIv.data()));
        StringSource ss(sData, true, new StreamTransformationFilter(aesEncryption, new StringSink(result)));
        return {result, sIv};
    } catch (const std::exception&) {
        return {std::string(), std::string()};
    }
}

// SharedUtil.Crypto.h:219-255.
std::string Aes128decode(const std::string& sData, const std::string& sKey, std::string sIv) {
    using namespace CryptoPP;
    constexpr size_t kMaxProbeSize = SIZE_MAX / 3 - 256;
    if (sData.size() > kMaxProbeSize) return std::string();
    void* probe = std::malloc(sData.size() * 3 + 256);
    if (!probe) return std::string();
    std::free(probe);
    try {
        sIv.resize(AES::BLOCKSIZE);
        std::string result;
        result.reserve(sData.size());
        CTR_Mode<AES>::Decryption aesDecryption;
        aesDecryption.SetKeyWithIV(reinterpret_cast<const CryptoPP::byte*>(sKey.data()), sKey.size(),
                                   reinterpret_cast<const CryptoPP::byte*>(sIv.data()));
        StringSource ss(sData, true, new StreamTransformationFilter(aesDecryption, new StringSink(result)));
        return result;
    } catch (const std::exception&) {
        return std::string();
    }
}

// SharedUtil.Hash.hpp:773-835.
void TeaEncode(const std::string& str, const std::string& key, std::string* out) {
    unsigned int v[2] = {};
    unsigned int w[2] = {};
    unsigned int k[4] = {};
    unsigned int keybuffer[4] = {};
    out->clear();

    int len = static_cast<int>(key.length());
    if (len > 16) len = 16;
    std::memcpy(keybuffer, key.c_str(), len);
    for (int i = 0; i < 4; ++i) k[i] = keybuffer[i];

    if (str.length() == 0 || str.length() > static_cast<size_t>(INT_MAX - 4)) return;
    int strbuflen = static_cast<int>(str.length());
    if ((strbuflen % 4) > 0) strbuflen += 4 - (strbuflen % 4);

    std::unique_ptr<unsigned char[]> strbuf(new (std::nothrow) unsigned char[strbuflen]);
    if (!strbuf) return;
    std::memset(strbuf.get(), 0, strbuflen);
    std::memcpy(strbuf.get(), str.c_str(), str.length());

    v[1] = 0;
    for (int i = 0; i < strbuflen; i += 4) {
        std::memcpy(&v[0], &strbuf[i], 4);
        encodeXtea(&v[0], &w[0], &k[0]);
        out->append(reinterpret_cast<const char*>(&w[0]), 4);
        v[1] = w[1];
    }
    out->append(reinterpret_cast<const char*>(&v[1]), 4);
}

// SharedUtil.Hash.hpp:837-909.
void TeaDecode(const std::string& str, const std::string& key, std::string* out) {
    unsigned int v[2] = {};
    unsigned int w[2] = {};
    unsigned int k[4] = {};
    unsigned int keybuffer[4] = {};
    out->clear();

    if (str.length() > static_cast<size_t>(INT_MAX)) return;
    const int numBlocks = static_cast<int>(str.length() / 4);
    const int numPasses = numBlocks - 1;
    if (numPasses <= 0) return;

    int len = static_cast<int>(key.length());
    if (len > 16) len = 16;
    std::memcpy(keybuffer, key.c_str(), len);
    for (int i = 0; i < 4; ++i) k[i] = keybuffer[i];

    const size_t workingSize = static_cast<size_t>(numPasses) * 4 + 4;
    std::unique_ptr<unsigned char[]> buffer(new (std::nothrow) unsigned char[workingSize]);
    if (!buffer) return;
    std::memset(buffer.get(), 0, workingSize);

    const char* p = str.c_str();
    std::memcpy(&v[1], &p[numPasses * 4], 4);
    for (int i = 0; i < numPasses; ++i) {
        std::memcpy(&v[0], &p[(numPasses - i - 1) * 4], 4);
        decodeXtea(&v[0], &w[0], &k[0]);
        std::memcpy(&buffer[(numPasses - i - 1) * 4], &w[0], 4);
        v[1] = w[1];
    }
    out->assign(reinterpret_cast<const char*>(buffer.get()), static_cast<size_t>(numPasses) * 4);
}

// SharedUtil.Hash.hpp:511-541.
std::string BcryptHash(const std::string& password, std::string salt, std::size_t cost) {
    constexpr std::size_t HashBufferSize = 60 + 1;
    if (salt.empty()) {
        std::random_device random;
        std::mt19937 generator(random());
        char saltBuffer[32];
        std::generate_n(saltBuffer, sizeof(saltBuffer), generator);
        char saltBase64Buffer[30];
        if (!bcrypt::crypt_gensalt_rn("$2y$", cost, saltBuffer, sizeof(saltBuffer), saltBase64Buffer, sizeof(saltBase64Buffer)))
            return "";
        salt = saltBase64Buffer;
    } else {
        // SString("$2y$%02u$%s", cost, salt.substr(0, 22).c_str())
        char prefix[32];
        std::snprintf(prefix, sizeof(prefix), "$2y$%02u$", static_cast<unsigned>(cost));
        salt = prefix + salt.substr(0, 22);
    }
    if (salt.size() != 29) return "";
    char hashBuffer[HashBufferSize];
    if (!bcrypt::crypt_rn(password.c_str(), salt.c_str(), hashBuffer, sizeof(hashBuffer))) return "";
    return hashBuffer;
}

// SharedUtil.Hash.hpp:543-550.
bool BcryptVerify(const std::string& password, const std::string& hash) {
    constexpr std::size_t HashBufferSize = 60 + 1;
    char checkedHashBuffer[HashBufferSize];
    if (!bcrypt::crypt_rn(password.c_str(), hash.c_str(), checkedHashBuffer, sizeof(checkedHashBuffer))) return false;
    return std::strcmp(checkedHashBuffer, hash.c_str()) == 0;
}

// SharedUtil.Crypto.h:257-266.
bool StringToZLibFormat(const std::string& format, int& outResult) {
    const int value = std::atoi(format.c_str());
    if ((value >= 9 && value <= 31) || (value >= -15 && value <= -9)) {
        outResult = value;
        return true;
    }
    return false;
}

// SharedUtil.Crypto.h:268-292.
int ZLibCompress(const std::string& input, std::string& output, int windowBits, int compression, ZLibStrategy strategy) {
    z_stream stream{};
    int result = deflateInit2(&stream, compression, Z_DEFLATED, windowBits, MAX_MEM_LEVEL, static_cast<int>(strategy));
    if (result != Z_OK) return result;
    output.resize(deflateBound(&stream, static_cast<uLong>(input.size())));
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    result = deflate(&stream, Z_FINISH);
    result |= deflateEnd(&stream);
    if (result == Z_STREAM_END) output.resize(stream.total_out);
    return result;
}

// SharedUtil.Crypto.h:294-333.
int ZLibUncompress(const std::string& input, std::string& output, int windowBits) {
    if (windowBits == 0 && input.size() >= 2) {
        if (input[0] == '\x1F' && input[1] == '\x8B') windowBits = static_cast<int>(ZLibFormat::GZIP);
        else if (input[0] == '\x78') windowBits = static_cast<int>(ZLibFormat::ZLIB);
        else windowBits = static_cast<int>(ZLibFormat::ZRAW);
    }
    z_stream stream{};
    int result = inflateInit2(&stream, windowBits);
    if (result != Z_OK) return result;
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    std::string buffer;
    buffer.resize(std::min(stream.avail_in, 128000U));
    while (true) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        result = inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) break;
        output.append(buffer, 0, stream.total_out - output.size());
        if (result == Z_STREAM_END) break;
    }
    result |= inflateEnd(&stream);
    return result;
}

}  // namespace WorldRPG::Crypto
