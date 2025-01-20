add_plugin(DecimaTAA)

target_sources(DecimaTAA PRIVATE
    DecimaTAA.cpp
    DecimaTAA.h
    Sharpen.ps.slang
    TAA.ps.slang
    DecimaSamplePattern.h
)

target_copy_shaders(DecimaTAA RenderPasses/DecimaTAA)

target_source_group(DecimaTAA "RenderPasses")
