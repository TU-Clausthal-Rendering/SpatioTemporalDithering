#pragma once
#include "Utils/SampleGenerators/CPUSampleGenerator.h"

namespace Falcor
{
    class SobolGenerator : public CPUSampleGenerator
    {
    public:
        SobolGenerator() = default;
        virtual ~SobolGenerator() = default;

        virtual uint32_t getSampleCount() const override { return 16u; }

        virtual void reset(uint32_t startID = 0) override { mCurSample = 0; }

        virtual float2 next() override
        {
            /*static const float2 kPattern[] =
            {
                float2(-0.46875, -0.46875), // 00
                float2(-0.15625, -0.28125), // 01
                float2(0.15625, -0.34375), // 02
                float2(0.46875, -0.40625), // 03
                float2(-0.28125, -0.15625), // 04
                float2(-0.09375, -0.09375), // 05
                float2(0.09375, -0.03125), // 06
                float2(0.28125, -0.21875), // 07
                float2(-0.34375, 0.15625), // 08
                float2(-0.03125, 0.09375), // 09
                float2(0.03125, 0.03125), // 10
                float2(0.34375, 0.21875), // 11
                float2(-0.40625, 0.46875), // 12
                float2(-0.21875, 0.28125), // 13
                float2(0.21875, 0.34375), // 14
                float2(0.40625, 0.40625), // 15
            };*/
            static const float2 kPattern[] =
            {
                float2(-0.375, -0.375),
                float2(-0.125, -0.375),
                float2(0.125, -0.375),
                float2(0.375, -0.375),
                float2(-0.375, -0.125),
                float2(-0.125, -0.125),
                float2(0.125, -0.125),
                float2(0.375, -0.125),
                float2(-0.375, 0.125),
                float2(-0.125, 0.125),
                float2(0.125, 0.125),
                float2(0.375, 0.125),
                float2(-0.375, 0.375),
                float2(-0.125, 0.375),
                float2(0.125, 0.375),
                float2(0.375, 0.375),
            };

            // permutation based on 4x4 dither matrix
            static constexpr uint kPermute[] = {
                0, 10, 2, 8,
                5, 15, 7, 13,
                1, 11, 3, 9,
                4, 14, 6, 12
            };

            //return kPattern[(mCurSample++) % getSampleCount()];
            return kPattern[kPermute[(mCurSample++) % getSampleCount()]];
        }

        uint32_t getCurSample() const { return mCurSample % getSampleCount(); }

    protected:
        uint32_t mCurSample = 0;
    };
}
