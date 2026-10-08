#pragma once
struct CAMERA_POINT;
struct QFPS_OFFSET;
class CameraQuasiFPS;
struct IDirect3DDevice9;
void MashupInit();
void MashupTick();
void MashupRender(IDirect3DDevice9* device);
bool MashupWantsInput();
bool MashupCamera(CameraQuasiFPS* camera, float (*matrix)[4], QFPS_OFFSET* offset, CAMERA_POINT* aim);
