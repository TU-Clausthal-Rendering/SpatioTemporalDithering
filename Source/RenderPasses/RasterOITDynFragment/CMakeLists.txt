add_plugin(RasterOITDynFragment)

target_sources(RasterOITDynFragment PRIVATE
    RasterOITDynFragment.cpp
    RasterOITDynFragment.h
    BuildList.3D.slang
)

target_copy_shaders(RasterOITDynFragment RenderPasses/RasterOITDynFragment)

target_source_group(RasterOITDynFragment "RenderPasses")
