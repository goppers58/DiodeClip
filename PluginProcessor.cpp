#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

juce::AudioProcessorValueTreeState::ParameterLayout DiodeClipperAudioProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    using ID = juce::ParameterID;

    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<P> (ID { "in",     1 }, "Input (dB)",  R (-24.f, 24.f, 0.1f), 0.f));
    l.add (std::make_unique<P> (ID { "extra",  1 }, "Extra Gain (dB)", R (0.f, 24.f, 0.1f), 0.f));
    {
        // TONE: 100Hz〜20kHz(対数的)。右端(20000)は ∞Hz = ローパスなし
        R toneRange (100.f, 20000.f, 1.f);
        toneRange.setSkewForCentre (1414.f);
        l.add (std::make_unique<P> (ID { "tone", 1 }, "Tone (Hz)", toneRange, 20000.f,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) -> juce::String
                {
                    if (v >= 19999.5f) return juce::String::fromUTF8 ("\xe2\x88\x9e Hz");
                    if (v >= 1000.0f)  return juce::String (v / 1000.0f, 2) + " kHz";
                    return juce::String ((int) std::lround (v)) + " Hz";
                })
                .withValueFromStringFunction ([] (const juce::String& s) -> float
                {
                    if (s.containsIgnoreCase ("inf") || s.contains (juce::String::fromUTF8 ("\xe2\x88\x9e")))
                        return 20000.0f;
                    float v = s.getFloatValue();
                    if (s.containsIgnoreCase ("k")) v *= 1000.0f;
                    return juce::jlimit (100.0f, 20000.0f, v);
                })));
    }
    l.add (std::make_unique<P> (ID { "out",    1 }, "Output (dB)", R (-24.f, 24.f, 0.1f), 0.f));
    l.add (std::make_unique<P> (ID { "vfP",    1 }, "Vf +",        R (0.01f, 5.f, 0.01f, 0.5f), presets::kDiodes[presets::Open].vf));
    l.add (std::make_unique<P> (ID { "vfN",    1 }, "Vf -",        R (0.01f, 5.f, 0.01f, 0.5f), presets::kDiodes[presets::Open].vf));
    juce::StringArray kneeNames;
    for (auto* n : clip::kKneeNames) kneeNames.add (n);
    l.add (std::make_unique<juce::AudioParameterChoice> (ID { "kneeP", 1 }, "Knee +", kneeNames, presets::kDiodes[presets::Open].knee));
    l.add (std::make_unique<juce::AudioParameterChoice> (ID { "kneeN", 1 }, "Knee -", kneeNames, presets::kDiodes[presets::Open].knee));
    l.add (std::make_unique<P> (ID { "slopeP", 1 }, "Slope +",     R (0.f, 1.f, 0.001f), presets::kDiodes[presets::Open].slope));
    l.add (std::make_unique<P> (ID { "slopeN", 1 }, "Slope -",     R (0.f, 1.f, 0.001f), presets::kDiodes[presets::Open].slope));
    l.add (std::make_unique<P> (ID { "mix",    1 }, "Dry/Wet (%)", R (0.f, 100.f, 0.1f), presets::kInit.mix));
    l.add (std::make_unique<P> (ID { "bias",   1 }, "Bias (V)",    R (-3.f, 3.f, 0.01f), presets::kInit.bias));
    l.add (std::make_unique<juce::AudioParameterChoice> (ID { "os", 1 }, "Oversampling",
                                                         juce::StringArray { "Off", "2x", "4x", "8x" }, 2));
    l.add (std::make_unique<juce::AudioParameterBool> (ID { "inAC",  1 }, "Input Coupling",  false));
    l.add (std::make_unique<juce::AudioParameterBool> (ID { "outAC", 1 }, "Output Coupling", false));
    return l;
}

DiodeClipperAudioProcessor::DiodeClipperAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pIn     = apvts.getRawParameterValue ("in");
    pOut    = apvts.getRawParameterValue ("out");
    pVfP    = apvts.getRawParameterValue ("vfP");
    pVfN    = apvts.getRawParameterValue ("vfN");
    pKneeP  = apvts.getRawParameterValue ("kneeP");
    pKneeN  = apvts.getRawParameterValue ("kneeN");
    pSlopeP = apvts.getRawParameterValue ("slopeP");
    pSlopeN = apvts.getRawParameterValue ("slopeN");
    pMix    = apvts.getRawParameterValue ("mix");
    pBias   = apvts.getRawParameterValue ("bias");
    pExtra  = apvts.getRawParameterValue ("extra");
    pTone   = apvts.getRawParameterValue ("tone");
    pOs     = apvts.getRawParameterValue ("os");
    pInAC   = apvts.getRawParameterValue ("inAC");
    pOutAC  = apvts.getRawParameterValue ("outAC");

    for (int i = 0; i < 3; ++i)
        oversamplers[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) (i + 1), juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);

    clip::tables();   // クリップ曲線のテーブルをここで構築(オーディオスレッドでは作らない)
}

