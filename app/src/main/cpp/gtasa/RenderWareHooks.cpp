#include "RenderWareHooks.h"
#include "BitmapFont.h"
#include "../shared/PerformanceProfiler.h"

#ifdef __ANDROID__
#include <GLES2/gl2.h>
#include <cstring>
#endif

namespace WorldRPG::Game {

struct RenderWareHooks::Impl {
    bool initialized = false;
    int screenWidth = 0;
    int screenHeight = 0;
    BitmapFont bitmapFont;

#ifdef __ANDROID__
    GLuint shaderProgram = 0;
    GLuint rectVbo = 0;
    GLint posAttrib = -1;
    GLint colorUniform = -1;
    GLint resolutionUniform = -1;

    static constexpr const char* kVertexShader = R"(
        attribute vec2 aPosition;
        uniform vec2 uResolution;
        void main() {
            vec2 clipSpace = (aPosition / uResolution) * 2.0 - 1.0;
            gl_Position = vec4(clipSpace.x, -clipSpace.y, 0.0, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        precision mediump float;
        uniform vec4 uColor;
        void main() {
            gl_FragColor = uColor;
        }
    )";

    GLuint compileShader(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            char log[512];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            RW_LOGE("Shader compile error: %s", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    bool initShaders() {
        GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShader);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
        if (!vs || !fs) return false;

        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vs);
        glAttachShader(shaderProgram, fs);
        glLinkProgram(shaderProgram);

        GLint linked = 0;
        glGetProgramiv(shaderProgram, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[512];
            glGetProgramInfoLog(shaderProgram, sizeof(log), nullptr, log);
            RW_LOGE("Shader link error: %s", log);
            glDeleteProgram(shaderProgram);
            shaderProgram = 0;
            return false;
        }

        posAttrib = glGetAttribLocation(shaderProgram, "aPosition");
        colorUniform = glGetUniformLocation(shaderProgram, "uColor");
        resolutionUniform = glGetUniformLocation(shaderProgram, "uResolution");

        // Create rectangle VBO (unit quad: 0,0 -> 1,1)
        static const GLfloat rectVerts[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
            1.0f, 1.0f
        };
        glGenBuffers(1, &rectVbo);
        glBindBuffer(GL_ARRAY_BUFFER, rectVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(rectVerts), rectVerts, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDeleteShader(vs);
        glDeleteShader(fs);
        return true;
    }
#endif
};

RenderWareHooks::RenderWareHooks() : pImpl_(std::make_unique<Impl>()) {}
RenderWareHooks::~RenderWareHooks() {
#ifdef __ANDROID__
    if (pImpl_->shaderProgram) glDeleteProgram(pImpl_->shaderProgram);
    if (pImpl_->rectVbo) glDeleteBuffers(1, &pImpl_->rectVbo);
#endif
}

bool RenderWareHooks::initialize() {
#ifdef __ANDROID__
    if (pImpl_->initShaders()) {
        if (!pImpl_->bitmapFont.initialize()) {
            RW_LOGE("RenderWareHooks: bitmap font initialization failed");
            return false;
        }
        pImpl_->initialized = true;
        RW_LOGI("RenderWareHooks: GLESv2 overlay renderer + bitmap font initialized");
        return true;
    }
    RW_LOGE("RenderWareHooks: shader initialization failed");
    return false;
#else
    RW_LOGI("RenderWareHooks: non-Android stub");
    return false;
#endif
}

bool RenderWareHooks::isReady() const {
    return pImpl_->initialized;
}

void RenderWareHooks::beginFrame() {
    // Hook point for RwCameraBeginScene — currently a no-op placeholder
}

void RenderWareHooks::endFrame() {
    PROFILE_SCOPE("RenderWareHooks::endFrame");
    // Custom overlays are drawn here after the game renders its frame
    // Actual drawing calls happen via drawRect/drawText from SyncManager or HUD code
}

void RenderWareHooks::drawRect(float x, float y, float width, float height, RenderColor color) {
#ifdef __ANDROID__
    if (!pImpl_->initialized || pImpl_->screenWidth == 0) return;

    glUseProgram(pImpl_->shaderProgram);
    glUniform2f(pImpl_->resolutionUniform,
                static_cast<float>(pImpl_->screenWidth),
                static_cast<float>(pImpl_->screenHeight));
    glUniform4f(pImpl_->colorUniform,
                color.r / 255.0f, color.g / 255.0f,
                color.b / 255.0f, color.a / 255.0f);

    glBindBuffer(GL_ARRAY_BUFFER, pImpl_->rectVbo);
    glEnableVertexAttribArray(pImpl_->posAttrib);
    glVertexAttribPointer(pImpl_->posAttrib, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    // Scale and translate the unit quad
    // For simplicity, we modify vertex data inline (production should use uniforms)
    GLfloat scaledVerts[] = {
        x, y,
        x + width, y,
        x, y + height,
        x + width, y + height
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(scaledVerts), scaledVerts, GL_DYNAMIC_DRAW);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(pImpl_->posAttrib);
    glUseProgram(0);
#else
    (void)x; (void)y; (void)width; (void)height; (void)color;
#endif
}

void RenderWareHooks::drawText(const char* text, float x, float y, float size, RenderColor color) {
    if (!text || !pImpl_->initialized) return;
    pImpl_->bitmapFont.drawText(text, x, y, size, color,
                                pImpl_->screenWidth, pImpl_->screenHeight);
}

void RenderWareHooks::drawTexturedRect(float x, float y, float w, float h, uint32_t textureId) {
    // Textured quad rendering stub — requires textured shader variant
    RW_LOGI("drawTexturedRect: tex=%u at (%.0f,%.0f) size=(%.0f,%.0f) (stub)",
            textureId, x, y, w, h);
}

uint32_t RenderWareHooks::loadTextureFromTexDb(const char* txdName, const char* textureName) {
    // GTA SA Mobile uses .texdb format (not PC .txd)
    // TODO: Implement texdb parser or use engine's own texture loading via hooks
    RW_LOGI("loadTextureFromTexDb: %s/%s (stub)", txdName ? txdName : "?", textureName ? textureName : "?");
    return 0;
}

void RenderWareHooks::deleteTexture(uint32_t textureId) {
#ifdef __ANDROID__
    if (textureId) glDeleteTextures(1, &textureId);
#else
    (void)textureId;
#endif
}

void RenderWareHooks::setScreenSize(int width, int height) {
    pImpl_->screenWidth = width;
    pImpl_->screenHeight = height;
    RW_LOGI("setScreenSize: %dx%d", width, height);
}

int RenderWareHooks::getScreenWidth() const { return pImpl_->screenWidth; }
int RenderWareHooks::getScreenHeight() const { return pImpl_->screenHeight; }

} // namespace WorldRPG::Game