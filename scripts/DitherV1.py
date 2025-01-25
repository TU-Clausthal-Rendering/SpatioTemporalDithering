from pathlib import WindowsPath, PosixPath
from falcor import *

def render_graph_DefaultRenderGraph():
    g = RenderGraph('DefaultRenderGraph')
    g.create_pass('DitherVBuffer', 'DitherVBuffer', {})
    g.create_pass('VBufferLighting', 'VBufferLighting', {'envMapIntensity': 0.25, 'ambientIntensity': 0.25, 'lightIntensity': 0.5, 'envMapMirror': False})
    g.add_edge('DitherVBuffer.vbuffer', 'VBufferLighting.vbuffer')
    g.mark_output('VBufferLighting.color')
    return g

DefaultRenderGraph = render_graph_DefaultRenderGraph()
try: m.addGraph(DefaultRenderGraph)
except NameError: None
