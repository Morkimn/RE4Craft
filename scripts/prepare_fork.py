"""Prepare pinned re4_tweaks for the local VS 2022 build, without auto-installing."""
from pathlib import Path
import re
import shutil
import hashlib

ROOT = Path(__file__).resolve().parents[1]
FORK = ROOT / 'tools/re4_tweaks/dllmain'
font = ROOT / 'build/dependencies/fa-solid-900.ttf'
if not font.is_file():
    raise RuntimeError('Missing Font Awesome Free; run scripts/Fetch-Sources.ps1 first.')
font_bytes = font.read_bytes()
if not font_bytes.startswith(b'\x00\x01\x00\x00'):
    raise RuntimeError('Unexpected Font Awesome font payload.')
if hashlib.sha256(font_bytes).hexdigest() != 'af19d135d3a935b3ebfbd80320716ffe1202052c5f68dc2c5f1abc57005ac605':
    raise RuntimeError('Unexpected Font Awesome Free 6.7.2 checksum.')
# Public builds contain the freely redistributable, unmodified font, replacing
# the upstream embedded Pro icon font. No game content is exported here.
header = ROOT / 'tools/re4_tweaks/includes/RE4CraftFontAwesomeFree.hpp'
header.write_text('// Font Awesome Free 6.7.2, Fonticons Inc.; SIL OFL 1.1.\n'
                  '// See licenses/Font-Awesome-Free.txt. Font bytes are unmodified.\n'
                  '#pragma once\nstatic unsigned char RE4CraftFontAwesomeFree[] = {\n' +
                  ',\n'.join(','.join(f'0x{b:02x}' for b in font_bytes[i:i+24])
                             for i in range(0, len(font_bytes), 24)) + '\n};\n', encoding='utf-8')
p = FORK / 'dllmain.vcxproj'
s = p.read_text(encoding='utf-8')
s = s.replace('<LanguageStandard>stdcpp20</LanguageStandard>', '<LanguageStandard>stdcpp17</LanguageStandard>')
s = s.replace('<PlatformToolset>v145</PlatformToolset>', '<PlatformToolset>v143</PlatformToolset>')
s = s.replace('<WarningLevel>Level3</WarningLevel>', '<WarningLevel>Level3</WarningLevel><DebugInformationFormat>None</DebugInformationFormat>') if '<DebugInformationFormat>None</DebugInformationFormat>' not in s else s
s = s.replace('<SubSystem>Windows</SubSystem>', '<SubSystem>Windows</SubSystem><GenerateDebugInformation>false</GenerateDebugInformation>') if '<GenerateDebugInformation>false</GenerateDebugInformation>' not in s else s
s = re.sub(r'<PostBuildEvent>.*?</PostBuildEvent>', '<PostBuildEvent><Command /></PostBuildEvent>', s, flags=re.S)
s = re.sub(r'<PreBuildEvent>.*?</PreBuildEvent>', '<PreBuildEvent><Command /></PreBuildEvent>', s, flags=re.S)
p.write_text(s, encoding='utf-8')
(FORK / 'gitparams.h').write_text('#define GIT_CUR_COMMIT "92c0208bd09c29c9640e13a2493b8e6bd6edd700"\n#define GIT_BRANCH "minecraft-mashup"\n', encoding='utf-8')
print('Prepared VS 2022 project; installation events disabled.')

def patch_source(name, old, new):
    file = FORK / name
    text = file.read_text(encoding='utf-8-sig')
    if new in text:
        return
    if text.count(old) != 1:
        raise RuntimeError(f'{name}: expected one patch anchor, found {text.count(old)}')
    file.write_text(text.replace(old, new), encoding='utf-8')

# Standard-conforming member declarations (no change to layout).
patch_source('SDK/em.h', 'cEmMgr::GetClosestEm', 'GetClosestEm')
patch_source('SDK/global.h', 'struct GLOBAL_WK::RTP*', 'struct RTP*')
patch_source('SDK/message.h', 'struct Message::MessageFont*', 'struct MessageFont*')

for name in ('MinecraftCore.h','MinecraftCore.cpp','Mashup.h','Mashup.cpp'):
    shutil.copyfile(ROOT / 'src' / name, FORK / name)
shutil.copytree(ROOT / 'src' / 'bridge', FORK / 'bridge', dirs_exist_ok=True)
for name in ('dllmain.cpp','Game.cpp','Trainer.cpp','EndSceneHook.cpp'):
    patch_source(name, '#include "dllmain.h"', '#include "dllmain.h"\n#include "Mashup.h"')
patch_source('dllmain.cpp', 'Trainer_Init();', 'Trainer_Init();\n\tMashupInit();')
# Keep the custom fork from offering to replace itself with upstream's DLL.
patch_source('dllmain.cpp', 're4t::cfg->ReadSettings();', 're4t::cfg->ReadSettings();\n\tre4t::cfg->bNeverCheckForUpdates = true;\n\tre4t::cfg->iMouseTurnType = MouseTurnTypes::TypeB;')
patch_source('Game.cpp', '\tcSceSys__scheduler(thisptr, unused);', '\tcSceSys__scheduler(thisptr, unused);\n\tMashupTick();')
patch_source('Game.cpp', '\tcSceSys__scheduler(thisptr, unused);\n\tMashupTick();', '\tBridgeBeforeTick();\n\tcSceSys__scheduler(thisptr, unused);\n\tMashupTick();')
patch_source('Game.cpp', '#include "Mashup.h"', '#include "Mashup.h"\n#include "bridge/Bridge.h"')
patch_source('Input.cpp', '#include "input.hpp"', '#include "input.hpp"\n#include "bridge/Bridge.h"')
patch_source('Input.cpp', 'MSG details = *static_cast<const MSG*>(message_data);', 'MSG details = *static_cast<const MSG*>(message_data);\n\tif (BridgeWindowMessage(details)) return true;')
if 'MashupCamera(thisptr, p_offset, p_aim)' in (FORK/'Trainer.cpp').read_text(encoding='utf-8-sig'):
    patch_source('Trainer.cpp', 'MashupCamera(thisptr, p_offset, p_aim)', 'MashupCamera(thisptr, pl_mat, p_offset, p_aim)')
