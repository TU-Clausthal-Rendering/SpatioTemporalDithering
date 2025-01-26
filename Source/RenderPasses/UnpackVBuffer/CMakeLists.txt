add_plugin(UnpackVBuffer)

target_sources(UnpackVBuffer PRIVATE
    UnpackVBuffer.cpp
    UnpackVBuffer.h
    Unpack.cs.slang
)

target_copy_shaders(UnpackVBuffer RenderPasses/UnpackVBuffer)

target_source_group(UnpackVBuffer "RenderPasses")
