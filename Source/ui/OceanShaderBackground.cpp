#include "OceanShaderBackground.h"
#include "AbyssalFlowShaders.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace amanita::ui
{
namespace
{
[[nodiscard]] float clampUnit(float value) noexcept
{
    if (! std::isfinite(value))
        return 0.0f;

    return juce::jlimit(0.0f, 1.0f, value);
}

[[nodiscard]] float smoothingAmount(float elapsedSeconds,
                                    float timeConstantSeconds) noexcept
{
    if (elapsedSeconds <= 0.0f)
        return 0.0f;

    return 1.0f - std::exp(-elapsedSeconds / timeConstantSeconds);
}
} // namespace

OceanShaderBackground::OceanShaderBackground()
{
    context_.setOpenGLVersionRequired(juce::OpenGLContext::openGL3_2);
    context_.setRenderer(this);
    context_.setComponentPaintingEnabled(true);
    context_.setContinuousRepainting(false);
}

OceanShaderBackground::~OceanShaderBackground()
{
    detach();
    context_.setRenderer(nullptr);
}

void OceanShaderBackground::attachTo(juce::Component& component)
{
    if (attached_.load(std::memory_order_acquire)
        && attachedComponent_ == &component)
    {
        triggerRepaint();
        return;
    }

    detach();
    attachedComponent_ = &component;
    attached_.store(true, std::memory_order_release);
    context_.attachTo(component);
    context_.triggerRepaint();
}

void OceanShaderBackground::detach() noexcept
{
    if (attached_.exchange(false, std::memory_order_acq_rel))
        context_.detach();

    attachedComponent_ = nullptr;
    ready_.store(false, std::memory_order_release);
}

void OceanShaderBackground::setSnapshot(const Snapshot& snapshot) noexcept
{
    setSnapshot(snapshot.algorithm,
                snapshot.evolution,
                snapshot.focus,
                snapshot.frozen,
                snapshot.accent);
}

void OceanShaderBackground::setSnapshot(int algorithm,
                                        float evolution,
                                        float focus,
                                        bool frozen,
                                        juce::Colour accent) noexcept
{
    algorithm_.store(juce::jlimit(0, 3, algorithm), std::memory_order_relaxed);
    evolution_.store(clampUnit(evolution), std::memory_order_relaxed);
    focus_.store(clampUnit(focus), std::memory_order_relaxed);
    frozen_.store(frozen, std::memory_order_relaxed);
    accentArgb_.store(static_cast<std::uint32_t>(accent.getARGB()),
                      std::memory_order_release);
}

void OceanShaderBackground::triggerRepaint() noexcept
{
    if (attached_.load(std::memory_order_acquire))
        context_.triggerRepaint();
}

bool OceanShaderBackground::isAttached() const noexcept
{
    return attached_.load(std::memory_order_acquire);
}

bool OceanShaderBackground::isReady() const noexcept
{
    return ready_.load(std::memory_order_acquire);
}

bool OceanShaderBackground::hasFailed() const noexcept
{
    return failed_.load(std::memory_order_acquire);
}

void OceanShaderBackground::newOpenGLContextCreated()
{
    releaseOpenGLResources();
    failed_.store(false, std::memory_order_release);

    if (! context_.areShadersAvailable()
        || context_.extensions.glGenVertexArrays == nullptr
        || context_.extensions.glBindVertexArray == nullptr
        || juce::gl::glActiveTexture == nullptr
        || juce::gl::glBindFramebuffer == nullptr
        || juce::gl::glCheckFramebufferStatus == nullptr)
    {
        DBG("Amanita Ocean: required OpenGL 3.2 functions are unavailable");
        failed_.store(true, std::memory_order_release);
        return;
    }

    if (! buildShaders())
    {
        failed_.store(true, std::memory_order_release);
        return;
    }

    context_.extensions.glGenVertexArrays(1, &vertexArray_);
    if (vertexArray_ == 0)
    {
        DBG("Amanita Ocean: failed to create the shader background VAO");
        sceneProgram_.reset();
        blurProgram_.reset();
        compositeProgram_.reset();
        failed_.store(true, std::memory_order_release);
        return;
    }

    previousFrameSeconds_ = 0.0;
    renderedStateInitialised_ = false;
    ready_.store(true, std::memory_order_release);
}

void OceanShaderBackground::renderOpenGL()
{
    using namespace juce::gl;

    const auto scale = static_cast<float>(context_.getRenderingScale());
    const auto width = juce::jmax(
        1, juce::roundToInt(static_cast<float>(attachedComponent_ != nullptr
                                                  ? attachedComponent_->getWidth()
                                                  : 1)
                            * scale));
    const auto height = juce::jmax(
        1, juce::roundToInt(static_cast<float>(attachedComponent_ != nullptr
                                                  ? attachedComponent_->getHeight()
                                                  : 1)
                            * scale));

    glBindFramebuffer(GL_FRAMEBUFFER,
                      juce::OpenGLFrameBuffer::getCurrentFrameBufferTarget());
    glViewport(0, 0, width, height);

    if (! ready_.load(std::memory_order_acquire)
        || sceneProgram_ == nullptr
        || compositeProgram_ == nullptr
        || vertexArray_ == 0)
    {
        juce::OpenGLHelpers::clear(juce::Colour(0xff06171a));
        return;
    }

    const auto defaultFramebuffer =
        juce::OpenGLFrameBuffer::getCurrentFrameBufferTarget();

    const auto target = loadSnapshot();
    const auto nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto elapsedSeconds = previousFrameSeconds_ > 0.0
        ? static_cast<float>(juce::jlimit(0.0, 0.10,
                                         nowSeconds - previousFrameSeconds_))
        : (1.0f / 30.0f);
    previousFrameSeconds_ = nowSeconds;

    if (! renderedStateInitialised_)
        initialiseRenderedState(target);
    else
        advanceRenderedState(target, elapsedSeconds);

    animationTime_ += elapsedSeconds * motion_;
    if (! std::isfinite(animationTime_))
        animationTime_ = 19.73f;
    else if (animationTime_ >= 4096.0f)
        animationTime_ = std::fmod(animationTime_, 4096.0f);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    context_.extensions.glBindVertexArray(vertexArray_);

    const auto targetsReady = ensureRenderTargets(width, height, scale);
    const auto sceneWidth = targetsReady ? sceneTarget_.getWidth() : width;
    const auto sceneHeight = targetsReady ? sceneTarget_.getHeight() : height;
    glBindFramebuffer(GL_FRAMEBUFFER,
                      targetsReady ? sceneTarget_.getFrameBufferID()
                                   : defaultFramebuffer);
    glViewport(0, 0, sceneWidth, sceneHeight);
    sceneProgram_->use();
    sceneProgram_->setUniform("uResolution",
                              static_cast<float>(sceneWidth),
                              static_cast<float>(sceneHeight));
    sceneProgram_->setUniform("uTime", animationTime_);
    sceneProgram_->setUniform("uEvolution", renderedEvolution_);
    sceneProgram_->setUniform("uFocus", renderedFocus_);
    sceneProgram_->setUniform("uDirectOutput", targetsReady ? 0.0f : 1.0f);
    sceneProgram_->setUniform("uAccent",
                              renderedAccent_.getFloatRed(),
                              renderedAccent_.getFloatGreen(),
                              renderedAccent_.getFloatBlue());
    sceneProgram_->setUniform("uCharacterBlend",
                              characterBlend_[0],
                              characterBlend_[1],
                              characterBlend_[2],
                              characterBlend_[3]);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (! targetsReady)
    {
        context_.extensions.glBindVertexArray(0);
        context_.extensions.glUseProgram(0);
        return;
    }

    auto bloomRendered = false;
    if (bloomReady_ && blurProgram_ != nullptr)
    {
        const auto characterRadius =
            characterBlend_[0] * 1.00f
          + characterBlend_[1] * 1.25f
          + characterBlend_[2] * 0.86f
          + characterBlend_[3] * 1.35f;
        const auto blurRadius =
            2.65f * characterRadius
          * juce::jmap(renderedEvolution_, 0.88f, 1.22f)
          * juce::jmap(renderedFocus_, 1.16f, 0.95f);

        const auto horizontalOk = renderBlurPass(
            sceneTarget_.getTextureID(),
            bloomPingTarget_,
            blurRadius / static_cast<float>(sceneTarget_.getWidth()),
            0.0f);
        const auto verticalOk = horizontalOk && renderBlurPass(
            bloomPingTarget_.getTextureID(),
            bloomPongTarget_,
            0.0f,
            blurRadius * 0.5f
                / static_cast<float>(bloomPongTarget_.getHeight()));
        bloomRendered = horizontalOk && verticalOk;

        if (! bloomRendered)
        {
            DBG("Amanita Ocean: disabling GPU bloom after a render-target failure");
            bloomReady_ = false;
            bloomAllocationFailed_ = true;
            glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
            glViewport(0, 0, width, height);
            bloomPingTarget_.release();
            bloomPongTarget_.release();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
    glViewport(0, 0, width, height);
    compositeProgram_->use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sceneTarget_.getTextureID());
    compositeProgram_->setUniform("uScene", 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,
                  bloomRendered ? bloomPongTarget_.getTextureID()
                                : sceneTarget_.getTextureID());
    compositeProgram_->setUniform("uBloom", 1);
    compositeProgram_->setUniform("uBloomStrength",
                                  bloomRendered ? 1.0f : 0.0f);
    compositeProgram_->setUniform("uEvolution", renderedEvolution_);
    compositeProgram_->setUniform("uFocus", renderedFocus_);
    compositeProgram_->setUniform("uAccent",
                                  renderedAccent_.getFloatRed(),
                                  renderedAccent_.getFloatGreen(),
                                  renderedAccent_.getFloatBlue());
    compositeProgram_->setUniform("uCharacterBlend",
                                  characterBlend_[0],
                                  characterBlend_[1],
                                  characterBlend_[2],
                                  characterBlend_[3]);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    context_.extensions.glBindVertexArray(0);
    context_.extensions.glUseProgram(0);
}

void OceanShaderBackground::openGLContextClosing()
{
    releaseOpenGLResources();
}

bool OceanShaderBackground::buildShaders()
{
    const auto makeProgram = [this](const char* fragment,
                                    const char* debugName)
        -> std::unique_ptr<juce::OpenGLShaderProgram>
    {
        auto candidate = std::make_unique<juce::OpenGLShaderProgram>(context_);
        if (! candidate->addVertexShader(abyssal_flow::vertex)
            || ! candidate->addFragmentShader(fragment)
            || ! candidate->link())
        {
            const auto message = juce::String("Amanita Ocean ")
                               + debugName + " shader error: "
                               + candidate->getLastError();
            DBG(message);
            std::fprintf(stderr, "%s\n", message.toRawUTF8());
            return {};
        }

        return candidate;
    };

    sceneProgram_ = makeProgram(abyssal_flow::fragment, "Abyssal Bloom");
    blurProgram_ = makeProgram(abyssal_flow::blurFragment,
                               "Abyssal Bloom blur");
    compositeProgram_ = makeProgram(abyssal_flow::compositeFragment,
                                    "background composite");
    return sceneProgram_ != nullptr && compositeProgram_ != nullptr;
}

bool OceanShaderBackground::ensureRenderTargets(int outputWidth,
                                                int outputHeight,
                                                float renderingScale)
{
    using namespace juce::gl;

    const auto defaultFramebuffer =
        juce::OpenGLFrameBuffer::getCurrentFrameBufferTarget();
    const auto isComplete = [defaultFramebuffer](
        const juce::OpenGLFrameBuffer& target)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, target.getFrameBufferID());
        const auto complete =
            glCheckFramebufferStatus(GL_FRAMEBUFFER)
                == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
        return complete;
    };

    // At 2x DPI this is still at least 1.1 physical pixels per logical UI
    // pixel, so the organic field remains smooth while avoiding a 4x fragment
    // cost on Retina. A 1x display keeps native logical resolution.
    const auto baseScale = renderingScale > 1.25f ? 0.60f : 1.0f;
    const auto fitScale = std::min(
        { baseScale,
          1600.0f / static_cast<float>(juce::jmax(1, outputWidth)),
          1000.0f / static_cast<float>(juce::jmax(1, outputHeight)) });
    const auto targetWidth = juce::jmax(
        1, juce::roundToInt(static_cast<float>(outputWidth) * fitScale));
    const auto targetHeight = juce::jmax(
        1, juce::roundToInt(static_cast<float>(outputHeight) * fitScale));

    const auto sceneMatches =
        sceneTarget_.isValid()
        && sceneTarget_.getWidth() == targetWidth
        && sceneTarget_.getHeight() == targetHeight;

    if (! sceneMatches)
    {
        if (sceneAllocationFailed_
            && attemptedSceneWidth_ == targetWidth
            && attemptedSceneHeight_ == targetHeight)
            return false;

        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
        sceneTarget_.release();
        bloomPingTarget_.release();
        bloomPongTarget_.release();
        attemptedSceneWidth_ = targetWidth;
        attemptedSceneHeight_ = targetHeight;
        bloomReady_ = false;
        sceneAllocationFailed_ = false;
        bloomAllocationFailed_ = false;
        attemptedBloomWidth_ = 0;
        attemptedBloomHeight_ = 0;

        if (! sceneTarget_.initialise(context_, targetWidth, targetHeight)
            || ! isComplete(sceneTarget_))
        {
            glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
            sceneTarget_.release();
            sceneAllocationFailed_ = true;
            DBG("Amanita Ocean: failed to create the reduced-resolution shader target");
            return false;
        }
    }

    if (blurProgram_ == nullptr)
    {
        bloomReady_ = false;
        return true;
    }

    const auto bloomWidth = juce::jmax(32, (targetWidth + 1) / 2);
    const auto bloomHeight = juce::jmax(32, (targetHeight + 1) / 2);
    const auto bloomMatches =
        bloomPingTarget_.isValid()
        && bloomPongTarget_.isValid()
        && bloomPingTarget_.getWidth() == bloomWidth
        && bloomPingTarget_.getHeight() == bloomHeight
        && bloomPongTarget_.getWidth() == bloomWidth
        && bloomPongTarget_.getHeight() == bloomHeight;

    if (bloomMatches)
    {
        bloomReady_ = true;
        return true;
    }

    if (bloomAllocationFailed_
        && attemptedBloomWidth_ == bloomWidth
        && attemptedBloomHeight_ == bloomHeight)
    {
        bloomReady_ = false;
        return true;
    }

    bloomPingTarget_.release();
    bloomPongTarget_.release();
    attemptedBloomWidth_ = bloomWidth;
    attemptedBloomHeight_ = bloomHeight;
    bloomAllocationFailed_ = false;

    if (bloomPingTarget_.initialise(context_, bloomWidth, bloomHeight)
        && isComplete(bloomPingTarget_)
        && bloomPongTarget_.initialise(context_, bloomWidth, bloomHeight)
        && isComplete(bloomPongTarget_))
    {
        bloomReady_ = true;
        return true;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebuffer);
    bloomPingTarget_.release();
    bloomPongTarget_.release();
    bloomReady_ = false;
    bloomAllocationFailed_ = true;
    DBG("Amanita Ocean: GPU bloom unavailable; using the base shader only");
    return true;
}

