// MEON ui - 모든 모달의 공통 래퍼. 설명은 ModalBase.h
#include "ModalBase.h"
#include "../theme/Tokens.h"
#include "../../core/util/Fold.h"
#include "../../MeonReduceMotion.h"

namespace meon::ui
{

namespace
{
    double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
    float clamp01 (double v) { return (float) juce::jlimit (0.0, 1.0, v); }
    const core::CubicBezier& boxEase()  { static const core::CubicBezier c (motion::foldEase[0], motion::foldEase[1], motion::foldEase[2], motion::foldEase[3]); return c; }
    const core::CubicBezier& easeOut()  { static const core::CubicBezier c (motion::easeOut[0], motion::easeOut[1], motion::easeOut[2], motion::easeOut[3]); return c; }
    const core::CubicBezier& easeIn()   { static const core::CubicBezier c (motion::easeIn[0], motion::easeIn[1], motion::easeIn[2], motion::easeIn[3]); return c; }

    juce::AffineTransform dialogTransform (juce::Rectangle<int> box, float dy, float scale)
    {
        // CSS transform: translateY(dy) scale(s) — 상자 가운데 기준으로 줄인 뒤 아래로
        const auto c = box.toFloat().getCentre();
        return juce::AffineTransform::scale (scale, scale, c.x, c.y).translated (0.0f, dy);
    }

    /** 닫히는 모달: 지워지기 직전 상자 그림을 4px 아래·98%·투명으로 (160ms), 덮개는 180ms ease-in 으로 사라진다. 누름을 받지 않는다 */
    class CloseGhost : public juce::Component, private juce::Timer
    {
    public:
        CloseGhost (juce::Image img, float imageScale, juce::Rectangle<int> boxArea, juce::Rectangle<int> dimArea, ModalBase::State fromState)
            : image (std::move (img)), scale (imageScale), box (boxArea), dim (dimArea), from (fromState)
        {
            setInterceptsMouseClicks (false, false);
            setWantsKeyboardFocus (false);
            setComponentID ("modalCloseGhost");
        }

        void start()
        {
            t0 = nowMs();
            startTimerHz (60);   // VBlank 가 없을 때도 (창이 가려지는 등) 끝나게
        }

        void paint (juce::Graphics& g) override
        {
            const double t = nowMs() - t0;
            const float dimA = from.dim * (1.0f - (float) easeIn() (clamp01 (t / motion::modalCloseDimMs)));
            const float p = (float) boxEase() (clamp01 (t / motion::modalCloseBoxMs));
            const float boxA = from.box * (1.0f - p);
            const float dy = from.dy + (motion::modalCloseDy - from.dy) * p;
            const float sc = from.scale + (motion::modalScale - from.scale) * p;
            if (dimA > 0.0f)
            {
                g.setColour (col::overlay.withMultipliedAlpha (dimA));
                g.fillRect (dim);
            }
            if (boxA > 0.0f && image.isValid())
            {
                g.setOpacity (boxA);
                g.drawImageTransformed (image, juce::AffineTransform::scale (1.0f / scale).translated ((float) box.getX(), (float) box.getY())
                                                   .followedBy (dialogTransform (box, dy, sc)));
            }
        }

        void parentHierarchyChanged() override { if (getParentComponent() == nullptr) finish(); }

    private:
        juce::Image image;
        float scale;
        juce::Rectangle<int> box, dim;
        ModalBase::State from;
        double t0 = 0.0;
        bool done = false;
        juce::VBlankAttachment vblank { this, [this] { tick(); } };

