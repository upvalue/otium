"""otium.webgpu through wgpu-native: offscreen drawing, and surfaces for hidden windows."""
from harness import BUILD, Skip, test


@test('webgpu render', suite='native', needs=['ui'], timeout=120)
def render(t):
    needs_wgpu()
    t.expect(t.otium('tests/webgpu/render.ot'), contains=['webgpu render: 3 checks passed'])


@test('webgpu surface', suite='native', needs=['ui'], timeout=120, parallel=False)
def surface(t):
    needs_wgpu()
    t.expect(t.otium('tests/webgpu/surface.ot'), contains=['webgpu surface: 3 checks passed'])


def needs_wgpu():
    if '(webgpu "")' in (BUILD/'ui-config.ot').read_text():
        raise Skip('this build has no wgpu-native')
