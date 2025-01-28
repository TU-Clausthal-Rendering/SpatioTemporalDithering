#define SETTINGS_EXPORTS
#include "LightSettings.h"

namespace
{
    const std::string kAmbientIntensity = "ambientIntensity";
    const std::string kEnvMapIntensity = "envMapIntensity";
    const std::string kLightIntensity = "lightIntensity";
    const std::string kEnvMapMirror = "envMapMirror";
}

LightSettings FALCOR_API_EXPORT LightSettings::get()
{
    static LightSettings instance;
    return instance;
}

void LightSettings::loadFromProperties(const Properties& props)
{
    for (const auto& [key, value] : props)
    {
        if (key == kEnvMapIntensity) mEnvMapIntensity = value;
        else if (key == kAmbientIntensity) mAmbientIntensity = value;
        else if (key == kLightIntensity) mLightIntensity = value;
        else if (key == kEnvMapMirror) mEnvMapMirror = value;
        else logWarning("Unknown field '{}' in a Lighting dictionary.", key);
    }
}

Properties LightSettings::getProperties() const
{
    Properties d;
    d[kEnvMapIntensity] = mEnvMapIntensity;
    d[kAmbientIntensity] = mAmbientIntensity;
    d[kLightIntensity] = mLightIntensity;
    d[kEnvMapMirror] = mEnvMapMirror;
    return d;
}

void LightSettings::updateShaderVar(ShaderVar& vars) const
{
    vars["LightingCB"]["gAmbientIntensity"] = mAmbientIntensity;
    vars["LightingCB"]["gEnvMapIntensity"] = mEnvMapIntensity;
    vars["LightingCB"]["gLightIntensity"] = mLightIntensity;
    vars["LightingCB"]["gEnvMapMirror"] = mEnvMapMirror;
}

void LightSettings::renderUI(Gui::Widgets& widget)
{
    widget.var("Ambient Intensity", mAmbientIntensity, 0.f, 100.f, 0.1f);
    widget.var("Env Map Intensity", mEnvMapIntensity, 0.f, 100.f, 0.1f);
    widget.var("Scene Light Intensity", mLightIntensity, 0.f, 100.f, 0.1f);
    widget.checkbox("Env Map Mirror Reflections", mEnvMapMirror);
}
