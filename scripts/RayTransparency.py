from pathlib import WindowsPath, PosixPath
from falcor import *

def render_graph_RayTransparency():
    g = RenderGraph('RayTransparency')
    g.create_pass('VBufferLighting', 'VBufferLighting', {'envMapIntensity': 0.25, 'ambientIntensity': 0.25, 'lightIntensity': 0.5, 'envMapMirror': False})
    g.create_pass('ToneMapper', 'ToneMapper', {'outputSize': 'Default', 'useSceneMetadata': True, 'exposureCompensation': 0.0, 'autoExposure': False, 'filmSpeed': 100.0, 'whiteBalance': False, 'whitePoint': 6500.0, 'operator': 'Linear', 'clamp': False, 'whiteMaxLuminance': 1.0, 'whiteScale': 11.199999809265137, 'fNumber': 1.0, 'shutter': 1.0, 'exposureMode': 'AperturePriority'})
    g.create_pass('TAA', 'TAA', {'alpha': 0.10000000149011612, 'colorBoxSigma': 1.0, 'antiFlicker': False})
    g.create_pass('DLSSPass', 'DLSSPass', {'enabled': True, 'outputSize': 'Default', 'profile': 'DLAA', 'motionVectorScale': 'Relative', 'isHDR': True, 'useJitteredMV': False, 'sharpness': 0.3499999940395355, 'exposure': 0.0})
    g.create_pass('UnpackVBuffer', 'UnpackVBuffer', {})
    g.create_pass('RayShadow', 'RayShadow', {})
    g.create_pass('RayTransparency', 'RayTransparency', {'useWhitelist': True, 'whitelist': 'CollectInner,Collectible,Smoke,TransparentPlane1,'})
    g.create_pass('ImageEquation0', 'ImageEquation', {'formula': 'float4(I0[xy].rgb+I0[xy].a*I1[xy].rgb, 1.0)', 'format': 'RGBA32Float'})
    g.create_pass('OutputSwitch', 'Switch', {'count': 2, 'selected': 0, 'i0': 'DLSS', 'i1': 'TAA'})
    g.add_edge('UnpackVBuffer.posW', 'RayShadow.posW')
    g.add_edge('UnpackVBuffer.normalW', 'RayShadow.normalW')
    g.add_edge('RayShadow.visibility', 'VBufferLighting.visibilityBuffer')
    g.add_edge('RayTransparency.vbuffer', 'UnpackVBuffer.vbuffer')
    g.add_edge('RayTransparency.vbuffer', 'VBufferLighting.vbuffer')
    g.add_edge('RayTransparency.mvec', 'TAA.motionVecs')
    g.add_edge('RayTransparency.mvec', 'DLSSPass.mvec')
    g.add_edge('RayTransparency.transparent', 'ImageEquation0.I0')
    g.add_edge('VBufferLighting.color', 'ImageEquation0.I1')
    g.add_edge('ImageEquation0.out', 'DLSSPass.color')
    g.add_edge('ImageEquation0.out', 'TAA.colorIn')
    g.add_edge('DLSSPass.output', 'OutputSwitch.i0')
    g.add_edge('TAA.colorOut', 'OutputSwitch.i1')
    g.add_edge('OutputSwitch.out', 'ToneMapper.src')
    g.add_edge('UnpackVBuffer.rasterZ', 'DLSSPass.depth')
    g.mark_output('ToneMapper.dst')
    return g

RayTransparency = render_graph_RayTransparency()
try: m.addGraph(RayTransparency)
except NameError: None
