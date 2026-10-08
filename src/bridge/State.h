#pragma once
#include "../Game.h"
#include "Link.h"
#include "SolidGrid.h"
#include <mutex>
#include <map>
#include <vector>
namespace re4craft {
namespace P = skycraft::proto;
extern std::recursive_mutex guard;
extern P::McState guest;
extern bool owns;
extern float yaw,pitch,cursorX,cursorY;
extern unsigned viewportW,viewportH;
extern SolidGrid solids;
void RenderInit();
void RenderRoomReset();
void SyncNativeLights();
bool MinecraftVisible();
bool TerrainGround(float x,float z,float ceiling,float& y);
void StickArrow(cEm* actor,const P::McEvent& event);
void TerrainReset(unsigned epoch);
void TerrainUpdate(Vec feet,unsigned epoch,bool refresh);
void RenderGuest(IDirect3DDevice9* device);
void NativeDraw(IDirect3DDevice9* device);
void NativeTarget(IDirect3DDevice9* device,unsigned index,IDirect3DSurface9* next);
void NativeClear(IDirect3DDevice9* device,unsigned flags);
void NativeBegin();
void NativeEnd(IDirect3DDevice9* device);
}

