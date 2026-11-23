#pragma once

#ifdef __ANDROID__
#include <jni.h>
#include <cstdint>
#include <memory>
#include <string>

namespace WorldRPG::Net {
class QuicGameClient {
public:
    static std::unique_ptr<QuicGameClient> Connect(JNIEnv* env, jobject activity,
                                                    const std::string& host, uint16_t port);
    ~QuicGameClient();
    // Zero during the handshake, positive when authenticated, negative on failure.
    int Status(std::string& error) const;
    int Poll(uint8_t* kind, uint32_t* bits, uint8_t* payload, int capacity);
    bool Send(uint8_t kind, uint32_t bits, const uint8_t* payload, uint8_t mode, uint8_t channel);
private:
    struct Connection;
    explicit QuicGameClient(std::shared_ptr<Connection> conn);
    std::shared_ptr<Connection> connection;
};
}
#endif
