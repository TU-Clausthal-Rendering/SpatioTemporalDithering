/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#pragma once
#include "Core/Macros.h"
#include "Utils/Math/Vector.h"
#include "Utils/SampleGenerators/CPUSampleGenerator.h"

namespace Falcor
{
/**
 * Sample pattern generator for the Direct3D 8x MSAA/SSAA pattern.
 */
class DecimaSamplePattern : public CPUSampleGenerator
{
public:
    /**
     * @param[in] sampleCount The sample count. This must be 2 currently.
     * @return New object, or throws an exception on error.
     */
    static ref<DecimaSamplePattern> create(uint32_t sampleCount) { return make_ref<DecimaSamplePattern>(sampleCount); }

    DecimaSamplePattern(uint32_t sampleCount) {}
    virtual ~DecimaSamplePattern() = default;

    virtual uint32_t getSampleCount() const override { return kSampleCount; }

    virtual void reset(uint32_t startID = 0) override { mCurSample = 0; }

    virtual float2 next() override { return kPattern[(mCurSample++) % kSampleCount]; }

protected:
    uint32_t mCurSample = 0;
    const  uint32_t kSampleCount = 2;
    const float2 kPattern[2] = {
        float2(-0.5f, 0.0f), // left edge
        float2(0.0f, -0.5f) // top edge
    };
};
} // namespace Falcor
