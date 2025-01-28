add_plugin(RayTransparency)

target_sources(RayTransparency PRIVATE
    RayTransparency.cpp
    RayTransparency.h
)

target_source_group(RayTransparency "RenderPasses")
