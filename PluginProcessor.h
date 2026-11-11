#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include "ClipShaper.h"

inline constexpr int ScopeBufferSize = 8192;

// オーディオスレッド -> UIスレッドへ渡す表示用バッファ(ロックなし。表示専用なので多少の競合は許容)
struct ScopeBuffer
{
    std::array<float, ScopeBufferSize> in{};   // 入力ゲイン後・BIAS前の入力サンプル(クリップ後の波形は画面側で計算する)
    std::atomic<int> writePos { 0 };

    void push (float x) noexcept
    {
        const int w = writePos.load (std::memory_order_relaxed);
        in [(size_t) w] = x;
        writePos.store ((w + 1) % ScopeBufferSize, std::memory_order_release);
    }
};

class DiodeClipperAudioProcessor : public juce::AudioProcessor
{
public:
    DiodeClipperAudioProcessor();
    ~DiodeClipperAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    ScopeBuffer scope;
    std::atomic<bool> scopeActive { false };   // エディタが開いている間だけスコープ用の計算をする
    std::atomic<bool> isRecording { false };   // UI表示用(録音中はCouplingを操作不可にする)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // オーバーサンプリング: 0=Off, 1=2x, 2=4x, 3=8x(段数1〜3のものを用意しておき、選んだものだけ使う)
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> oversamplers;
    int osIdx = 2;
    double baseRate = 44100.0;
    void applyOversampling (int idx);   // prepareToPlay時と、選択が変わったときに呼ぶ(内部レート・レイテンシ・平滑化を作り直す)
    void resetSmoothers();
    float toneAlpha (float fc) const noexcept;   // TONE(1次ローパス)の係数。∞Hzは1(=素通し)

    std::atomic<float>* pIn = nullptr;     std::atomic<float>* pOut = nullptr;
    std::atomic<float>* pVfP = nullptr;    std::atomic<float>* pVfN = nullptr;
    std::atomic<float>* pKneeP = nullptr;  std::atomic<float>* pKneeN = nullptr;
    std::atomic<float>* pSlopeP = nullptr; std::atomic<float>* pSlopeN = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pBias = nullptr;
    std::atomic<float>* pTone = nullptr;    // TONE: ローパスのカットオフ(Hz)。最大値=∞Hz(バイパス)
    std::atomic<float>* pExtra = nullptr;   // EXTRA GAIN (dB)。INPUTのdBに足し算で重なる
    std::atomic<float>* pOs = nullptr;      // オーバーサンプリング(Choice)
    std::atomic<float>* pInAC = nullptr;   // 1 = AC coupling
    std::atomic<float>* pOutAC = nullptr;

    // カップリング用1次ハイパス(内部レートで動作)。状態はチャンネルごと
    struct DcBlock { float x1 = 0.f, y1 = 0.f; };
    std::array<DcBlock, 2> inHp, outHp;
    float hpR = 0.999f;
    double osRateHz = 192000.0;                     // 内部(オーバーサンプリング後)のレート
    float toneZ[2] = { 0.0f, 0.0f };                // TONE(1次ローパス)の状態
    bool inACActive = false, outACActive = false;   // 実際に適用中の状態(録音中は変更しない)
    bool inInit = true, outInit = true;            // 切替直後にフィルタ状態を現在値で初期化

    using Smoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    // 上側/下側のクリップ設定。動かしている間だけ毎サンプル更新し、止まっている間は計算しない
    struct Side
    {
        Smoother vf, slope, fade;                   // fade: Kneeを切り替えたときの旧→新のクロスフェード(0→1)
        const float* tab = nullptr;
        const float* prevTab = nullptr;
        int knee = 0;
        float vfv = 1.0f, inv = 1.0f, slv = 1.0f;   // 現在値(vf, 1/vf, slope)
    };
    Side sideP, sideN;
    Smoother sGin, sGout, sWet, sBias, sTone, sInAC, sOutAC;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiodeClipperAudioProcessor)
};
