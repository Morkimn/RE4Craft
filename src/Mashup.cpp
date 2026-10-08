#include "dllmain.h"
#include "Mashup.h"
#include "bridge/Bridge.h"
void MashupInit(){BridgeInit();}
void MashupTick(){BridgeTick();}
void MashupRender(IDirect3DDevice9* device){BridgeRender(device);}
bool MashupWantsInput(){return false;}
bool MashupCamera(CameraQuasiFPS* camera,float (*matrix)[4],QFPS_OFFSET* offset,CAMERA_POINT* aim){return BridgeCamera(camera,matrix,offset,aim);}
