#include "../dllmain.h"
#include "Bridge.h"
#include "State.h"
#include "ToggleKey.h"
#include "HitGeometry.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <limits>
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
bool enabled=true,wasPlayable=false;
bool modeChanged=false,cameraOwned=false;
bool nativeActionRequested=false;
unsigned nativeActionFrames=0;
Vec nativeAtariOffset{},handoffFeet{};
unsigned handoffLogFrames=0;
bool protectionOwned=false,savedNoDeath=false,savedNoTarget=false;
cPlayer* protectedPlayer=nullptr;
uint8_t savedDamageTimer=0;
ToggleKey modeKey;
float savedCameraRatio=.8f;
cPlayer* heldPlayer=nullptr;
unsigned savedDraw=0,epoch=1,teleport=0,guestPid=0,nextId=1;
uint16_t savedCollision=0,room=0xffff;
int lastHp=0;
ULONGLONG lastLog=0;
struct PendingHit { unsigned guid; int damage; ULONGLONG expires; };
struct HitRay { Vec from{},to{}; bool start=false,end=false; };
std::map<cEm*,PendingHit> pendingHits;
std::map<unsigned,HitRay> hitRays;
using NativeDamageFn=int(__cdecl*)(cEm*,int,int,int);
NativeDamageFn nativeDamage=nullptr;
using NativeMoveFn=void(__thiscall*)(cPlayer*);
NativeMoveFn nativeMove=nullptr;
ULONGLONG resumeSince=0;
Vec resumeFeet{};
bool resumeTracking=false;
bool keyHeld[512]{},buttonHeld[6]{};
bool absoluteValid=false;
POINT absolutePrevious{};
bool legacyValid=false;
POINT legacyPrevious{};
ULONGLONG lastRawMotion=0;
unsigned keyEvents=0,mouseEvents=0;
unsigned cameraCalls=0;
unsigned nativeMoveCalls=0,nativeKeyCalls=0;
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
bool protectedMode(){return (guest.flags&(P::kMcCreative|P::kMcSpectator))!=0;}
void forwardDamage(float damage,cPlayer* p);
int __cdecl nativeDamageHook(cEm* em,int damage,int randomRange,int flags){
 std::lock_guard<std::recursive_mutex> lock(guard);
 auto g=GlobalPtr();auto player=PlayerPtr();bool sensor=owns&&enabled&&em==player&&g;
 int nativeHp=sensor?g->playerHpCur_4FB4:0;
 auto i=pendingHits.find(em);bool bridged=i!=pendingHits.end()&&em->guid_F8==i->second.guid&&GetTickCount64()<i->second.expires;
 if(bridged){damage=i->second.damage;randomRange=0;pendingHits.erase(i);}
 int before=em->hp_324,result=nativeDamage(em,damage,randomRange,flags);
 // Restore the sensor before the caller chooses a lethal native animation.
 // Minecraft decides whether the shield blocked and whether the player died.
 if(sensor&&g->playerHpCur_4FB4<nativeHp){
  if(!protectedMode())forwardDamage(float(nativeHp-g->playerHpCur_4FB4)*20.f/std::max(1,int(g->playerHpMax_4FB6)),player);
  g->playerHpCur_4FB4=int16_t(nativeHp);result=nativeHp;
 }
 if(bridged)spd::log()->info("RE4CRAFT native damage actorGuid={} part={} weapon={} hp={}->{}",em->guid_F8,em->m_DmgInfo_328.m_pDamageYarare_18?em->m_DmgInfo_328.m_pDamageYarare_18->parts_no_26:-1,em->m_DmgInfo_328.m_Wep_6,before,em->hp_324);
 return result;
}
void forwardDamage(float damage,cPlayer* p){
 // Generic damage has no source position, so Minecraft cannot test the shield
 // arc. Prefer the native attacker, then the recorded impact origin.
 Vec source=p->m_DmgInfo_328.m_PosFrom_8;
 Vec delta=source-p->pos_94;float distance=delta.x*delta.x+delta.z*delta.z;
 bool valid=std::isfinite(distance)&&distance>=1&&distance<=64000.f*64000.f&&(source.x!=0||source.y!=0||source.z!=0);
 // m_pEm is also used for contextual actions and grabs: it is not a reliable
 // attacker identifier. Use the impact origin first, never an unrelated proxy.
 unsigned fallback=0;
 if(!valid){float best=4000.f*4000.f;for(auto& [id,em]:actors)if(em&&em->IsValid()&&em->hp_324>0){
  switch(em->id_100){case 0x18:case 0x21:case 0x23:case 0x26:case 0x27:case 0x28:case 0x29:case 0x2e:continue;}
  Vec d=em->pos_94-p->pos_94;float r=d.x*d.x+d.z*d.z;if(r<best){best=r;source=em->pos_94;fallback=id;valid=true;}
 }}
 auto origin=skycraft::SkyToMc(source);float x=valid?float(origin.x):std::numeric_limits<float>::quiet_NaN(),z=valid?float(origin.z):std::numeric_limits<float>::quiet_NaN();
 int32_t xb,zb;memcpy(&xb,&x,4);memcpy(&zb,&z,4);
 spd::log()->info("RE4CRAFT incoming damage={} source={:.3f},{:.3f} valid={} fallbackActor={} look={:.1f} nativeWeapon={}",damage,x,z,valid,fallback,yaw,p->m_DmgInfo_328.m_Wep_6);
 link.PushInput(P::kInHurtDirected,P::kHurtMelee,int(damage*500),xb,zb);
}
void syncPlayer(cPlayer* p,Vec feet){
 p->pos_94=p->pos_old_110=feet;
 // cPlayer::collisionCheck (67C870) keeps the swept-ray origin 400 units
 // above the feet. A feet-only backup makes its first native sweep hit ground.
 p->pos_bak_7F8=feet+Vec{0,400,0};p->speed_104=Vec{};
 p->atari_2B4.m_Pos_30=p->atari_2B4.m_oldPos_3C=feet+nativeAtariOffset;
 p->matUpdate();p->world_old_7C=p->world_old2_88=p->world_70;
}
void restoreProtection(){
 if(!protectionOwned)return;
 if(auto g=GlobalPtr()){
  FlagSet(g->flags_DEBUG_0_60,unsigned(Flags_DEBUG::DBG_NO_DEATH2),savedNoDeath);
  FlagSet(g->flags_DEBUG_0_60,unsigned(Flags_DEBUG::DBG_EM_NO_ATK),savedNoTarget);
 }
 auto p=PlayerPtr();if(p&&p==protectedPlayer&&p->IsValid())p->m_DmgInfo_328.m_Timer_5=savedDamageTimer;
 protectionOwned=false;protectedPlayer=nullptr;
}
void updateProtection(){
 auto g=GlobalPtr();auto p=PlayerPtr();
 if((!owns&&!nativeActionFrames)||!enabled||!protectedMode()||!g||!p||!p->IsValid()){restoreProtection();return;}
 if(!protectionOwned){
  savedNoDeath=FlagIsSet(g->flags_DEBUG_0_60,unsigned(Flags_DEBUG::DBG_NO_DEATH2));
  savedNoTarget=FlagIsSet(g->flags_DEBUG_0_60,unsigned(Flags_DEBUG::DBG_EM_NO_ATK));
  protectedPlayer=p;savedDamageTimer=p->m_DmgInfo_328.m_Timer_5;protectionOwned=true;
 }
 FlagSet(g->flags_DEBUG_0_60,unsigned(Flags_DEBUG::DBG_NO_DEATH2),true);
 FlagSet(g->flags_DEBUG_0_60,unsigned(Flags_DEBUG::DBG_EM_NO_ATK),true);
 p->m_DmgInfo_328.m_Timer_5=128;
}
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
void release(bool prepareNative=false){
 restoreProtection();
 if(owns)link.PushInput(P::kInReleaseAll,0);owns=false;
 memset(keyHeld,0,sizeof(keyHeld));memset(buttonHeld,0,sizeof(buttonHeld));absoluteValid=legacyValid=false;
 auto p=PlayerPtr();
 if(heldPlayer&&p==heldPlayer&&p->IsValid()){
  Vec feet=p->pos_94;
  // Native movement now advances every frame. Keep its collision backup at
  // the current Minecraft feet before returning full control to RE4.
  if(prepareNative&&GlobalPtr()&&GlobalPtr()->Rno0_20==3&&!cutscene()){
   syncPlayer(p,feet);
   handoffFeet=feet;handoffLogFrames=3;
  }
  p->be_flag_4=(p->be_flag_4&~(UNIT_BE_FLAG_TRANS|UNIT_BE_FLAG_DRAWFOOTSHADOW))|savedDraw;
  const uint16_t mask=SAT_SCA_ENABLE|SAT_OBA_ENABLE;
  p->atari_2B4.m_flag_1A=(p->atari_2B4.m_flag_1A&~mask)|(savedCollision&mask);
 }
 if(cameraOwned){Game_SetCameraSmoothness(savedCameraRatio/.80000001f);cameraOwned=false;}
 heldPlayer=nullptr;
}
void __fastcall nativeMoveHook(cPlayer* p,void*){
 Vec feet{};bool anchor=false;
 {std::lock_guard<std::recursive_mutex> lock(guard);++nativeMoveCalls;anchor=owns&&p==heldPlayer&&!cutscene();if(anchor)feet=p->pos_94;}
 // Advance native hurt, action and animation state. SPF_PL also stopped these
 // and could leave story events waiting forever on a frozen player routine.
 nativeMove(p);
 {std::lock_guard<std::recursive_mutex> lock(guard);
  if(anchor&&owns&&p==heldPlayer){
   if(p!=PlayerPtr()||!GlobalPtr()||GlobalPtr()->curRoomId_4FAC!=room||!playable()){release();wasPlayable=false;resumeTracking=false;}
   else syncPlayer(p,feet);
  }
 }
}
void __cdecl nativeKeysHook(){
 auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 reinterpret_cast<void(__cdecl*)()>(base+0x566610)();
 std::lock_guard<std::recursive_mutex> lock(guard);
 ++nativeKeyCalls;
 if(!enabled||cutscene()||(!owns&&!resumeTracking&&!wasPlayable))return;
 BridgeFilterNativeButtons();
 if(nativeActionFrames){BridgeInjectActionButtons();--nativeActionFrames;spd::log()->info("RE4CRAFT G injected after native key update on={:x} trigger={:x}",Key_btn_on(),Key_btn_trg());}
}
void hookNativeSimulation(){
 auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
 auto jumpMatches=[&](unsigned thunk,unsigned target){auto a=base+thunk;return *reinterpret_cast<uint8_t*>(a)==0xe9&&a+5+*reinterpret_cast<int32_t*>(a+1)==base+target;};
 if(!jumpMatches(0x71c1,0x1b5af0)||!jumpMatches(0xe1f1,0x354950)||!jumpMatches(0x2d1a,0x364d20)||!jumpMatches(0x3224,0x1aeef0)){
  spd::log()->error("RE4CRAFT native simulation signature mismatch");return;
 }
 auto keyCall=base+0x56a86b;
 if(*reinterpret_cast<uint8_t*>(keyCall)!=0xe8||keyCall+5+*reinterpret_cast<int32_t*>(keyCall+1)!=base+0x16db){spd::log()->error("RE4CRAFT key refresh signature mismatch");return;}
 nativeDamage=reinterpret_cast<NativeDamageFn>(base+0x1b5af0);InjectHook(base+0x71c1,nativeDamageHook,HookType::Jump);
 nativeMove=reinterpret_cast<NativeMoveFn>(base+0x364d20);InjectHook(base+0xe1f1,nativeMoveHook,HookType::Jump);
 InjectHook(keyCall,nativeKeysHook,HookType::Call);
 spd::log()->info("RE4CRAFT native simulation and damage hooks installed");
}
YARARE_INFO* hitZone(cEm* em,Vec from,Vec to,int weapon,bool& approximate){
 auto point=[](Vec v){return HitPoint{v.x,v.y,v.z};};
 auto transform=[](const Mtx& m,Vec v,bool position){return Vec{
  m[0][0]*v.x+m[0][1]*v.y+m[0][2]*v.z+(position?m[0][3]:0),
  m[1][0]*v.x+m[1][1]*v.y+m[1][2]*v.z+(position?m[1][3]:0),
  m[2][0]*v.x+m[2][1]*v.y+m[2][2]*v.z+(position?m[2][3]:0)};};
 using GetPartFn=cCoord*(__thiscall*)(cModel*,int);
 auto getPart=reinterpret_cast<GetPartFn>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+0x25b7c0);
 auto ignoresZone=reinterpret_cast<int(__cdecl*)(int)>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+0x1b9a30);
 YARARE_INFO *result=nullptr,*body=nullptr;
 double best=INFINITY,bodyDistance=INFINITY;unsigned count=0;
 for(auto z=&em->yarare_344;z&&count++<256;z=z->nextInfo_30){
  if(!(z->flag_24&1)||z->parts_no_26<0||z->parts_no_26>em->nParts_102)continue;
  if((z->flag_24&0x10)&&ignoresZone(weapon))continue;
  auto bone=z->parts_no_26?getPart(em,z->parts_no_26-1):em;if(!bone)continue;
  Vec end=z->offset_0;if(z->flag_24&2)end.x+=z->height_1C;else if(z->flag_24&4)end.z+=z->height_1C;else end.y+=z->height_1C;
  auto a=point(transform(bone->mat_C,z->offset_0,true)),b=point(transform(bone->mat_C,end,true));
  auto r=point(transform(bone->mat_C,Vec{0,0,z->radius_18},false));double radius=std::sqrt(hitDot(r,r));
  double t=segmentCapsule(point(from),point(to),a,b,radius);
  if(t<best){best=t;result=z;}
  // A Minecraft proxy is slightly wider than the animated mesh. Keep those
  // valid Minecraft hits as body damage, without manufacturing a headshot.
  double distance=pointSegmentDistanceSq(point(to),a,b)-radius*radius;
  if(z->parts_no_26!=5&&distance<bodyDistance){bodyDistance=distance;body=z;}
 }
 approximate=!result;
 if(!result)result=body;
 if(result){auto d=to-from;result->len_28=d.x*d.x+d.y*d.y+d.z*d.z;}
 return result;
}
void modeInput(bool down){if(modeKey.update(down)){enabled=!enabled;modeChanged=true;spd::log()->info("RE4CRAFT mode={} (F10)",enabled?"Minecraft":"Resident Evil 4");}}
void publishActors(){
 std::vector<P::ActorRecord> records;std::map<unsigned,cEm*> fresh;
 auto mgr=EmMgrPtr();auto p=PlayerPtr();
 if(p&&mgr&&mgr->m_nArray_8<4096)for(auto& em:*mgr){
  if(!em.IsValid()||!IsEnemy(em.id_100)||em.hp_324<=0||em.r_no_0_FC==unsigned(cEm::Routine0::Die))continue;
  auto delta=em.pos_94-p->pos_94;if(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>64000.f*64000.f)continue;
  auto it=ids.find(em.guid_F8);if(it==ids.end())it=ids.emplace(em.guid_F8,nextId++).first;
  P::ActorRecord a{};a.formId=it->second;
  // Wildlife and the separate merchant are still hittable, but not golem targets.
  switch(em.id_100){case 0x18:case 0x21:case 0x23:case 0x26:case 0x27:case 0x28:case 0x29:case 0x2e:break;default:a.flags=P::kActorHostile|P::kActorInCombat;}
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
  if(e.type==P::kEvHitRayStart||e.type==P::kEvHitRayEnd){
   if(std::isfinite(e.a)&&std::isfinite(e.b)&&std::isfinite(e.c)&&std::abs(e.a)<1e6&&std::abs(e.b)<1e6&&std::abs(e.c)<1e6){auto& r=hitRays[e.formId];Vec point=skycraft::McToSky(e.a,e.b,e.c);if(e.type==P::kEvHitRayStart){r=HitRay{};r.from=point;r.start=true;}else{r.to=point;r.end=true;}}
  }else if(e.type==P::kEvHitActor&&std::isfinite(e.a)&&e.a>0&&e.a<10000){
   auto it=actors.find(e.formId);if(it==actors.end()||!it->second->IsValid())continue;auto em=it->second;
   if(em->hp_324<=0||em->r_no_0_FC==unsigned(cEm::Routine0::Die)||!nativeDamage)continue;
   int damage=std::min(30000,int(std::ceil(e.a*100))),weapon=(e.flags&P::kHitProjectile)?2:0x10;
   auto r=hitRays[e.formId];hitRays.erase(e.formId);
   if(!r.start||!r.end){float radius,height;actorShape(*em,radius,height);r.from=em->pos_94+Vec{2000,height*500,0};r.to=em->pos_94+Vec{-2000,height*500,0};}
   bool approximate=false;auto zone=hitZone(em,r.from,r.to,weapon,approximate);
   if(!zone){spd::log()->warn("RE4CRAFT actor={} has no active native damage shape",e.formId);continue;}
   auto& pending=pendingHits[em];if(pending.guid!=em->guid_F8||GetTickCount64()>=pending.expires)pending={em->guid_F8,0,0};
   pending.damage=std::min(30000,pending.damage+damage);pending.expires=GetTickCount64()+1000;
   using SetHitFn=void(__thiscall*)(cDmgInfo*,int,int,int,const Vec*,float,YARARE_INFO*);
   auto setHit=reinterpret_cast<SetHitFn>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+0x1aeef0);
   setHit(&em->m_DmgInfo_328,0,10,weapon,&r.from,zone->len_28,zone);
   spd::log()->info("RE4CRAFT queued hit actor={} weapon={} flags={} MCdamage={} part={} bodyFallback={} hp={}",e.formId,weapon,e.flags,e.a,zone->parts_no_26,approximate,em->hp_324);
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
 if(!screen&&vk=='G'){if(down&&!(lp&(1L<<30)))nativeActionRequested=true;return true;}
 if(!screen&&vk==VK_ESCAPE)return false;
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
void BridgeInit(){if(link.Create())spd::log()->info("RE4CRAFT 0.3.3 host adapter; SkyCraft protocol {} mapping Local\\RE4Craft_v1",P::kVersion);hookNativeSimulation();RenderInit();}
void BridgeBeforeTick(){
 std::lock_guard<std::recursive_mutex> lock(guard);
 if(modeChanged){release(!enabled);wasPlayable=false;nativeActionRequested=false;nativeActionFrames=0;modeChanged=false;}
 if(owns&&GetForegroundWindow()!=hWindow){link.PushInput(P::kInReleaseAll,0);memset(keyHeld,0,sizeof(keyHeld));memset(buttonHeld,0,sizeof(buttonHeld));absoluteValid=false;}
 if(nativeActionRequested){
  nativeActionRequested=false;
  if(owns&&playable()&&!(guest.flags&P::kMcSpectator)){
   nativeActionFrames=1;
   spd::log()->info("RE4CRAFT native action requested (G)");
  }
 }
 updateProtection();
 SyncNativeLights(); // native light allocation/update stays on the game thread
 beforeActorMove.clear();auto mgr=EmMgrPtr();
 if(owns&&mgr&&mgr->m_nArray_8<4096)for(auto& em:*mgr)if(em.IsValid()&&IsEnemy(em.id_100)&&em.hp_324>0)beforeActorMove[em.guid_F8]=em.pos_94;
}
void BridgeTick(){
 std::lock_guard<std::recursive_mutex> lock(guard);if(!link.Valid())return;link.Heartbeat();
 auto g=GlobalPtr();auto p=PlayerPtr();
 // Forward even a lethal native hit before playable() tests RE4 health. The
 // guest decides shield/armor/death; native health is only a damage sensor.
 if(owns&&enabled&&g&&p&&p->IsValid()&&lastHp>g->playerHpCur_4FB4){float damage=float(lastHp-g->playerHpCur_4FB4)*20.f/std::max(1,int(g->playerHpMax_4FB6));if(!protectedMode())forwardDamage(damage,p);g->playerHpCur_4FB4=int16_t(lastHp);}
 bool running=enabled&&playable(),roomChanged=g&&room!=g->curRoomId_4FAC;
 if(handoffLogFrames&&p&&p->IsValid()){Vec d=p->pos_94-handoffFeet;spd::log()->info("RE4CRAFT native handoff frame={} delta={:.1f},{:.1f},{:.1f} routine={},{},{},{}",4-handoffLogFrames,d.x,d.y,d.z,p->r_no_0_FC,p->r_no_1_FD,p->r_no_2_FE,p->r_no_3_FF);--handoffLogFrames;}
 if(roomChanged){release();room=g->curRoomId_4FAC;++epoch;++teleport;ids.clear();actors.clear();pendingHits.clear();hitRays.clear();resumeTracking=false;wasPlayable=false;RenderRoomReset();TerrainReset(epoch);}
 if(running&&!wasPlayable){
  Vec delta=p->pos_94-resumeFeet;
  if(!resumeTracking||std::abs(delta.x)+std::abs(delta.y)+std::abs(delta.z)>40){resumeTracking=true;resumeSince=GetTickCount64();resumeFeet=p->pos_94;}
  if(GetTickCount64()-resumeSince<250){running=false;blockReason="native-position-settling";}
 }else if(!running)resumeTracking=false;
 if(running&&!wasPlayable){auto look=g->Camera_74.CamPoint_A4.Target_C-g->Camera_74.CamPoint_A4.Campos_0;
  yaw=std::atan2(-look.x,look.z)*57.2957795f;pitch=-std::atan2(look.y,std::hypot(look.x,look.z))*57.2957795f;++teleport;lastHp=g->playerHpCur_4FB4;resumeTracking=false;
  spd::log()->info("RE4CRAFT resume room={:x} teleport={} feet={:.1f},{:.1f},{:.1f} routine={},{},{},{}",room,teleport,p->pos_94.x,p->pos_94.y,p->pos_94.z,p->r_no_0_FC,p->r_no_1_FD,p->r_no_2_FE,p->r_no_3_FF);
 }
 bool entering=running&&!wasPlayable;wasPlayable=running;bool alive=link.McAlive();P::McState fresh{};
 if(alive&&link.ReadMcState(fresh))guest=fresh;
 if(alive&&guestPid!=link.McPid()){guestPid=link.McPid();link.ResetOverlay();TerrainReset(epoch);++teleport;spd::log()->info("RE4CRAFT Minecraft connected pid={}",guestPid);}
 bool ready=running&&alive&&(guest.flags&P::kMcInWorld)&&guest.teleportAck==teleport&&!(guest.flags&P::kMcDead);
 if(!ready)release();else{
  if(!owns){heldPlayer=p;nativeAtariOffset=p->atari_2B4.m_Pos_30-p->pos_94;savedDraw=p->be_flag_4&(UNIT_BE_FLAG_TRANS|UNIT_BE_FLAG_DRAWFOOTSHADOW);savedCollision=p->atari_2B4.m_flag_1A;savedCameraRatio=Game_GetCameraSmoothness();cameraOwned=true;owns=true;spd::log()->info("RE4CRAFT Minecraft owns movement, input, inventory and weapons");}
  if(std::isfinite(guest.x)&&std::isfinite(guest.y)&&std::isfinite(guest.z)&&std::abs(guest.x)<1e6&&std::abs(guest.y)<1e6&&std::abs(guest.z)<1e6){
   Vec feet=skycraft::McToSky(guest.x,guest.y,guest.z);p->ang_A0.y=-yaw*.0174532925f;
   // Restoring native collision on F10 must use the current Minecraft location,
   // not the capsule left at the last native movement tick.
   p->be_flag_4&=~(UNIT_BE_FLAG_TRANS|UNIT_BE_FLAG_DRAWFOOTSHADOW);p->atari_2B4.m_flag_1A&=~(SAT_SCA_ENABLE|SAT_OBA_ENABLE);syncPlayer(p,feet);
  }
  updateProtection();
  lastHp=g->playerHpCur_4FB4;collideActors();combat();
 }
 if(!owns){P::McEvent discarded{};unsigned count=0;while(count++<P::kEventRingEntries&&link.PopEvent(discarded)){};}
 if(running){TerrainUpdate(p->pos_94,epoch,entering||roomChanged);publishActors();}else link.WriteActors(nullptr,0);
 host.flags=running?P::kSkyInGame:P::kSkyMenuOpen;host.worldId=room;host.collisionEpoch=epoch;host.teleportSeq=teleport;
 if(p&&p->IsValid()){auto m=skycraft::SkyToMc(p->pos_94);host.posX=m.x;host.posY=m.y;host.posZ=m.z;}
 host.yaw=yaw;host.pitch=pitch;host.viewportW=viewportW;host.viewportH=viewportH;host.gameHour=12;link.WriteSkyState(host);
 if(GetTickCount64()-lastLog>5000){lastLog=GetTickCount64();spd::log()->info("RE4CRAFT state running={} reason={} owns={} guest={} flags={:x} ack={}/{} actors={} keys={} mouse={} cameras={} blockedMoves={} nativeMoves={} nativeKeys={} look={:.1f},{:.1f}",running,blockReason,owns,alive,guest.flags,guest.teleportAck,teleport,actors.size(),keyEvents,mouseEvents,cameraCalls,blockedMoves,nativeMoveCalls,nativeKeyCalls,yaw,pitch);}
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
   if(!screen&&k.VKey=='G')return true; // native action uses the legacy key stream once
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
void BridgeNativeDepth(IDirect3DDevice9* d,IDirect3DSurface9* next){std::lock_guard<std::recursive_mutex> lock(guard);NativeDepth(d,next);}
void BridgeNativeClear(IDirect3DDevice9* d,unsigned flags){std::lock_guard<std::recursive_mutex> lock(guard);NativeClear(d,flags);}
void BridgeNativeBegin(){std::lock_guard<std::recursive_mutex> lock(guard);NativeBegin();}
void BridgeNativeEnd(IDirect3DDevice9* d){std::lock_guard<std::recursive_mutex> lock(guard);NativeEnd(d);}

