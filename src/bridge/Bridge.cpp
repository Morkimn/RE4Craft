#include "../dllmain.h"
#include "Bridge.h"
#include "State.h"
#include "ToggleKey.h"
#include <cmath>
#include <algorithm>
#include <cstring>
namespace re4craft {
std::recursive_mutex guard;
P::McState guest{};
bool owns=false;
float yaw=0,pitch=0,cursorX=640,cursorY=360;
unsigned viewportW=1280,viewportH=720;
SolidGrid solids;
}
using namespace re4craft;
namespace {
auto& link=skycraft::Link::Get();
P::SkyState host{};
bool stopOwned=false,enabled=true,wasPlayable=false;
bool modeChanged=false,cameraOwned=false;
ToggleKey modeKey;
float savedCameraRatio=.8f;
cPlayer* heldPlayer=nullptr;
unsigned savedDraw=0,epoch=1,teleport=0,guestPid=0,nextId=1;
uint16_t savedCollision=0,room=0xffff;
int lastHp=0;
ULONGLONG lastLog=0;
bool keyHeld[512]{},buttonHeld[6]{};
bool absoluteValid=false;
POINT absolutePrevious{};
bool legacyValid=false;
POINT legacyPrevious{};
ULONGLONG lastRawMotion=0;
unsigned keyEvents=0,mouseEvents=0;
unsigned cameraCalls=0;
std::map<unsigned,cEm*> actors;
std::map<unsigned,unsigned> ids;
std::map<unsigned,Vec> beforeActorMove;
unsigned blockedMoves=0;
void actorShape(cEm& em,float& radius,float& height){
 // Native atari radius includes the vertical capsule ends. radius2 is the
 // horizontal radius; its center is offset above the feet.
 radius=std::clamp(em.atari_2B4.m_radius2_10/1000.f,.1f,3.f);
 height=std::clamp((em.atari_2B4.m_height_14+2*em.atari_2B4.m_radius_C)/1000.f,.2f,12.f);
}
void collideActors(){
 auto mgr=EmMgrPtr();if(!mgr||mgr->m_nArray_8>4096||solids.sections.empty())return;
 for(auto& em:*mgr){
  if(!em.IsValid()||!IsEnemy(em.id_100)||em.hp_324<=0)continue;
  auto i=beforeActorMove.find(em.guid_F8);Vec old=i==beforeActorMove.end()?em.pos_94:i->second;
  auto a=skycraft::SkyToMc(old),b=skycraft::SkyToMc(em.pos_94);
  if(std::abs(a.x-b.x)>32||std::abs(a.y-b.y)>16||std::abs(a.z-b.z)>32)continue;
  float radius,height;actorShape(em,radius,height);
  auto q=solids.sweep({a.x,a.y,a.z},{b.x,b.y,b.z},radius,height);
  Vec corrected=skycraft::McToSky(q.x,q.y,q.z),delta=corrected-em.pos_94;
  if(std::abs(delta.x)+std::abs(delta.y)+std::abs(delta.z)<.1f)continue;
  em.pos_94=em.pos_old_110=corrected;
  em.atari_2B4.m_Pos_30+=delta;em.atari_2B4.m_oldPos_3C+=delta;
  if(std::abs(delta.x)>.1f)em.speed_104.x=0;if(std::abs(delta.y)>.1f)em.speed_104.y=0;if(std::abs(delta.z)>.1f)em.speed_104.z=0;
  em.matUpdate();++blockedMoves;
 }
}
const char* blockReason="startup";
bool cutscene(){
 auto g=GlobalPtr();if(!g)return true;
 for(auto f:{Flags_STATUS::STA_MOVIE_ON,Flags_STATUS::STA_MOVIE2_ON,Flags_STATUS::STA_EVENT,Flags_STATUS::STA_DIEDEMO})
  if(FlagIsSet(g->flags_STATUS_0_501C,unsigned(f)))return true;
 return false;
}
bool playable(){
 auto g=GlobalPtr();auto p=PlayerPtr();
 if(!g||!p||!p->IsValid()){blockReason="no-player";return false;}
 if(g->Rno0_20!=3){blockReason="native-routine";return false;}
 if(g->playerHpCur_4FB4<=0){blockReason="dead";return false;}
 if(cutscene()){blockReason="cutscene";return false;}
 if((SubScreenWk&&SubScreenWk->open_flag_2C)||OptionOpenFlag()){blockReason="native-menu";return false;}
 if(EvtMgr&&EvtMgr->NowExeEvtName_1C[0]&&EvtMgr->IsAliveEvt(EvtMgr->NowExeEvtName_1C,nullptr,AliveEvtTypeNormal)){blockReason="event";return false;}
 // SYS_ROOMJUMP remains set during normal play (live room 0x100 proof). Routine0
 // and the actual read/door flags describe loading; ROOMJUMP is not a loading latch.
 for(auto f:{Flags_SYSTEM::SYS_DATA_READ,Flags_SYSTEM::SYS_LOAD_GAME,Flags_SYSTEM::SYS_DOORDEMO})if(FlagIsSet(g->Flags_SYSTEM_0_54,unsigned(f))){blockReason="loading";return false;}
 if(FlagIsSet(g->flags_STOP_0_170,unsigned(Flags_STOP::SPF_PL))){blockReason="native-stop";return false;}
 blockReason="none";return true;
}
void release(){
 if(owns)link.PushInput(P::kInReleaseAll,0);owns=false;
 memset(keyHeld,0,sizeof(keyHeld));memset(buttonHeld,0,sizeof(buttonHeld));absoluteValid=legacyValid=false;
 auto p=PlayerPtr();
 if(heldPlayer&&p==heldPlayer&&p->IsValid()){
  p->be_flag_4=(p->be_flag_4&~(UNIT_BE_FLAG_TRANS|UNIT_BE_FLAG_DRAWFOOTSHADOW))|savedDraw;
  const uint16_t mask=SAT_SCA_ENABLE|SAT_OBA_ENABLE;
  p->atari_2B4.m_flag_1A=(p->atari_2B4.m_flag_1A&~mask)|(savedCollision&mask);
 }
 if(cameraOwned){Game_SetCameraSmoothness(savedCameraRatio/.80000001f);cameraOwned=false;}
 if(stopOwned&&GlobalPtr())FlagSet(GlobalPtr()->flags_STOP_0_170,unsigned(Flags_STOP::SPF_PL),false);stopOwned=false;
 heldPlayer=nullptr;
}
void modeInput(bool down){if(modeKey.update(down)){enabled=!enabled;modeChanged=true;spd::log()->info("RE4CRAFT mode={} (F10)",enabled?"Minecraft":"Resident Evil 4");}}
void publishActors(){
 std::vector<P::ActorRecord> records;std::map<unsigned,cEm*> fresh;
 auto mgr=EmMgrPtr();auto p=PlayerPtr();
 if(p&&mgr&&mgr->m_nArray_8<4096)for(auto& em:*mgr){
  if(!em.IsValid()||!IsEnemy(em.id_100)||em.hp_324<=0||em.r_no_0_FC==unsigned(cEm::Routine0::Die))continue;
  auto delta=em.pos_94-p->pos_94;if(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>64000.f*64000.f)continue;
  auto it=ids.find(em.guid_F8);if(it==ids.end())it=ids.emplace(em.guid_F8,nextId++).first;
  P::ActorRecord a{};a.formId=it->second;a.flags=P::kActorHostile|P::kActorInCombat;
  auto m=skycraft::SkyToMc(em.pos_94);a.x=float(m.x);a.y=float(m.y);a.z=float(m.z);a.yaw=-em.ang_A0.y*57.2957795f;
  float radius,height;actorShape(em,radius,height);a.width=radius*2;a.height=height;
  a.healthFrac=em.hp_max_326>0?float(em.hp_324)/em.hp_max_326:1.f;a.level=1;
  const auto name=cEmMgr::EmIdToName(em.id_100,true);strncpy_s(a.name,name.c_str(),_TRUNCATE);
  records.push_back(a);fresh[a.formId]=&em;if(records.size()>=P::kMaxActors)break;
 }
 actors=std::move(fresh);link.WriteActors(records.data(),unsigned(records.size()));
}
void combat(){
 P::McEvent e{};int count=0;
 while(count++<P::kEventRingEntries&&link.PopEvent(e)){
  if(e.type==P::kEvHitActor&&std::isfinite(e.a)&&e.a>=0&&e.a<10000){
   auto it=actors.find(e.formId);if(it==actors.end()||!it->second->IsValid())continue;auto em=it->second;
   int old=em->hp_324,damage=int(std::ceil(e.a*100));em->hp_324=int16_t(std::max(0,old-damage));
   if(em->hp_324==0){
    // em10's native damageCheck (45AE3F -> 457D30) requires the hit latch.
    // Jumping to Die skips weapon-specific fall/death motion initialization.
    em->m_DmgInfo_328.m_Flag_4=1;em->m_DmgInfo_328.m_Timer_5=0;
    em->m_DmgInfo_328.m_Wep_6=0; // ordinary ballistic death, no dismemberment
    em->m_DmgInfo_328.m_PosFrom_8=PlayerPtr()->pos_94;
    em->m_DmgInfo_328.m_pDamageYarare_18=&em->yarare_344;
   }
   spd::log()->info("RE4CRAFT hit actor={} weapon={} flags={} MCdamage={} RE4hp={}->{}",e.formId,e.weapon,e.flags,e.a,old,em->hp_324);
  }else if(e.type==P::kEvArrowStuck){auto it=actors.find(e.formId);if(it!=actors.end()&&it->second->IsValid())StickArrow(it->second,e);
  }else if(e.type==P::kEvPlayerDied){GlobalPtr()->playerHpCur_4FB4=0;release();}
 }
}
uint16_t scancode(WPARAM vk,LPARAM lp){
 if(vk>='A'&&vk<='Z')return uint16_t(vk-'A'+4);if(vk>='1'&&vk<='9')return uint16_t(vk-'1'+30);
 if(vk=='0')return 39;if(vk>=VK_F1&&vk<=VK_F12)return uint16_t(vk-VK_F1+58);
 switch(vk){case VK_RETURN:return 40;case VK_ESCAPE:return 41;case VK_BACK:return 42;case VK_TAB:return 43;case VK_SPACE:return 44;
 case VK_OEM_MINUS:return 45;case VK_OEM_PLUS:return 46;case VK_OEM_4:return 47;case VK_OEM_6:return 48;case VK_OEM_5:return 49;
 case VK_OEM_1:return 51;case VK_OEM_7:return 52;case VK_OEM_3:return 53;case VK_OEM_COMMA:return 54;case VK_OEM_PERIOD:return 55;case VK_OEM_2:return 56;
 case VK_CAPITAL:return 57;case VK_INSERT:return 73;case VK_HOME:return 74;case VK_PRIOR:return 75;case VK_DELETE:return 76;case VK_END:return 77;case VK_NEXT:return 78;
 case VK_RIGHT:return 79;case VK_LEFT:return 80;case VK_DOWN:return 81;case VK_UP:return 82;
 case VK_CONTROL:return(lp&(1L<<24))?228:224;case VK_SHIFT:return((lp>>16)&255)==0x36?229:225;case VK_MENU:return(lp&(1L<<24))?230:226;default:return 0;}
}
bool keyInput(WPARAM vk,LPARAM lp,bool down){
 bool screen=(guest.flags&P::kMcScreenOpen)!=0;
 if(!screen&&(vk==VK_ESCAPE||vk=='G'))return false;
 if(!screen&&vk=='O'){if(down&&!(lp&(1L<<30)))link.PushInput(P::kInOpenMenu,0);return true;}
 auto sc=scancode(vk,lp);
 if(sc&&sc<512&&keyHeld[sc]!=down){keyHeld[sc]=down;link.PushInput(P::kInKey,sc,down?1:0);++keyEvents;}
 return true;
}
void mouseButton(unsigned button,bool down){
 if(button&&button<6&&buttonHeld[button]!=down){buttonHeld[button]=down;link.PushInput(P::kInMouseButton,uint16_t(button),down?1:0);++mouseEvents;}
}
void mouseMotion(float dx,float dy,bool screen){
 if(screen){cursorX=std::clamp(cursorX+dx,0.f,float(viewportW-1));cursorY=std::clamp(cursorY+dy,0.f,float(viewportH-1));link.PushInput(P::kInCursor,0,int(cursorX),int(cursorY));}
 else{float s=guest.sensitivity*.6f+.2f,f=s*s*s*8*.15f;yaw=std::remainder(yaw+dx*f,360.f);pitch=std::clamp(pitch+dy*f,-89.9f,89.9f);}
}
}
void BridgeInit(){if(link.Create())spd::log()->info("RE4CRAFT 0.3.1-alpha host adapter; SkyCraft protocol {} mapping Local\\RE4Craft_v1",P::kVersion);RenderInit();}
void BridgeBeforeTick(){
 std::lock_guard<std::recursive_mutex> lock(guard);
 if(modeChanged){release();wasPlayable=false;modeChanged=false;}
 if(owns&&GetForegroundWindow()!=hWindow){link.PushInput(P::kInReleaseAll,0);memset(keyHeld,0,sizeof(keyHeld));memset(buttonHeld,0,sizeof(buttonHeld));absoluteValid=false;}
 if(stopOwned&&GlobalPtr())FlagSet(GlobalPtr()->flags_STOP_0_170,unsigned(Flags_STOP::SPF_PL),false);stopOwned=false;
 SyncNativeLights(); // native light allocation/update stays on the game thread
 beforeActorMove.clear();auto mgr=EmMgrPtr();
 if(owns&&mgr&&mgr->m_nArray_8<4096)for(auto& em:*mgr)if(em.IsValid()&&IsEnemy(em.id_100)&&em.hp_324>0)beforeActorMove[em.guid_F8]=em.pos_94;
}
void BridgeTick(){
 std::lock_guard<std::recursive_mutex> lock(guard);if(!link.Valid())return;link.Heartbeat();
 auto g=GlobalPtr();auto p=PlayerPtr();bool running=enabled&&playable(),roomChanged=g&&room!=g->curRoomId_4FAC;
 if(roomChanged){release();room=g->curRoomId_4FAC;++epoch;++teleport;ids.clear();actors.clear();RenderRoomReset();TerrainReset(epoch);}
 if(running&&!wasPlayable){auto look=g->Camera_74.CamPoint_A4.Target_C-g->Camera_74.CamPoint_A4.Campos_0;
  yaw=std::atan2(-look.x,look.z)*57.2957795f;pitch=-std::atan2(look.y,std::hypot(look.x,look.z))*57.2957795f;++teleport;lastHp=g->playerHpCur_4FB4;
 }
 bool entering=running&&!wasPlayable;wasPlayable=running;bool alive=link.McAlive();P::McState fresh{};
 if(alive&&link.ReadMcState(fresh))guest=fresh;
 if(alive&&guestPid!=link.McPid()){guestPid=link.McPid();link.ResetOverlay();TerrainReset(epoch);++teleport;spd::log()->info("RE4CRAFT Minecraft connected pid={}",guestPid);}
 bool ready=running&&alive&&(guest.flags&P::kMcInWorld)&&guest.teleportAck==teleport&&!(guest.flags&P::kMcDead);
 if(!ready)release();else{
  if(!owns){heldPlayer=p;savedDraw=p->be_flag_4&(UNIT_BE_FLAG_TRANS|UNIT_BE_FLAG_DRAWFOOTSHADOW);savedCollision=p->atari_2B4.m_flag_1A;savedCameraRatio=Game_GetCameraSmoothness();cameraOwned=true;owns=true;spd::log()->info("RE4CRAFT Minecraft owns movement, input, inventory and weapons");}
  if(std::isfinite(guest.x)&&std::isfinite(guest.y)&&std::isfinite(guest.z)&&std::abs(guest.x)<1e6&&std::abs(guest.y)<1e6&&std::abs(guest.z)<1e6){
   Vec feet=skycraft::McToSky(guest.x,guest.y,guest.z);p->pos_94=p->pos_old_110=feet;p->speed_104=Vec{};p->ang_A0.y=-yaw*.0174532925f;
   p->be_flag_4&=~(UNIT_BE_FLAG_TRANS|UNIT_BE_FLAG_DRAWFOOTSHADOW);p->atari_2B4.m_flag_1A&=~(SAT_SCA_ENABLE|SAT_OBA_ENABLE);p->matUpdate();
  }
  if(lastHp>g->playerHpCur_4FB4){float damage=float(lastHp-g->playerHpCur_4FB4)*20.f/std::max(1,int(g->playerHpMax_4FB6));link.PushInput(P::kInHurt,P::kHurtMelee,int(damage*500));g->playerHpCur_4FB4=int16_t(lastHp);}
  lastHp=g->playerHpCur_4FB4;collideActors();combat();
  if(owns){FlagSet(g->flags_STOP_0_170,unsigned(Flags_STOP::SPF_PL),true);stopOwned=true;}
 }
 if(!owns){P::McEvent discarded{};unsigned count=0;while(count++<P::kEventRingEntries&&link.PopEvent(discarded)){};}
 if(running){TerrainUpdate(p->pos_94,epoch,entering||roomChanged);publishActors();}else link.WriteActors(nullptr,0);
 host.flags=running?P::kSkyInGame:P::kSkyMenuOpen;host.worldId=room;host.collisionEpoch=epoch;host.teleportSeq=teleport;
 if(p&&p->IsValid()){auto m=skycraft::SkyToMc(p->pos_94);host.posX=m.x;host.posY=m.y;host.posZ=m.z;}
 host.yaw=yaw;host.pitch=pitch;host.viewportW=viewportW;host.viewportH=viewportH;host.gameHour=12;link.WriteSkyState(host);
 if(GetTickCount64()-lastLog>5000){lastLog=GetTickCount64();spd::log()->info("RE4CRAFT state running={} reason={} owns={} guest={} flags={:x} ack={}/{} actors={} keys={} mouse={} cameras={} blockedMoves={} look={:.1f},{:.1f}",running,blockReason,owns,alive,guest.flags,guest.teleportAck,teleport,actors.size(),keyEvents,mouseEvents,cameraCalls,blockedMoves,yaw,pitch);}
}
bool re4craft::MinecraftVisible(){return owns&&enabled&&!cutscene();}
bool BridgeOwnsInput(){std::lock_guard<std::recursive_mutex> lock(guard);return MinecraftVisible();}
bool BridgeCamera(CameraQuasiFPS* camera,float (*matrix)[4],QFPS_OFFSET* offset,CAMERA_POINT* aim){
 std::lock_guard<std::recursive_mutex> lock(guard);if(!MinecraftVisible())return false;
 ++cameraCalls;
 float yr=yaw*.0174532925f,pr=pitch*.0174532925f;Vec forward{-std::sin(yr)*std::cos(pr),-std::sin(pr),std::cos(yr)*std::cos(pr)};
 Vec eye=PlayerPtr()->pos_94+Vec{0,std::clamp(guest.eyeHeight,.1f,3.f)*1000,0};
 if(guest.cameraMode==1)eye-=forward*(guest.cameraDistance*1000);if(guest.cameraMode==2){eye+=forward*(guest.cameraDistance*1000);forward*=-1;}
 memset(camera->m_pl_mat_178,0,sizeof(Mtx));for(int i=0;i<3;i++)camera->m_pl_mat_178[i][i]=1;
 camera->m_pl_mat_178[0][3]=eye.x;camera->m_pl_mat_178[1][3]=eye.y;camera->m_pl_mat_178[2][3]=eye.z;
 // move() copied m_pl_mat into a stack matrix BEFORE hitCheck. Its final
 // MTXMultVec uses that argument, not the member changed above (59E30B).
 memcpy(matrix,camera->m_pl_mat_178,sizeof(Mtx));
 // Only override this frame's aim; preserve native camera offset tables.
 Game_SetCameraSmoothness(0);
 aim->Campos_0=Vec{};aim->Target_C=forward*10000;aim->Roll_18=0;aim->Fovy_1C=std::clamp(guest.fovDeg,30.f,110.f);return true;
}
bool BridgeWindowMessage(const MSG& m){
 std::lock_guard<std::recursive_mutex> lock(guard);
 if(m.message==WM_KILLFOCUS||(m.message==WM_ACTIVATEAPP&&!m.wParam)){modeKey.reset();return false;}
 if((m.message==WM_KEYDOWN||m.message==WM_KEYUP||m.message==WM_SYSKEYDOWN||m.message==WM_SYSKEYUP)&&m.wParam==VK_F10){
  bool down=m.message==WM_KEYDOWN||m.message==WM_SYSKEYDOWN;
  if(!down||!(m.lParam&(1L<<30)))modeInput(down);return true;
 }
 // F10 remains available even when the bridge does not own keyboard input.
 if(m.message==WM_INPUT){RAWINPUT raw{};UINT bytes=sizeof(raw);
  // DirectInput's helper thread can deliver a complete raw down/up pair
  // before the render thread delivers the matching legacy pair. One latch
  // cannot deduplicate that ordering. F10 uses only the game's WM_KEY pair;
  // consume its raw duplicate without toggling or forwarding it to Minecraft.
  if(GetRawInputData(reinterpret_cast<HRAWINPUT>(m.lParam),RID_INPUT,&raw,&bytes,sizeof(RAWINPUTHEADER))!=UINT(-1)&&raw.header.dwType==RIM_TYPEKEYBOARD&&raw.data.keyboard.VKey==VK_F10&&GetForegroundWindow()==hWindow)return true;
 }
 if(!MinecraftVisible())return false;bool screen=(guest.flags&P::kMcScreenOpen)!=0;
 if(m.message==WM_KEYDOWN||m.message==WM_KEYUP||m.message==WM_SYSKEYDOWN||m.message==WM_SYSKEYUP){
  bool down=m.message==WM_KEYDOWN||m.message==WM_SYSKEYDOWN;
  if(m.wParam==VK_F4&&(m.lParam&(1L<<29)))return false;
  return keyInput(m.wParam,m.lParam,down);
 }
 if(m.message==WM_CHAR){if(screen&&m.wParam>=32)link.PushInput(P::kInText,0,int(m.wParam));return true;}
 if(m.message==WM_MOUSEMOVE){
  POINT point{SHORT(LOWORD(m.lParam)),SHORT(HIWORD(m.lParam))};
  if(screen){cursorX=std::clamp(float(point.x),0.f,float(viewportW-1));cursorY=std::clamp(float(point.y),0.f,float(viewportH-1));link.PushInput(P::kInCursor,0,int(cursorX),int(cursorY));}
  else if(legacyValid&&GetTickCount64()-lastRawMotion>100)mouseMotion(float(point.x-legacyPrevious.x),float(point.y-legacyPrevious.y),false);
  legacyPrevious=point;legacyValid=true;return true;
 }
 if(m.message==WM_INPUT){RAWINPUT raw{};UINT bytes=sizeof(raw);
  if(GetRawInputData(reinterpret_cast<HRAWINPUT>(m.lParam),RID_INPUT,&raw,&bytes,sizeof(RAWINPUTHEADER))==UINT(-1))return false;
  if(GetForegroundWindow()!=hWindow)return false;
  if(raw.header.dwType==RIM_TYPEKEYBOARD){
   auto& k=raw.data.keyboard;LPARAM lp=LPARAM(k.MakeCode)<<16;if(k.Flags&RI_KEY_E0)lp|=1L<<24;
   return keyInput(k.VKey,lp,(k.Flags&RI_KEY_BREAK)==0);
  }
  if(raw.header.dwType==RIM_TYPEMOUSE){
   if(raw.data.mouse.lLastX||raw.data.mouse.lLastY)lastRawMotion=GetTickCount64();
   float dx=float(raw.data.mouse.lLastX),dy=float(raw.data.mouse.lLastY);
   if(!(raw.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)){
    absoluteValid=false;mouseMotion(dx,dy,screen);
   }else{
    bool desktop=(raw.data.mouse.usFlags&MOUSE_VIRTUAL_DESKTOP)!=0;
    int width=GetSystemMetrics(desktop?SM_CXVIRTUALSCREEN:SM_CXSCREEN),height=GetSystemMetrics(desktop?SM_CYVIRTUALSCREEN:SM_CYSCREEN);
    POINT point{LONG(dx*width/65535.f),LONG(dy*height/65535.f)};
    if(desktop){point.x+=GetSystemMetrics(SM_XVIRTUALSCREEN);point.y+=GetSystemMetrics(SM_YVIRTUALSCREEN);}
    ScreenToClient(hWindow,&point);
    if(screen){cursorX=std::clamp(float(point.x),0.f,float(viewportW-1));cursorY=std::clamp(float(point.y),0.f,float(viewportH-1));link.PushInput(P::kInCursor,0,int(cursorX),int(cursorY));}
    else if(absoluteValid)mouseMotion(float(point.x-absolutePrevious.x),float(point.y-absolutePrevious.y),false);
    absolutePrevious=point;absoluteValid=true;
   }
   const USHORT flags=raw.data.mouse.usButtonFlags,downs[]={RI_MOUSE_LEFT_BUTTON_DOWN,RI_MOUSE_MIDDLE_BUTTON_DOWN,RI_MOUSE_RIGHT_BUTTON_DOWN,RI_MOUSE_BUTTON_4_DOWN,RI_MOUSE_BUTTON_5_DOWN};
   for(int i=0;i<5;i++){if(flags&downs[i])mouseButton(i+1,true);if(flags&(downs[i]<<1))mouseButton(i+1,false);}
   if(flags&RI_MOUSE_WHEEL)link.PushInput(P::kInScroll,0,SHORT(raw.data.mouse.usButtonData));
  }return true;
 }
 if(m.message==WM_LBUTTONDOWN||m.message==WM_LBUTTONUP)mouseButton(1,m.message==WM_LBUTTONDOWN);
 if(m.message==WM_MBUTTONDOWN||m.message==WM_MBUTTONUP)mouseButton(2,m.message==WM_MBUTTONDOWN);
 if(m.message==WM_RBUTTONDOWN||m.message==WM_RBUTTONUP)mouseButton(3,m.message==WM_RBUTTONDOWN);
 return m.message>=WM_MOUSEFIRST&&m.message<=WM_MOUSELAST;
}
void BridgeRender(IDirect3DDevice9* device){std::lock_guard<std::recursive_mutex> lock(guard);RenderGuest(device);}
void BridgeNativeDraw(IDirect3DDevice9* d){std::lock_guard<std::recursive_mutex> lock(guard);NativeDraw(d);}
void BridgeNativeTarget(IDirect3DDevice9* d,unsigned index,IDirect3DSurface9* next){std::lock_guard<std::recursive_mutex> lock(guard);NativeTarget(d,index,next);}
void BridgeNativeClear(IDirect3DDevice9* d,unsigned flags){std::lock_guard<std::recursive_mutex> lock(guard);NativeClear(d,flags);}
void BridgeNativeBegin(){std::lock_guard<std::recursive_mutex> lock(guard);NativeBegin();}
void BridgeNativeEnd(IDirect3DDevice9* d){std::lock_guard<std::recursive_mutex> lock(guard);NativeEnd(d);}

