#pragma once
#include <Windows.h>
struct IDirect3DDevice9;
struct IDirect3DSurface9;
struct CAMERA_POINT;
struct QFPS_OFFSET;
class CameraQuasiFPS;
void BridgeInit();
void BridgeBeforeTick();
void BridgeTick();
void BridgeRender(IDirect3DDevice9* device);
bool BridgeCamera(CameraQuasiFPS* camera, float (*matrix)[4], QFPS_OFFSET* offset, CAMERA_POINT* aim);
bool BridgeWindowMessage(const MSG& message);
bool BridgeOwnsInput();
void BridgeNativeDraw(IDirect3DDevice9* device);
void BridgeNativeTarget(IDirect3DDevice9* device, unsigned index, IDirect3DSurface9* next);
void BridgeNativeClear(IDirect3DDevice9* device, unsigned flags);
void BridgeNativeBegin();
void BridgeNativeEnd(IDirect3DDevice9* device);
