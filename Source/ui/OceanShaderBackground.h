#pragma once

#include <juce_opengl/juce_opengl.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace amanita::ui
{
/**
    Owns the OpenGL context used to paint the Ocean background beneath a JUCE
    component. attachTo(), detach(), triggerRepaint(), and the destructor run
    on the message thread. setSnapshot() uses atomics so the GL thread never
    touches live JUCE controls.
*/
class OceanShaderBackground final : private juce::OpenGLRenderer
{
public:
    struct Snapshot
    {
        int algorithm = 0;
        float evolution = 0.0f;
        float focus = 1.0f;
        bool frozen = false;
        float currentFlowX = 0.0f;
        float currentFlowY = 0.0f;
        float currentStrength = 0.0f;
        juce::Colour accent { 0xff79cbd0 };
    };

    OceanShaderBackground();
    ~OceanShaderBackground() override;

    void attachTo(juce::Component& component);
    void detach() noexcept;

    void setSnapshot(const Snapshot& snapshot) noexcept;
    void setSnapshot(int algorithm,
                     float evolution,
                     float focus,
                     bool frozen,
                     juce::Colour accent) noexcept;
    void setSnapshot(int algorithm,
                     float evolution,
                     float focus,
                     bool frozen,
                     float currentFlowX,
                     float currentFlowY,
                     float currentStrength,
                     juce::Colour accent) noexcept;
    void triggerRepaint() noexcept;

    [[nodiscard]] bool isAttached() const noexcept;
    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] bool hasFailed() const noexcept;

private:
    struct RenderSnapshot
    {
        int algorithm = 0;
        float evolution = 0.0f;
        float focus = 1.0f;
        bool frozen = false;
        float currentFlowX = 0.0f;
        float currentFlowY = 0.0f;
        float currentStrength = 0.0f;
        juce::Colour accent { 0xff79cbd0 };
    };

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    [[nodiscard]] bool buildShaders();
    [[nodiscard]] bool ensureRenderTargets(int outputWidth,
                                           int outputHeight,
                                           float renderingScale);
    [[nodiscard]] bool renderBlurPass(GLuint sourceTexture,
                                      juce::OpenGLFrameBuffer& destination,
                                      float directionX,
                                      float directionY);
    void releaseOpenGLResources() noexcept;
    [[nodiscard]] RenderSnapshot loadSnapshot() const noexcept;
    void initialiseRenderedState(const RenderSnapshot& target) noexcept;
    void advanceRenderedState(const RenderSnapshot& target,
                              float elapsedSeconds) noexcept;

    juce::OpenGLContext context_;
    std::unique_ptr<juce::OpenGLShaderProgram> sceneProgram_;
    std::unique_ptr<juce::OpenGLShaderProgram> blurProgram_;
    std::unique_ptr<juce::OpenGLShaderProgram> compositeProgram_;
    juce::OpenGLFrameBuffer sceneTarget_;
    juce::OpenGLFrameBuffer bloomPingTarget_;
    juce::OpenGLFrameBuffer bloomPongTarget_;
    GLuint vertexArray_ = 0;
    int attemptedSceneWidth_ = 0;
    int attemptedSceneHeight_ = 0;
    int attemptedBloomWidth_ = 0;
    int attemptedBloomHeight_ = 0;
    bool sceneAllocationFailed_ = false;
    bool bloomReady_ = false;
    bool bloomAllocationFailed_ = false;

    std::atomic<int> algorithm_ { 0 };
    std::atomic<float> evolution_ { 0.0f };
    std::atomic<float> focus_ { 1.0f };
    std::atomic<bool> frozen_ { false };
    std::atomic<float> currentFlowX_ { 0.0f };
    std::atomic<float> currentFlowY_ { 0.0f };
    std::atomic<float> currentStrength_ { 0.0f };
    std::atomic<std::uint32_t> accentArgb_ { 0xff79cbd0u };
    std::atomic<std::uint32_t> snapshotRevision_ { 0 };

    std::atomic<bool> attached_ { false };
    std::atomic<bool> ready_ { false };
    std::atomic<bool> failed_ { false };
    juce::Component* attachedComponent_ = nullptr;

    std::array<float, 5> characterBlend_ { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float renderedEvolution_ = 0.0f;
    float renderedFocus_ = 1.0f;
    float renderedCurrentFlowX_ = 0.0f;
    float renderedCurrentFlowY_ = 0.0f;
    float renderedCurrentStrength_ = 0.0f;
    juce::Colour renderedAccent_ { 0xff79cbd0 };
    float motion_ = 1.0f;
    float animationTime_ = 19.73f;
    double previousFrameSeconds_ = 0.0;
    bool renderedStateInitialised_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OceanShaderBackground)
};
} // namespace amanita::ui