bool DiodeClipperAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

float DiodeClipperAudioProcessor::toneAlpha (float fc) const noexcept
{
    if (fc >= 19999.5f)
        return 1.0f;   // ∞Hz: 係数1 = ローパスなし
    return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * fc / (float) osRateHz);
}

// 全パラメータの平滑化を、いまの値・いまの内部レートで作り直す
void DiodeClipperAudioProcessor::resetSmoothers()
{
    const double ramp = 0.02; // 20ms
    auto init = [&] (Smoother& s, float v) { s.reset (osRateHz, ramp); s.setCurrentAndTargetValue (v); };

    init (sGin,   juce::Decibels::decibelsToGain (pIn->load() + pExtra->load()));
    init (sGout,  juce::Decibels::decibelsToGain (pOut->load()));
    init (sWet,   pMix->load() * 0.01f);
    init (sBias,  pBias->load());
    init (sTone,  toneAlpha (pTone->load()));
    init (sInAC,  inACActive  ? 1.0f : 0.0f);
    init (sOutAC, outACActive ? 1.0f : 0.0f);

    auto initSide = [&] (Side& s, float vf, float slope, int knee)
    {
        init (s.vf, vf);  init (s.slope, slope);  init (s.fade, 1.0f);
        s.vfv = vf;  s.inv = 1.0f / vf;  s.slv = slope;
        s.knee = knee;  s.tab = s.prevTab = clip::table (knee);
    };
    initSide (sideP, pVfP->load(), pSlopeP->load(), (int) (pKneeP->load() + 0.5f));
    initSide (sideN, pVfN->load(), pSlopeN->load(), (int) (pKneeN->load() + 0.5f));
}

void DiodeClipperAudioProcessor::applyOversampling (int idx)
{
    osIdx = idx;
    osRateHz = baseRate * (double) (1 << idx);   // Off=x1, 2x, 4x, 8x

    if (idx > 0)
    {
        oversamplers[(size_t) idx - 1]->reset();
        setLatencySamples ((int) std::lround (oversamplers[(size_t) idx - 1]->getLatencyInSamples()));
    }
    else
        setLatencySamples (0);

    // 1次ハイパス fc = 5Hz
    hpR = std::exp (-juce::MathConstants<float>::twoPi * 5.0f / (float) osRateHz);
    inInit = outInit = true;
    inHp.fill ({});
    outHp.fill ({});
    toneZ[0] = toneZ[1] = 0.0f;
    resetSmoothers();
}

void DiodeClipperAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseRate = sampleRate;
    for (auto& o : oversamplers)
    {
        o->initProcessing ((size_t) samplesPerBlock);
        o->reset();
    }

    inACActive  = pInAC->load()  > 0.5f;
    outACActive = pOutAC->load() > 0.5f;
    applyOversampling (juce::jlimit (0, 3, (int) (pOs->load() + 0.5f)));
}

void DiodeClipperAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels());
    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    if (numCh == 0 || buffer.getNumSamples() == 0)
        return;

    // オーバーサンプリングの選択が変わったら切り替える
    {
        const int wantOs = juce::jlimit (0, 3, (int) (pOs->load() + 0.5f));
        if (wantOs != osIdx)
            applyOversampling (wantOs);
    }

    // 録音中はCouplingの切替を受け付けない(ホストが録音状態を通知する場合)
    bool recording = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            recording = pos->getIsRecording();
    isRecording.store (recording, std::memory_order_relaxed);

    if (! recording)
    {
        const bool wantIn  = pInAC->load()  > 0.5f;
        const bool wantOut = pOutAC->load() > 0.5f;
        if (wantIn  != inACActive)  { inACActive  = wantIn;  inInit  = true; }
        if (wantOut != outACActive) { outACActive = wantOut; outInit = true; }
    }

    // 目標値の更新(値が変わったときだけ平滑化が動く)
    const float gin = juce::Decibels::decibelsToGain (pIn->load() + pExtra->load());   // INPUT + EXTRA GAIN (dB)
    sGin.setTargetValue  (gin);
    sGout.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    sWet.setTargetValue  (pMix->load() * 0.01f);
    sBias.setTargetValue (pBias->load());
    sTone.setTargetValue (toneAlpha (pTone->load()));
    sInAC.setTargetValue  (inACActive  ? 1.0f : 0.0f);
    sOutAC.setTargetValue (outACActive ? 1.0f : 0.0f);

    auto updateSide = [] (Side& s, float vf, float slope, int knee)
    {
        s.vf.setTargetValue (vf);
        s.slope.setTargetValue (slope);
        if (knee != s.knee)   // Kneeを切り替えたら、旧カーブから新カーブへ20msでクロスフェード
        {
            s.prevTab = s.tab;
            s.tab = clip::table (knee);
            s.knee = knee;
            s.fade.setCurrentAndTargetValue (0.0f);
            s.fade.setTargetValue (1.0f);
        }
    };
    updateSide (sideP, pVfP->load(), pSlopeP->load(), (int) (pKneeP->load() + 0.5f));
    updateSide (sideN, pVfN->load(), pSlopeN->load(), (int) (pKneeN->load() + 0.5f));

    bool inNeedInit[2]  = { inInit, inInit };
    bool outNeedInit[2] = { outInit, outInit };
    inInit = outInit = false;

    // スコープ用: エディタが開いているときだけ、入力(ゲイン後)をベースレートで記録
    if (scopeActive.load (std::memory_order_relaxed))
    {
        const float* src = buffer.getReadPointer (0);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            scope.push (src[i] * gin);
    }

    // オーバーサンプリング(Offなら素通し) → クリップ → ダウンサンプリング
    // Dry/Wetは内部レートで混ぜるので、Dryも同じフィルタを通り位相が揃う
    juce::dsp::AudioBlock<float> block (buffer);
    auto sub = block.getSubsetChannelBlock (0, (size_t) numCh);
    auto up = osIdx > 0 ? oversamplers[(size_t) osIdx - 1]->processSamplesUp (sub) : sub;

    const int n = (int) up.getNumSamples();
    const int nch = (int) up.getNumChannels();

    // 動いている間だけ値を進める。fading=trueの間はKneeの新旧カーブを混ぜる
    auto stepSide = [] (Side& s, bool& fading, float& f)
    {
        if (s.vf.isSmoothing())    { s.vfv = s.vf.getNextValue(); s.inv = 1.0f / s.vfv; }
        if (s.slope.isSmoothing()) { s.slv = s.slope.getNextValue(); }
        fading = s.fade.isSmoothing();
        f = fading ? s.fade.getNextValue() : 1.0f;
    };
    auto evalSide = [] (const Side& s, float v, bool fading, float f)
    {
        const float yn = clip::shape (v, s.vfv, s.inv, s.slv, s.tab);
        if (! fading)
            return yn;
        const float yo = clip::shape (v, s.vfv, s.inv, s.slv, s.prevTab);
        return yo + f * (yn - yo);
    };

    for (int i = 0; i < n; ++i)
    {
        const float g  = sGin.getNextValue();
        const float go = sGout.getNextValue();
        const float w  = sWet.getNextValue();
        const float bs = sBias.getNextValue();
        const float ta = sTone.getNextValue();

        bool fadeP, fadeN;  float fP, fN;
        stepSide (sideP, fadeP, fP);
        stepSide (sideN, fadeN, fN);

        // カップリングのON/OFF切替もクロスフェード(a: 0=素通し, 1=ハイパス)
        const bool inRun  = inACActive  || sInAC.isSmoothing();
        const bool outRun = outACActive || sOutAC.isSmoothing();
        const float ia = inRun  ? sInAC.getNextValue()  : 0.0f;
        const float oa = outRun ? sOutAC.getNextValue() : 0.0f;

        for (int ch = 0; ch < nch; ++ch)
        {
            float* d = up.getChannelPointer ((size_t) ch);
            const size_t c = (size_t) juce::jmin (ch, 1);

            // 入力カップリング
            float x = d[i];
            if (inRun)
            {
                auto& ih = inHp[c];
                if (inNeedInit[c]) { ih.x1 = x; ih.y1 = x; inNeedInit[c] = false; }  // 開始直後は連続になるよう初期化
                const float xhp = x - ih.x1 + hpR * ih.y1;
                ih.x1 = x; ih.y1 = xhp;
                x += ia * (xhp - x);
            }

            const float v = x * g + bs;   // BIAS: クリップ段の入力にDC電圧を足す(Dryには影響しない)
            const float y = v >= 0.0f ? evalSide (sideP, v, fadeP, fP)
                                      : evalSide (sideN, v, fadeN, fN);
            float m = x * (1.0f - w) + y * w;

            // TONE: クリップ(とDry/Wet)の後、OUTPUTの前に入れる。係数1(∞Hz)のときは素通しで、状態だけ追従させる
            {
                float& tz = toneZ[c];
                if (ta < 1.0f) { tz += ta * (m - tz); m = tz; }
                else           { tz = m; }
            }
            m *= go;

            // 出力カップリング
            if (outRun)
            {
                auto& oh = outHp[c];
                if (outNeedInit[c]) { oh.x1 = m; oh.y1 = m; outNeedInit[c] = false; }
                const float ohp = m - oh.x1 + hpR * oh.y1;
                oh.x1 = m; oh.y1 = ohp;
                d[i] = m + oa * (ohp - m);
            }
            else
                d[i] = m;
        }
    }

    if (osIdx > 0)
        oversamplers[(size_t) osIdx - 1]->processSamplesDown (sub);
}

juce::AudioProcessorEditor* DiodeClipperAudioProcessor::createEditor()
{
    return new DiodeClipperEditor (*this);
}

void DiodeClipperAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DiodeClipperAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DiodeClipperAudioProcessor();
}
