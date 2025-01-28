#pragma once
#include "Falcor.h"
#include "Utils/Properties.h"

using namespace Falcor;

#ifdef SETTINGS_EXPORTS
#define SETTINGS_API __declspec(dllexport)
#else
#define SETTINGS_API __declspec(dllimport)
#endif

class SETTINGS_API LightSettings
{
public:
    static LightSettings get();

    void loadFromProperties(const Properties& props);
    Properties getProperties() const;
    void updateShaderVar(ShaderVar& vars) const;
    void renderUI(Gui::Widgets& widget);
private:
    float mEnvMapIntensity = 0.25f;
    float mAmbientIntensity = 0.25f;
    float mLightIntensity = 0.5f;
    bool mEnvMapMirror = false;
};
