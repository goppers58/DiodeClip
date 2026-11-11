#pragma once
// クリップ曲線(JUCE非依存)。
//   正規化した入力 u = |x|/Vf に対して  f(u) = u / (1 + u^p)^(1/p)   (p = knee、大きいほど角が鋭い)
//   出力 = Vf * f(u) を基本に、slope で頭打ち後の傾きを足す:  y = c + slope * (|x| - c)
// f(u) は u/(1+u) = s ∈ [0,1) を引数にしたテーブルで引く。
// sで引くので、入力が非常に大きくても(漸近値1に収束するので)テーブルの外に出ない。
#include <array>
#include <cmath>

namespace clip
{
    inline constexpr int   kNumKnees = 6;
    inline constexpr float kKneeP[kNumKnees]     = { 1.0f, 1.5f, 2.0f, 4.0f, 8.0f, 16.0f };
    inline constexpr const char* kKneeNames[kNumKnees] = { "Very soft", "Soft", "Medium", "Hard", "Sharp", "Very sharp" };
    inline constexpr int   kTableSize = 4096;

    struct Tables
    {
        std::array<std::array<float, kTableSize + 2>, kNumKnees> t {};

        Tables()
        {
            for (int k = 0; k < kNumKnees; ++k)
            {
                const double p = (double) kKneeP[k];
                for (int i = 0; i <= kTableSize + 1; ++i)
                {
                    const double s = (double) i / (double) kTableSize;
                    if (s >= 1.0) { t[(size_t) k][(size_t) i] = 1.0f; continue; }
                    const double u = s / (1.0 - s);
                    t[(size_t) k][(size_t) i] = (float) (u / std::pow (1.0 + std::pow (u, p), 1.0 / p));
                }
            }
        }
    };

    // 初回呼び出しで構築される。オーディオスレッドの外(コンストラクタ)で一度呼んでおくこと
    inline const Tables& tables()
    {
        static const Tables instance;
        return instance;
    }

    inline const float* table (int kneeIndex) noexcept
    {
        if (kneeIndex < 0) kneeIndex = 0;
        if (kneeIndex >= kNumKnees) kneeIndex = kNumKnees - 1;
        return tables().t[(size_t) kneeIndex].data();
    }

    inline float curve (const float* t, float s) noexcept
    {
        if (! (s < 1.0f))                 // s>=1 または NaN
            return t[kTableSize];
        const float pos = s * (float) kTableSize;
        const int   i   = (int) pos;
        const float fr  = pos - (float) i;
        return t[i] + fr * (t[i + 1] - t[i]);
    }

    // 片側分: vf, invVf(=1/vf), slope, そのkneeのテーブル
    inline float shape (float x, float vf, float invVf, float slope, const float* t) noexcept
    {
        const float a = std::abs (x);
        const float u = a * invVf;
        const float c = vf * curve (t, u / (1.0f + u));
        const float m = c + slope * (a - c);
        return x >= 0.0f ? m : -m;
    }
}