bool OceanShaderBackground::renderBlurPass(
    GLuint sourceTexture,
    juce::OpenGLFrameBuffer& destination,
    float directionX,
    float directionY)
{
    using namespace juce::gl;

    if (blurProgram_ == nullptr || ! destination.isValid()
        || sourceTexture == 0)
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, destination.getFrameBufferID());
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return false;

    glViewport(0, 0, destination.getWidth(), destination.getHeight());
    blurProgram_->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    blurProgram_->setUniform("uSource", 0);
    blurProgram_->setUniform("uDirection", directionX, directionY);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return true;
}

void OceanShaderBackground::releaseOpenGLResources() noexcept
{
    ready_.store(false, std::memory_order_release);
    sceneTarget_.release();
    bloomPingTarget_.release();
    bloomPongTarget_.release();
    sceneProgram_.reset();
    blurProgram_.reset();
    compositeProgram_.reset();
    attemptedSceneWidth_ = 0;
    attemptedSceneHeight_ = 0;
    attemptedBloomWidth_ = 0;
    attemptedBloomHeight_ = 0;
    sceneAllocationFailed_ = false;
    bloomReady_ = false;
    bloomAllocationFailed_ = false;

    if (vertexArray_ != 0)
    {
        context_.extensions.glDeleteVertexArrays(1, &vertexArray_);
        vertexArray_ = 0;
    }
}

