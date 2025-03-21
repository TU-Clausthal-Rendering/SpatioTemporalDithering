add_plugin(DitherVBufferRaster)

target_sources(DitherVBufferRaster PRIVATE
    DitherVBufferRaster.cpp
    DitherVBufferRaster.h
    DitherVBuffer.3D.slang
)

target_copy_shaders(DitherVBufferRaster RenderPasses/DitherVBufferRaster)

target_source_group(DitherVBufferRaster "RenderPasses")