        void timerCallback() override { tick(); }
        void tick()
        {
            if (done) return;
            repaint();
            if (nowMs() - t0 >= juce::jmax (motion::modalCloseBoxMs, motion::modalCloseDimMs)) finish();
        }
        void finish()
        {
            if (done) return;
            done = true;
            stopTimer();
            setVisible (false);
            juce::Component::SafePointer<juce::Component> sp (this);
            juce::MessageManager::callAsync ([sp] { delete sp.getComponent(); });   // 부모에서 빠진다
        }
    };
}

//==============================================================================
ModalBase::ModalBase() = default;
ModalBase::~ModalBase() { fallback.stopTimer(); }

juce::AffineTransform ModalBase::boxTransform() const
{
    return dialogTransform (dialogBounds(), state.dy, state.scale);
}

void ModalBase::paint (juce::Graphics& g)
{
    if (state.dim > 0.0f)
    {
        g.setColour (col::overlay.withMultipliedAlpha (state.dim));
        g.fillRect (dimBounds());
    }
    if (state.box <= 0.0f) return;
    if (! opening)
    {
        paintDialog (g);
        return;
    }
    juce::Graphics::ScopedSaveState s (g);
    g.addTransform (boxTransform());
    g.beginTransparencyLayer (state.box);
    paintDialog (g);
    g.endTransparencyLayer();
}

void ModalBase::parentHierarchyChanged() { maybeStart(); }
void ModalBase::visibilityChanged()      { maybeStart(); }

void ModalBase::maybeStart()
{
    if (started || ! isShowing()) return;
    started = true;
    if (systemPrefersReducedMotion()) return;   // 동작 줄이기: 바로 다 보인다
    opening = true;
    startMs = nowMs();
    state = { 0.0f, 0.0f, motion::modalOpenDy, motion::modalScale };
    applyToChildren();
    if (vblank == nullptr) vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { frame(); });
    fallback.startTimerHz (60);
    repaint();
}

void ModalBase::frame()
{
    if (! opening) return;
    const double t = nowMs() - startMs;
    const float d = (float) easeOut() (clamp01 (t / motion::modalOpenDimMs));
    const float p = (float) boxEase() (clamp01 (t / motion::modalOpenBoxMs));
    state = { d, p, motion::modalOpenDy * (1.0f - p), motion::modalScale + (1.0f - motion::modalScale) * p };
    if (t >= juce::jmax (motion::modalOpenDimMs, motion::modalOpenBoxMs)) { finishOpen(); return; }
    applyToChildren();
    repaint();
}

void ModalBase::finishOpen()
{
    opening = false;
    state = {};
    applyToChildren();   // 변환 없음 (정확히 100%), 원래 투명도
    fallback.stopTimer();
    repaint();
}

void ModalBase::applyToChildren()
{
    const auto t = opening ? boxTransform() : juce::AffineTransform();
    for (auto* c : getChildren())
    {
        float base = c->getAlpha();
        bool known = false;
        for (auto& [sp, a] : baseAlpha)
            if (sp.getComponent() == c) { base = a; known = true; }
        if (! known) baseAlpha.emplace_back (c, base);
        c->setTransform (t);
        c->setAlpha (opening ? base * state.box : base);
    }
}

void ModalBase::playCloseAnimation()
{
    auto* parent = getParentComponent();
    if (parent == nullptr || ! isShowing() || systemPrefersReducedMotion()) return;
    if ((bool) parent->getProperties()[noCloseAnimationProperty]) return;
    const auto from = state;   // 열리는 중이었으면 그 모습에서 이어서 닫힌다
    // 상자만 (덮개 없이, 변환 없이) 그림으로
    opening = false;
    state = { 0.0f, 1.0f, 0.0f, 1.0f };
    applyToChildren();
    fallback.stopTimer();
    float scale = 1.0f;
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        scale = (float) d->scale;
    scale = juce::jmax (1.0f, scale * juce::Component::getApproximateScaleFactorForComponent (this));
    const auto box = dialogBounds();
    auto image = createComponentSnapshot (box, true, scale);
    auto* ghost = new CloseGhost (image, scale, box, dimBounds(), from);
    parent->addAndMakeVisible (ghost, parent->getIndexOfChildComponent (this) + 1);
    ghost->setBounds (getBounds());
    ghost->start();
}

} // namespace meon::ui
