#include "PluginEditor.h"

//==============================================================================
ClipSection::ClipSection (APVTS& a, const juce::String& sfx)
    : apvts (a), suffix (sfx),
      vf    (a, "vf"    + sfx, "Vf", " V"),
      knee  (a, "knee"  + sfx, "Knee"),
      slope (a, "slope" + sfx, "Slope")
{
    title.setText (sfx == "P" ? "UPPER (+)" : "LOWER (-)", juce::dontSendNotification);
    title.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, juce::Colour (0xffffa24a));

    for (int i = 0; i < presets::NumDiodes; ++i)
        preset.addItem (presets::kDiodes[i].name, i + 1);
    preset.setItemEnabled (1, false);                 // Custom は表示専用(選べない)
    preset.setSelectedId (1, juce::dontSendNotification);
    preset.onChange = [this] { applyPreset (preset.getSelectedId() - 1); };

    symUpper.setButtonText (juce::String::fromUTF8 ("Upper → Lower"));
    symUpper.setTooltip (juce::String::fromUTF8 ("UPPER側の Vf・Knee・Slope を LOWER にコピーして、上下を対称にします"));
    symUpper.onClick = [this] { copyParams ("P", "N"); };
    symLower.setButtonText (juce::String::fromUTF8 ("Lower → Upper"));
    symLower.setTooltip (juce::String::fromUTF8 ("LOWER側の Vf・Knee・Slope を UPPER にコピーして、上下を対称にします"));
    symLower.onClick = [this] { copyParams ("N", "P"); };

    addAndMakeVisible (title);
    addAndMakeVisible (preset);
    addAndMakeVisible (symUpper);
    addAndMakeVisible (symLower);
    addAndMakeVisible (vf);
    addAndMakeVisible (knee);
    addAndMakeVisible (slope);
}

void ClipSection::applyPreset (int idx)
{
    if (idx <= 0 || idx >= presets::NumDiodes)
        return;

    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    };

    const auto& pr = presets::kDiodes[idx];
    set ("vf"    + suffix, pr.vf);
    set ("knee"  + suffix, (float) pr.knee);
    set ("slope" + suffix, pr.slope);
}

void ClipSection::copyParams (const juce::String& from, const juce::String& to)
{
    for (const char* base : { "vf", "knee", "slope" })
    {
        const float v = apvts.getRawParameterValue (juce::String (base) + from)->load();
        if (auto* p = apvts.getParameter (juce::String (base) + to))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    }
}

void ClipSection::refreshFromParams()
{
    if (preset.isPopupActive())
        return;

    const float v = apvts.getRawParameterValue ("vf"    + suffix)->load();
    const int   k = (int) (apvts.getRawParameterValue ("knee" + suffix)->load() + 0.5f);
    const float s = apvts.getRawParameterValue ("slope" + suffix)->load();

    int id = 1;   // 一致するものがなければ Custom
    for (int i = 1; i < presets::NumDiodes; ++i)
        if (presets::matches (presets::kDiodes[i], v, k, s)) { id = i + 1; break; }

    if (preset.getSelectedId() != id)
        preset.setSelectedId (id, juce::dontSendNotification);
}

void ClipSection::resized()
{
    auto r = getLocalBounds().reduced (4);
    auto left = r.removeFromLeft (170);
    title.setBounds (left.removeFromTop (22));
    preset.setBounds (left.removeFromTop (28).reduced (0, 1));
    left.removeFromTop (4);
    symUpper.setBounds (left.removeFromTop (22));
    left.removeFromTop (2);
    symLower.setBounds (left.removeFromTop (22));

    const int w = r.getWidth() / 3;
    vf.setBounds    (r.removeFromLeft (w));
    knee.setBounds  (r.removeFromLeft (w));
    slope.setBounds (r);
}