else:
    patch_source('Trainer.cpp', '\tif (!re4t::cfg->bTrainerEnableFreeCam)\n\t{\n\t\tCameraQuasiFPS__hitCheck', '\tif (MashupCamera(thisptr, pl_mat, p_offset, p_aim)) return;\n\tif (!re4t::cfg->bTrainerEnableFreeCam)\n\t{\n\t\tCameraQuasiFPS__hitCheck')
patch_source('Trainer.cpp', 'if (!re4t::cfg->bTrainerEnableFreeCam && *(uint8_t*)(regs.ebx + 0x1B6) == 0)', 'if (!BridgeOwnsInput() && !re4t::cfg->bTrainerEnableFreeCam && *(uint8_t*)(regs.ebx + 0x1B6) == 0)')
patch_source('Trainer.cpp', '#include "Mashup.h"', '#include "Mashup.h"\n#include "bridge/Bridge.h"')
patch_source('EndSceneHook.cpp', 'return (bCfgMenuOpen || bImGuiUIFocus ||', 'return (MashupWantsInput() || bCfgMenuOpen || bImGuiUIFocus ||')
patch_source('EndSceneHook.cpp', '\tTrainer_Update();', '\tTrainer_Update();\n\tMashupRender(pDevice);')
patch_source('EndSceneHook.cpp', '#include <faprolight.hpp>', '#include <RE4CraftFontAwesomeFree.hpp>')
patch_source('EndSceneHook.cpp', '&FAprolight, sizeof FAprolight', 'RE4CraftFontAwesomeFree, sizeof RE4CraftFontAwesomeFree')
# Draw exported Minecraft geometry before RE4 clears/reuses world depth or
# switches to its screen passes. Only the HUD belongs in EndScene.
patch_source('D3D9hook.cpp', '#include "D3D9Hook.h"', '#include "D3D9Hook.h"\n#include "bridge/Bridge.h"')
for name in ('DrawIndexedPrimitive','DrawIndexedPrimitiveUP','DrawPrimitive','DrawPrimitiveUP'):
    patch_source('D3D9hook.cpp', f'\treturn m_direct3DDevice9->{name}(', f'\tBridgeNativeDraw(m_direct3DDevice9);\n\treturn m_direct3DDevice9->{name}(')
patch_source('D3D9hook.cpp', '\treturn m_direct3DDevice9->SetRenderTarget(RenderTargetIndex, pRenderTarget);', '\tBridgeNativeTarget(m_direct3DDevice9, RenderTargetIndex, pRenderTarget);\n\treturn m_direct3DDevice9->SetRenderTarget(RenderTargetIndex, pRenderTarget);')
patch_source('D3D9hook.cpp', '\treturn m_direct3DDevice9->Clear(Count, pRects, Flags, Color, Z, Stencil);', '\tBridgeNativeClear(m_direct3DDevice9, Flags);\n\treturn m_direct3DDevice9->Clear(Count, pRects, Flags, Color, Z, Stencil);')
patch_source('D3D9hook.cpp', '\treturn m_direct3DDevice9->BeginScene();', '\tBridgeNativeBegin();\n\treturn m_direct3DDevice9->BeginScene();')
patch_source('D3D9hook.cpp', '\t// Used to render our ImGui interface', '\tBridgeNativeEnd(m_direct3DDevice9);\n\t// Used to render our ImGui interface')
patch_source('dllmain.h', '#define VERBOSE', '// VERBOSE disabled for the mashup player build')
# The user already declined executable modification. Keep their original EXE and
# suppress the repeated upstream offer on each diagnostic restart.
patch_source('WndProcHook.cpp', '\tLAACheck();', '\t// RE4Craft keeps the original game executable; upstream LAA prompt disabled.')
s=p.read_text(encoding='utf-8')
if '<ClCompile Include="MinecraftCore.cpp"' not in s:
    s=s.replace('<ClCompile Include="AudioTweaks.cpp"', '<ClCompile Include="MinecraftCore.cpp" /><ClCompile Include="Mashup.cpp" />\n    <ClCompile Include="AudioTweaks.cpp"')
    if '<ClCompile Include="MinecraftCore.cpp"' not in s:
        raise RuntimeError('Cannot locate project compile anchor')
    p.write_text(s,encoding='utf-8')
if '<ClCompile Include="bridge\\Bridge.cpp"' not in s:
    s=s.replace('<ClCompile Include="Mashup.cpp" />', '<ClCompile Include="Mashup.cpp" /><ClCompile Include="bridge\\Bridge.cpp" /><ClCompile Include="bridge\\Link.cpp" /><ClCompile Include="bridge\\Terrain.cpp" /><ClCompile Include="bridge\\Render.cpp" />')
    p.write_text(s,encoding='utf-8')
print('Mashup sources and native integration prepared.')
