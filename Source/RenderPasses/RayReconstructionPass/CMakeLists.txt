add_plugin(RayReconstructionPass)

target_sources(RayReconstructionPass PRIVATE
    RayReconstructionPass.cpp
    RayReconstructionPass.h
)

target_source_group(RayReconstructionPass "RenderPasses")