//==============================================================================
void ScopeComponent::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff0e1013));

    auto get = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const float vfP = get ("vfP"), vfN = get ("vfN");
    const float sP  = get ("slopeP"), sN = get ("slopeN");
    const float bias = get ("bias");
    const float* tabP = clip::table ((int) (get ("kneeP") + 0.5f));
    const float* tabN = clip::table ((int) (get ("kneeN") + 0.5f));
    const float invP = 1.0f / vfP, invN = 1.0f / vfN;
    const float range = juce::jmax (1.0f, 1.5f * juce::jmax (vfP, vfN));   // 縦軸は ±range [V]

    auto toY = [&] (float v)
    {
        return b.getCentreY() - juce::jlimit (-range, range, v) / range * (b.getHeight() * 0.5f);
    };

    // 画面側で今のパラメータからクリップ後の波形を計算する(BIAS・Vf・Knee・Slope を動かすと、音が流れていなくても追従する)
    auto shaped = [&] (float v)
    {
        return v >= 0.0f ? clip::shape (v, vfP, invP, sP, tabP)
                         : clip::shape (v, vfN, invN, sN, tabN);
    };

    const float yP = toY (vfP);
    const float yN = toY (-vfN);

    // クリップ領域(Vfの外側)を薄く塗る
    g.setColour (juce::Colour (0x22ff5555));
    g.fillRect (b.getX(), b.getY(), b.getWidth(), yP - b.getY());
    g.fillRect (b.getX(), yN, b.getWidth(), b.getBottom() - yN);

    // 0V線とVf線
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.drawHorizontalLine ((int) b.getCentreY(), b.getX(), b.getRight());
    g.setColour (juce::Colour (0xffff6b6b));
    g.drawHorizontalLine ((int) yP, b.getX(), b.getRight());
    g.drawHorizontalLine ((int) yN, b.getX(), b.getRight());

    // 直近のサンプルを取り出し、入力(BIAS前)の立ち上がりゼロクロスから描く(波形を静止させる)
    constexpr int span = 1024, snap = 2048;
    const int w = proc.scope.writePos.load (std::memory_order_acquire);
    auto at = [&] (int i)
    {
        return proc.scope.in[(size_t) ((w - snap + i + ScopeBufferSize) % ScopeBufferSize)];
    };

    int start = 0;
    for (int i = 1; i < snap - span; ++i)
        if (at (i - 1) < 0.f && at (i) >= 0.f) { start = i; break; }

    juce::Path pin, pout;
    for (int i = 0; i < span; ++i)
    {
        const float x  = b.getX() + b.getWidth() * (float) i / (float) (span - 1);
        const float vin = at (start + i) + bias;           // クリップ段に入る電圧(BIAS込み)
        const float yi = toY (vin);
        const float yo = toY (shaped (vin));
        if (i == 0) { pin.startNewSubPath (x, yi); pout.startNewSubPath (x, yo); }
        else        { pin.lineTo (x, yi);          pout.lineTo (x, yo); }
    }
    g.setColour (juce::Colours::grey.withAlpha (0.7f));
    g.strokePath (pin, juce::PathStrokeType (1.2f));
    g.setColour (juce::Colour (0xffffa24a));
    g.strokePath (pout, juce::PathStrokeType (2.0f));

    // Vfの文字は波形の上に重ねて読めるようにする
    g.setColour (juce::Colour (0xffff8d8d));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("+Vf " + juce::String (vfP, 2) + " V", 8, (int) yP + 2,  140, 16, juce::Justification::left);
    g.drawText ("-Vf " + juce::String (vfN, 2) + " V", 8, (int) yN - 18, 140, 16, juce::Justification::left);

    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawRect (b, 1.0f);
}

//==============================================================================
DiodeClipperEditor::DiodeClipperEditor (DiodeClipperAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p),
      inKnob  (p.apvts, "in",  "INPUT", " dB"),
      outKnob (p.apvts, "out", "OUTPUT", " dB"),
      mixKnob (p.apvts, "mix", "DRY / WET", " %"),
      biasKnob (p.apvts, "bias", "BIAS (DC)", " V"),
      extraKnob (p.apvts, "extra", "EXTRA GAIN", " dB"),
      toneKnob (p.apvts, "tone", "TONE"),
      inCoupling (p.apvts, "inAC"), outCoupling (p.apvts, "outAC"),
      upper (p.apvts, "P"), lower (p.apvts, "N"), scope (p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &presetLabel, &presetBox, &osLabel, &osBox, &inKnob, &outKnob, &mixKnob, &biasKnob, &extraKnob, &toneKnob,
                                                             &inCoupling, &outCoupling, &upper, &lower, &scope })
        addAndMakeVisible (c);

    presetLabel.setText ("PRESET", juce::dontSendNotification);
    presetLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    presetLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffa24a));

    presetBox.addItem ("Custom", 1);
    for (int i = 0; i < presets::kNumPlugin; ++i)
        presetBox.addItem (presets::kPlugin[i].name, i + 2);
    presetBox.setItemEnabled (1, false);               // Custom は表示専用(選べない)
    presetBox.onChange = [this] { applyPluginPreset (presetBox.getSelectedId() - 2); };

    // OVERSAMPLING: 右上。PRESET欄と同じ作り
    osLabel.setText ("OVERSAMPLING", juce::dontSendNotification);
    osLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    osLabel.setColour (juce::Label::textColourId, juce::Colour (0xffffa24a));
    osLabel.setJustificationType (juce::Justification::centredRight);
    osBox.addItemList (juce::StringArray { "Off", "2x", "4x", "8x" }, 1);
    osAtt = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, "os", osBox);

    setSize (900, 620);
    proc.scopeActive.store (true);
    refreshPresetBox();
    upper.refreshFromParams();
    lower.refreshFromParams();
    startTimerHz (10);
}

