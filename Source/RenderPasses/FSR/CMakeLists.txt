add_plugin(FSR)

target_sources(FSR PRIVATE
    FSR.cpp
    FSR.h
)

target_link_libraries(FSR PRIVATE ffxapi)

target_source_group(FSR "RenderPasses")
