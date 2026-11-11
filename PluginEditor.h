#pragma once
#include "PluginProcessor.h"
#include "Presets.h"

using APVTS = juce::AudioProcessorValueTreeState;

// ラベル付きロータリーノブ
struct Knob : juce::Component
{
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;
    std::unique_ptr<APVTS::SliderAttachment> att;

    // ラベルとテキストボックスの高さを同じ(18)にしているので、ダイヤルの中心はこのコンポーネントの縦の中心になる
    Knob (APVTS& apvts, const juce::String& id, const juce::String& name, const juce::String& suffix = {})
    {
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        slider.setTextValueSuffix (suffix);

        // 背景(0xff1b1d21)に埋もれないよう、軌跡は明るい灰色、値の弧は橙色、つまみの点は淡い橙色にする
        slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff5a616b));
        slider.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colour (0xffffa24a));
        slider.setColour (juce::Slider::thumbColourId,               juce::Colour (0xffffe3c2));
        addAndMakeVisible (label);
        addAndMakeVisible (slider);
        att = std::make_unique<APVTS::SliderAttachment> (apvts, id, slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (18));
        slider.setBounds (r);
    }
};

// COUPLING ON/OFF ボタン(ON=コンデンサが入った状態=直流をカット)
struct CouplingButton : juce::Component
{
    CouplingButton (APVTS& apvts, const juce::String& id)
    {
        button.setClickingTogglesState (true);
        button.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffa24a));
        button.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        button.onStateChange = [this] { button.setButtonText (button.getToggleState() ? "ON" : "OFF"); };
        button.setTooltip (juce::String::fromUTF8 ("ON: 5Hz以下(直流分)をカットする / OFF: そのまま通す"));
        addAndMakeVisible (button);
        att = std::make_unique<APVTS::ButtonAttachment> (apvts, id, button);
        button.setButtonText (button.getToggleState() ? "ON" : "OFF");
        label.setText ("COUPLING", juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (11.0f));
        addAndMakeVisible (label);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        button.setBounds (r.reduced (20, 2));
    }
    void setLocked (bool locked)
    {
        button.setEnabled (! locked);
        label.setText (locked ? "COUPLING (REC)" : "COUPLING", juce::dontSendNotification);
        label.setAlpha (locked ? 0.5f : 1.0f);
    }
    juce::TextButton button;
    juce::Label label;
    std::unique_ptr<APVTS::ButtonAttachment> att;
};

// 上側 or 下側のクリップ調整部: Preset / Vf / Knee / Slope
struct ClipSection : juce::Component
{
    ClipSection (APVTS& a, const juce::String& sfx);
    void applyPreset (int diodeIndex);   // kDiodes の番号。パラメータを書き換える
    void refreshFromParams();            // 現在の値に合うプリセット名を表示(合わなければ Custom)
    void copyParams (const juce::String& from, const juce::String& to);   // from側の Vf・Knee・Slope を to側へコピー("P"/"N")
    void resized() override;

    APVTS& apvts;
    juce::String suffix;   // "P" or "N"
    juce::Label title;
    juce::ComboBox preset;
    juce::TextButton symUpper, symLower; // 「Upper → Lower」「Lower → Upper」(コピー元 → コピー先)
    Knob vf, knee, slope;
};

// オシロスコープ
class ScopeComponent : public juce::Component, private juce::Timer
{
public:
    explicit ScopeComponent (DiodeClipperAudioProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override { repaint(); }
    DiodeClipperAudioProcessor& proc;
};

class DiodeClipperEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DiodeClipperEditor (DiodeClipperAudioProcessor&);
    ~DiodeClipperEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1b1d21)); }

private:
    void timerCallback() override;
    void applyPluginPreset (int index);   // kPlugin の番号
    void refreshPresetBox();

    DiodeClipperAudioProcessor& proc;
    juce::TooltipWindow tooltip { this, 600 };
    juce::Label presetLabel;
    juce::ComboBox presetBox;
    juce::Label osLabel;
    juce::ComboBox osBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> osAtt;
    Knob inKnob, outKnob, mixKnob, biasKnob, extraKnob, toneKnob;
    CouplingButton inCoupling, outCoupling;
    ClipSection upper, lower;
    ScopeComponent scope;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DiodeClipperEditor)
};
