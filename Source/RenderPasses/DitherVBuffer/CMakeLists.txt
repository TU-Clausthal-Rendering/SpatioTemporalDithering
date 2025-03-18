add_plugin(DitherVBuffer)

target_sources(DitherVBuffer PRIVATE
    DitherVBuffer.cpp
    DitherVBuffer.h
    DitherVBuffer.rt.slang
    Dither.slangh
    PermutationLookup.h
)

target_copy_shaders(DitherVBuffer RenderPasses/DitherVBuffer)

target_source_group(DitherVBuffer "RenderPasses")