DiodeClipperEditor::~DiodeClipperEditor()
{
    proc.scopeActive.store (false);
}

void DiodeClipperEditor::applyPluginPreset (int index)
{
    if (index < 0 || index >= presets::kNumPlugin)
        return;

    const auto& pr = presets::kPlugin[index];

    auto set = [this] (const juce::String& id, float v)
    {
        if (auto* prm = proc.apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };

    upper.applyPreset (pr.upper);
    lower.applyPreset (pr.lower);
    set ("mix",  pr.mix);
    set ("bias", pr.bias);
}

// 現在の値が、全体プリセット(上下のダイオード・Dry/Wet・BIAS)のどれかと一致するか調べて表示する
void DiodeClipperEditor::refreshPresetBox()
{
    if (presetBox.isPopupActive())
        return;

    auto get = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const float vP = get ("vfP"), sP = get ("slopeP"), vN = get ("vfN"), sN = get ("slopeN");
    const int   kP = (int) (get ("kneeP") + 0.5f), kN = (int) (get ("kneeN") + 0.5f);
    const float mix = get ("mix"), bias = get ("bias");

    int id = 1;   // 一致するものがなければ Custom
    for (int i = 0; i < presets::kNumPlugin; ++i)
    {
        const auto& pr = presets::kPlugin[i];
        if (presets::matches (presets::kDiodes[pr.upper], vP, kP, sP)
            && presets::matches (presets::kDiodes[pr.lower], vN, kN, sN)
            && std::abs (mix - pr.mix) < 0.06f && std::abs (bias - pr.bias) < 0.006f)
        {
            id = i + 2;
            break;
        }
    }

    if (presetBox.getSelectedId() != id)
        presetBox.setSelectedId (id, juce::dontSendNotification);
}

void DiodeClipperEditor::timerCallback()
{
    const bool rec = proc.isRecording.load (std::memory_order_relaxed);
    inCoupling.setLocked (rec);
    outCoupling.setLocked (rec);

    refreshPresetBox();
    upper.refreshFromParams();
    lower.refreshFromParams();
}

void DiodeClipperEditor::resized()
{
    auto r = getLocalBounds().reduced (8);

    auto bar = r.removeFromTop (30);
    presetLabel.setBounds (bar.removeFromLeft (70));
    presetBox.setBounds (bar.removeFromLeft (260).reduced (0, 2));
    osBox.setBounds (bar.removeFromRight (90).reduced (0, 2));
    bar.removeFromRight (8);
    osLabel.setBounds (bar.removeFromRight (120));
    r.removeFromTop (8);

    auto left  = r.removeFromLeft (130);
    auto right = r.removeFromRight (130);

    upper.setBounds (r.removeFromTop (120));
    lower.setBounds (r.removeFromBottom (120));
    scope.setBounds (r.reduced (0, 4));

    // INPUT / OUTPUT: ダイヤルの中心をスコープの0V線の高さに合わせる(Knobの縦の中心=ダイヤルの中心)
    const int cy = scope.getBounds().getCentreY();
    const int kx = 5;                                   // 列(幅130)の中での左余白。幅120のつまみを置く
    inKnob.setBounds  (left.getX()  + kx, cy - 85, 120, 170);
    outKnob.setBounds (right.getX() + kx, cy - 85, 120, 170);
    inCoupling.setBounds  (left.getX()  + kx, cy + 85, 120, 46);
    outCoupling.setBounds (right.getX() + kx, cy + 85, 120, 46);

    // BIAS / DRY/WET: 下側のVf・Knee・Slopeのつまみと同じ高さ・同じ大きさ
    const int rowY = lower.getY() + 4, rowH = lower.getHeight() - 8;
    biasKnob.setBounds (left.getX()  + kx, rowY, 120, rowH);
    mixKnob.setBounds  (right.getX() + kx, rowY, 120, rowH);

    // EXTRA GAIN: INPUTの上。上側のVf・Knee・Slopeのつまみと同じ大きさで、そこから extraGainDrop だけ下げてINPUTに近づける
    const int extraGainDrop = 40;
    extraKnob.setBounds (left.getX() + kx, upper.getY() + 4 + extraGainDrop, 120, upper.getHeight() - 8);
    toneKnob.setBounds  (right.getX() + kx, upper.getY() + 4 + extraGainDrop, 120, upper.getHeight() - 8);   // OUTPUTの上(EXTRA GAINと同じ高さ)
}
