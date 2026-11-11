#pragma once
// ダイオードの特性値と、プラグイン全体のプリセット(JUCE非依存)。
// プリセットを増減したいときは、このファイルだけを編集する。
#include <cmath>

namespace presets
{
    // ---- ダイオード1本分(上側/下側それぞれのPresetで使う) ----
    // knee: 0=Very soft, 1=Soft, 2=Medium, 3=Hard, 4=Sharp, 5=Very sharp  (ClipShaper.h の kKneeP と対応)
    struct Diode { const char* name; float vf; int knee; float slope; };

    enum DiodeId { Custom = 0, Open, BlueLED, RedLED, D1N4148, D1N4148x2, D1N60, Close, NumDiodes };

    inline constexpr Diode kDiodes[] = {
        { "Custom",    0.00f, 2, 0.00f },   // 手動(値は使わない)
        { "Open",      5.00f, 2, 1.00f },   // 素子なし: Slope=1 でクリップしない
        { "Blue LED",  2.55f, 5, 0.08f },
        { "Red LED",   1.55f, 4, 0.03f },
        { "1N4148",    0.50f, 3, 0.02f },
        { "1N4148 x2", 1.00f, 3, 0.04f },   // 2本直列(各側)
        { "1N60",      0.25f, 1, 0.01f },
        { "Close",     0.01f, 2, 0.00f },   // 短絡: Vf最小で出力をほぼ0に潰す
    };
    static_assert (sizeof (kDiodes) / sizeof (kDiodes[0]) == NumDiodes, "kDiodes と DiodeId の数が合っていません");

    // 現在の値がこのダイオードのプリセットと一致するか。Openは Slope=1 なら Vf/Knee を問わない
    inline bool matches (const Diode& d, float vf, int knee, float slope)
    {
        if (d.slope >= 0.999f)
            return slope >= 0.999f;
        return std::fabs (vf - d.vf) < 0.006f && knee == d.knee && std::fabs (slope - d.slope) < 0.0006f;
    }

    // ---- プラグイン全体のプリセット。ダイオード(上下)・Dry/Wet・BIAS だけを切り替える ----
    // INPUT / OUTPUT / Coupling は含めない
    struct Plugin { const char* name; int upper, lower; float mix, bias; };

    inline constexpr Plugin kPlugin[] = {
        { "Init", Open,       Open,       100.0f,  0.0f },
        { "TS",   D1N4148,    D1N4148,    100.0f,  0.0f },
        { "OD",   D1N4148x2,  D1N4148,    100.0f,  0.0f },
        { "Guv",  RedLED,     RedLED,     100.0f,  0.0f },
        { "BB",   D1N4148x2,  D1N4148x2,  100.0f,  0.0f },
        { "Cent", D1N60,      D1N60,       50.0f,  0.0f },
        { "Tim",  D1N4148x2,  D1N4148x2,  100.0f, -0.5f },
    };
    inline constexpr int kNumPlugin = (int) (sizeof (kPlugin) / sizeof (kPlugin[0]));

    // 起動時の既定値は Init と同じ(上下とも Open、Dry/Wet 100、他は0)
    inline constexpr const Plugin& kInit = kPlugin[0];
}
