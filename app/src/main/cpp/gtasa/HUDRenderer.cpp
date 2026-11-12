#include "HUDRenderer.h"

#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <deque>

namespace WorldRPG::Game {

struct ChatLine {
    std::string text;
    uint8_t color;
};

struct HUDRenderer::Impl {
    RenderWareHooks* renderer = nullptr;
    int screenWidth = 0;
    int screenHeight = 0;

    // Configurable HUD element parameters (AGENTS.md directive 10)
    std::unordered_map<std::string, HUDElementParams> elementParams;

    // Chat overlay buffer
    std::deque<ChatLine> chatLines;
    int maxChatLines = 8;

    void initDefaultParams() {
        // Health bar: bottom-left
        elementParams["health_bar"] = {0.02f, 0.92f, 0.15f, 0.025f, true};
        // Armor bar: below health
        elementParams["armor_bar"] = {0.02f, 0.89f, 0.15f, 0.020f, true};
        // Nametag offset (relative to player screen position)
        elementParams["nametag"] = {0.5f, 0.3f, 0.12f, 0.02f, true};
        // Speedometer: bottom-right
        elementParams["speedometer"] = {0.85f, 0.90f, 0.12f, 0.06f, true};
        // Chat overlay: top-left
        elementParams["chat_overlay"] = {0.01f, 0.01f, 0.40f, 0.25f, true};
        // Connection status: top-center
        elementParams["connection_status"] = {0.35f, 0.01f, 0.30f, 0.03f, true};
    }

    void drawHealthBar(const Sync::ElementManager& elements, Sync::ElementID localPlayerId) {
        auto it = elementParams.find("health_bar");
        if (it == elementParams.end() || !it->second.visible) return;

        Sync::PlayerSyncData player;
        if (!elements.getPlayerSync(localPlayerId, player)) return;

        const auto& p = it->second;
        float healthFrac = std::clamp(player.health / 100.0f, 0.0f, 1.0f);

        // Background (dark gray)
        renderer->drawRect(p.x, p.y, p.width, p.height, {40, 40, 40, 200});
        // Foreground (green gradient based on health)
        uint8_t r = static_cast<uint8_t>(255 * (1.0f - healthFrac));
        uint8_t g = static_cast<uint8_t>(255 * healthFrac);
        renderer->drawRect(p.x, p.y, p.width * healthFrac, p.height, {r, g, 0, 230});
    }

    void drawArmorBar(const Sync::ElementManager& elements, Sync::ElementID localPlayerId) {
        auto it = elementParams.find("armor_bar");
        if (it == elementParams.end() || !it->second.visible) return;

        Sync::PlayerSyncData player;
        if (!elements.getPlayerSync(localPlayerId, player)) return;

        const auto& p = it->second;
        float armorFrac = std::clamp(player.armor / 100.0f, 0.0f, 1.0f);

        renderer->drawRect(p.x, p.y, p.width, p.height, {40, 40, 40, 200});
        renderer->drawRect(p.x, p.y, p.width * armorFrac, p.height, {180, 180, 220, 230});
    }

    void drawChatOverlay() {
        auto it = elementParams.find("chat_overlay");
        if (it == elementParams.end() || !it->second.visible || chatLines.empty()) return;

        const auto& p = it->second;
        float lineHeight = p.height / static_cast<float>(maxChatLines);

        int lineIdx = 0;
        for (auto rit = chatLines.rbegin(); rit != chatLines.rend() && lineIdx < maxChatLines; ++rit, ++lineIdx) {
            float ly = p.y + p.height - (lineIdx + 1) * lineHeight;
            // Semi-transparent background per line
            renderer->drawRect(p.x, ly, p.width, lineHeight, {0, 0, 0, 120});
            // Text (stub — bitmap font not yet implemented)
            renderer->drawText(rit->text.c_str(), p.x + 0.005f, ly + 0.002f, 12.0f,
                              {255, 255, 255, static_cast<uint8_t>(rit->color)});
        }
    }

    void drawConnectionStatus(bool connected) {
        auto it = elementParams.find("connection_status");
        if (it == elementParams.end() || !it->second.visible) return;

        const auto& p = it->second;
        const char* text = connected ? "CONNECTED" : "DISCONNECTED";
        RenderColor bg = connected ? RenderColor{0, 80, 0, 180} : RenderColor{120, 0, 0, 180};
        renderer->drawRect(p.x, p.y, p.width, p.height, bg);
        renderer->drawText(text, p.x + 0.005f, p.y + 0.003f, 14.0f, RenderColor::white());
    }
};

HUDRenderer::HUDRenderer() : pImpl_(std::make_unique<Impl>()) {
    pImpl_->initDefaultParams();
}

HUDRenderer::~HUDRenderer() = default;

bool HUDRenderer::initialize(RenderWareHooks& renderer, int screenWidth, int screenHeight) {
    pImpl_->renderer = &renderer;
    pImpl_->screenWidth = screenWidth;
    pImpl_->screenHeight = screenHeight;
    HUD_LOGI("HUDRenderer: initialized %dx%d", screenWidth, screenHeight);
    return true;
}

void HUDRenderer::draw(const Sync::ElementManager& elements, Sync::ElementID localPlayerId) {
    if (!pImpl_->renderer || !pImpl_->renderer->isReady()) return;

    pImpl_->drawHealthBar(elements, localPlayerId);
    pImpl_->drawArmorBar(elements, localPlayerId);
    pImpl_->drawChatOverlay();
    pImpl_->drawConnectionStatus(localPlayerId != Sync::INVALID_ELEMENT_ID);
}

void HUDRenderer::setElementParams(const char* elementName, const HUDElementParams& params) {
    if (!elementName) return;
    pImpl_->elementParams[std::string(elementName)] = params;
    HUD_LOGI("HUDRenderer: set params for '%s' x=%.3f y=%.3f w=%.3f h=%.3f vis=%d",
             elementName, params.x, params.y, params.width, params.height, params.visible);
}

HUDElementParams HUDRenderer::getElementParams(const char* elementName) const {
    if (!elementName) return {};
    auto it = pImpl_->elementParams.find(std::string(elementName));
    return (it != pImpl_->elementParams.end()) ? it->second : HUDElementParams{};
}

void HUDRenderer::listElements(std::function<void(const char*, const HUDElementParams&)> callback) const {
    if (!callback) return;
    for (const auto& [name, params] : pImpl_->elementParams) {
        callback(name.c_str(), params);
    }
}

void HUDRenderer::addChatMessage(const char* message, uint8_t color) {
    if (!message) return;
    // ASCII-normalize per AGENTS.md directive 7
    std::string ascii = Net::NormalizeAscii(std::string(message));
    pImpl_->chatLines.push_back({std::move(ascii), color});
    while (static_cast<int>(pImpl_->chatLines.size()) > pImpl_->maxChatLines) {
        pImpl_->chatLines.pop_front();
    }
}

void HUDRenderer::setMaxChatLines(int maxLines) {
    pImpl_->maxChatLines = std::max(1, maxLines);
}

void HUDRenderer::onScreenResize(int width, int height) {
    pImpl_->screenWidth = width;
    pImpl_->screenHeight = height;
    HUD_LOGI("HUDRenderer: resized to %dx%d", width, height);
}

} // namespace WorldRPG::Game