OceanShaderBackground::RenderSnapshot
OceanShaderBackground::loadSnapshot() const noexcept
{
    RenderSnapshot snapshot;
    snapshot.algorithm = juce::jlimit(
        0, 3, algorithm_.load(std::memory_order_relaxed));
    snapshot.evolution = clampUnit(
        evolution_.load(std::memory_order_relaxed));
    snapshot.focus = clampUnit(focus_.load(std::memory_order_relaxed));
    snapshot.frozen = frozen_.load(std::memory_order_relaxed);
    snapshot.accent = juce::Colour(
        accentArgb_.load(std::memory_order_acquire));
    return snapshot;
}

void OceanShaderBackground::initialiseRenderedState(
    const RenderSnapshot& target) noexcept
{
    characterBlend_.fill(0.0f);
    characterBlend_[static_cast<std::size_t>(target.algorithm)] = 1.0f;
    renderedEvolution_ = target.evolution;
    renderedFocus_ = target.focus;
    renderedAccent_ = target.accent;
    motion_ = target.frozen ? 0.0f : 1.0f;
    renderedStateInitialised_ = true;
}

void OceanShaderBackground::advanceRenderedState(
    const RenderSnapshot& target,
    float elapsedSeconds) noexcept
{
    const auto characterAmount = smoothingAmount(elapsedSeconds, 0.55f);
    auto blendSum = 0.0f;
    for (std::size_t index = 0; index < characterBlend_.size(); ++index)
    {
        const auto destination = static_cast<int>(index) == target.algorithm
            ? 1.0f : 0.0f;
        characterBlend_[index] +=
            (destination - characterBlend_[index]) * characterAmount;
        blendSum += characterBlend_[index];
    }

    if (blendSum > 0.0001f)
        for (auto& blend : characterBlend_)
            blend /= blendSum;

    const auto controlAmount = smoothingAmount(elapsedSeconds, 0.18f);
    renderedEvolution_ +=
        (target.evolution - renderedEvolution_) * controlAmount;
    renderedFocus_ += (target.focus - renderedFocus_) * controlAmount;

    const auto colourAmount = smoothingAmount(elapsedSeconds, 0.42f);
    renderedAccent_ = renderedAccent_.interpolatedWith(target.accent,
                                                        colourAmount);

    const auto motionTime = target.frozen ? 0.32f : 0.70f;
    const auto motionAmount = smoothingAmount(elapsedSeconds, motionTime);
    motion_ += ((target.frozen ? 0.0f : 1.0f) - motion_) * motionAmount;
}
} // namespace amanita::ui
