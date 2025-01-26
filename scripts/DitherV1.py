from pathlib import WindowsPath, PosixPath
from falcor import *

def render_graph_DefaultRenderGraph():
    g = RenderGraph('DefaultRenderGraph')
    g.create_pass('VBufferLighting', 'VBufferLighting', {'envMapIntensity': 0.25, 'ambientIntensity': 0.25, 'lightIntensity': 0.5, 'envMapMirror': False})
    g.create_pass('DitherVBuffer', 'DitherVBuffer', {})
    g.create_pass('ToneMapper', 'ToneMapper', {'outputSize': 'Default', 'useSceneMetadata': True, 'exposureCompensation': 0.0, 'autoExposure': False, 'filmSpeed': 100.0, 'whiteBalance': False, 'whitePoint': 6500.0, 'operator': 'Aces', 'clamp': True, 'whiteMaxLuminance': 1.0, 'whiteScale': 11.199999809265137, 'fNumber': 1.0, 'shutter': 1.0, 'exposureMode': 'AperturePriority'})
    g.create_pass('TAA', 'TAA', {'alpha': 0.10000000149011612, 'colorBoxSigma': 1.0, 'antiFlicker': False})
    g.create_pass('DLSSPass', 'DLSSPass', {'enabled': True, 'outputSize': 'Default', 'profile': 'DLAA', 'motionVectorScale': 'Relative', 'isHDR': True, 'useJitteredMV': False, 'sharpness': 0.0, 'exposure': 0.0})
    g.create_pass('UnpackVBuffer', 'UnpackVBuffer', {})
    g.create_pass('RayShadow', 'RayShadow', {})
    g.add_edge('DitherVBuffer.vbuffer', 'VBufferLighting.vbuffer')
    g.add_edge('VBufferLighting.color', 'ToneMapper.src')
    g.add_edge('ToneMapper.dst', 'TAA.colorIn')
    g.add_edge('DitherVBuffer.mvec', 'TAA.motionVecs')
    g.add_edge('ToneMapper.dst', 'DLSSPass.color')
    g.add_edge('DitherVBuffer.mvec', 'DLSSPass.mvec')
    g.add_edge('DitherVBuffer.depth', 'DLSSPass.depth')
    g.add_edge('DitherVBuffer.vbuffer', 'UnpackVBuffer.vbuffer')
    g.add_edge('UnpackVBuffer.posW', 'RayShadow.posW')
    g.add_edge('UnpackVBuffer.normalW', 'RayShadow.normalW')
    g.add_edge('RayShadow.visibility', 'VBufferLighting.visibilityBuffer')
    g.mark_output('DLSSPass.output')
    g.mark_output('TAA.taaOut')
    return g

DefaultRenderGraph = render_graph_DefaultRenderGraph()
try: m.addGraph(DefaultRenderGraph)
except NameError: None
