"""otium.webgpu through wgpu-native, with no window."""
from harness import BUILD, Skip, test


@test('webgpu render', suite='native', needs=['ui'], timeout=120)
def render(t):
    config = (BUILD/'ui-config.ot').read_text()
    if '(webgpu "")' in config:
        raise Skip('this build has no wgpu-native')
    t.expect(t.otium('tests/webgpu/render.ot'), contains=['webgpu render: 3 checks passed'])
