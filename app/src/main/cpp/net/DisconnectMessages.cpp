#include "DisconnectMessages.h"

#include <cstdio>
#include <cstdlib>

namespace WorldRPG::Net::DisconnectMessages {
namespace {

std::string Code(const char* code) { return std::string(" [") + code + "]"; }

// SString(fmt, arg) with one %s.
std::string Format(const char* fmt, const std::string& arg) {
    std::string out = fmt;
    const size_t at = out.find("%s");
    if (at != std::string::npos) out.replace(at, 2, arg);
    return out;
}

std::string Plural(long value, const char* one, const char* many) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), value == 1 ? one : many, static_cast<int>(value));
    return buf;
}

// "Time Remaining: ..." (CPacketHandler.cpp:715-737).
std::string DurationText(const std::string& duration) {
    if (duration.empty() || duration == "0") return std::string();
    long seconds = std::strtol(duration.c_str(), nullptr, 10);
    const long days = seconds / 86400;
    seconds %= 86400;
    const long hours = seconds / 3600;
    seconds %= 3600;
    const long mins = seconds / 60;
    seconds %= 60;
    std::string out = "\nTime Remaining: ";
    if (days) out += Plural(days, "%d day", "%d days") + ((hours || mins) ? " " : "");
    if (hours) out += Plural(hours, "%d hour", "%d hours") + (mins ? " " : "");
    if (mins) out += Plural(mins, "%d minute", "%d minutes") + (seconds ? " " : "");
    if (!days && !hours && seconds) out += Plural(seconds, "%d second", "%d seconds");
    return out;
}

} // namespace

std::string Pack(const Box& box) { return box.title + kSeparator + box.text; }

bool HasDuration(uint8_t type) { return type == BANNED_SERIAL || type == BANNED_IP || type == BAN; }

Box ServerDisconnected(uint8_t type, const std::string& message, const std::string& duration) {
    const char* reason = "";
    const char* code = nullptr;
    bool extra = false;
    switch (type) {
        case INVALID_NICKNAME: reason = "Disconnected: Invalid nickname"; code = "CD30"; break;
        case NO_REASON: reason = "Disconnect from server"; code = "CD31"; break;
        case BANNED_SERIAL: reason = "Disconnected: Serial is banned.\nReason: %s"; code = "CD32"; extra = true; break;
        case BANNED_IP: reason = "Disconnected: You are banned.\nReason: %s"; code = "CD33"; extra = true; break;
        case BANNED_ACCOUNT: reason = "Disconnected: Account is banned.\nReason: %s"; code = "CD34"; extra = true; break;
        case VERSION_MISMATCH: reason = "Disconnected: Version mismatch"; code = "CD35"; break;
        case JOIN_FLOOD: reason = "Disconnected: Join flood. Please wait a minute, then reconnect."; code = "CD36"; break;
        case DIFFERENT_BRANCH: reason = "Disconnected: Server from different branch.\nInformation: %s"; code = "CD37"; extra = true; break;
        case BAD_VERSION: reason = "Disconnected: Bad version.\nInformation: %s"; code = "CD38"; extra = true; break;
        case SERVER_NEWER: reason = "Disconnected: Server is running a newer build.\nInformation: %s"; code = "CD39"; extra = true; break;
        case SERVER_OLDER: reason = "Disconnected: Server is running an older build.\nInformation: %s"; code = "CD40"; extra = true; break;
        case NICK_CLASH: reason = "Disconnected: Nick already in use"; code = "CD41"; break;
        case ELEMENT_FAILURE: reason = "Disconnected: Player element could not be created."; code = "CD42"; break;
        case GENERAL_REFUSED: reason = "Disconnected: Server refused the connection: %s"; code = "CD43"; extra = true; break;
        case SERIAL_VERIFICATION: reason = "Disconnected: Serial verification failed"; code = "CD44"; break;
        case SERIAL_DUPLICATE: reason = "Disconnected: Serial already in use"; code = "CD50"; break;
        case CONNECTION_DESYNC: reason = "Disconnected: Connection desync %s"; code = "CD45"; extra = true; break;
        case INVALID_PASSWORD:
            // Desktop opens the password window (ShowServerInfo(2)); the phone has none yet.
            return Box{"Disconnected", "This server is password protected. Passwords are not supported on this client yet."};
        case KICK: reason = "Disconnected: You were kicked by %s"; code = "CD46"; extra = true; break;
        case BAN: reason = "Disconnected: You were banned by %s"; code = "CD47"; extra = true; break;
        case CUSTOM: reason = "%s"; code = "CD48"; extra = true; break;
        case SHUTDOWN: reason = "Disconnected: Server shutdown or restarting"; code = "CD49"; break;
        default: break;  // INCORRECT_PASSWORD and unknown types: no text and no code on desktop.
    }
    std::string text = extra ? Format(reason, message) : std::string(reason);
    // Desktop shows an empty box for a type it does not know; show the server's message instead.
    if (text.empty()) text = message;
    text += DurationText(duration);
    return Box{std::string("Disconnected") + (code ? Code(code) : std::string()), text};
}

Transport ClassifyTransport(const std::string& reason) {
    // Strings set by RakNetWrapper::DoPulse for the RakNet ids.
    if (reason == "Connection attempt failed") return Transport::AttemptFailed;
    if (reason == "Disconnected by server") return Transport::Disconnected;
    if (reason == "Connection lost") return Transport::Lost;
    if (reason == "Banned from server") return Transport::Banned;
    if (reason == "Invalid password") return Transport::InvalidPassword;
    return Transport::Other;
}

Box TransportFailure(Transport cause, bool joined) {
    switch (cause) {
        case Transport::Disconnected:
            return {"Error" + Code(joined ? "CD15" : "CC28"), "Disconnected: disconnected from the server"};
        case Transport::Lost:
            return {"Error" + Code(joined ? "CD16" : "CC29"), "Disconnected: connection to the server was lost"};
        case Transport::Banned:
            return {"Error" + Code(joined ? "CD13" : "CC27"), "Disconnected: you are banned from this server"};
        case Transport::InvalidPassword:
            if (joined) return {"Error" + Code("CD17"), "Disconnected: invalid password specified"};
            return {"Error", "This server is password protected. Passwords are not supported on this client yet."};
        case Transport::AttemptFailed:
        case Transport::Other:
        default:
            return {"Error" + Code(joined ? "CD18" : "CC30"), "Disconnected: connection was refused"};
    }
}

Box ConnectTimedOut() { return {"Error" + Code("CC23"), "Connection timed out"}; }
Box JoinTimedOut() { return {"Error" + Code("CD08"), "Connection timed out"}; }
Box ConnectionLost() { return {"Error" + Code("CD09"), "Connection with the server was lost"}; }
Box InvalidNick() { return {"Error" + Code("CC20"), "Connecting failed. Invalid nick provided!"}; }

Box ProtocolError(unsigned code) {
    char text[160];
    std::snprintf(text, sizeof(text),
                  "Protocol error (%u). If this problem persists, please check out multitheftauto.com for support.", code);
    return {"Connection error" + Code("CD63"), text};
}

Box Plain(const std::string& title, const std::string& text) { return {title, text}; }

} // namespace WorldRPG::Net::DisconnectMessages
