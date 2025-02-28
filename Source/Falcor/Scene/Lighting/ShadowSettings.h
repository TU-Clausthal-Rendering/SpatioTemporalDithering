#pragma once
#include "Falcor.h"
#include "Utils/Properties.h"

using namespace Falcor;

class FALCOR_API ShadowSettings
{
public:
    enum class RayConeShadow
    {
        RawAlpha,
        AlphaTest,
        Saturated
    };

    FALCOR_ENUM_INFO(
        RayConeShadow,
        {
            { RayConeShadow::RawAlpha, "RawAlpha" },
            { RayConeShadow::AlphaTest, "AlphaTest" },
            { RayConeShadow::Saturated, "Saturated" },
        }
    );

    static ShadowSettings& get();

    void loadFromProperties(const Properties& props);
    Properties getProperties() const;
    void updateShaderVar(ref<Device> pDevice, ShaderVar& vars);
    void renderUI(Gui::Widgets& widget);
    DefineList getShaderDefines(Scene& scene, uint2 frameDim) const;
private:
    ShadowSettings();

    float mPointLightClip = 0.2f;
    bool mRayCones = false;
    bool mDiminishBorder = true;
    float mLodBias = 0.0f;
    RayConeShadow mRayConeShadow = RayConeShadow::RawAlpha;

    ref<Sampler> mpSampler;
};

FALCOR_ENUM_REGISTER(ShadowSettings::RayConeShadow);
