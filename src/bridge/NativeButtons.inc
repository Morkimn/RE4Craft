// Included in Game.cpp next to the private native key pointers.
// The real key update replaces these values each frame; do not restore stale
// triggers after the scheduler, before cPlayer has had a chance to read them.
void BridgeFilterNativeButtons(){
 if(!ptrKey_btn_on||!ptrKey_btn_trg)return;
 constexpr uint64_t menus=uint64_t(KEY_BTN::KEY_OPTION)|uint64_t(KEY_BTN::KEY_CANCEL)|uint64_t(KEY_BTN::KEY_EV_CANCEL);
 *reinterpret_cast<uint64_t*>(ptrKey_btn_on)&=menus;
 *reinterpret_cast<uint64_t*>(ptrKey_btn_trg)&=menus;
 if(auto joy=JoyPtr()){
  joy->on_14=JOY_BTN(joy->on_14&(JOY_START|JOY_BACK));
  joy->trg_18=JOY_BTN(joy->trg_18&(JOY_START|JOY_BACK));
  joy->leftStick_X_0=joy->leftStick_Y_4=0;
  joy->rightStick_X_8=joy->rightStick_Y_9=0;
 }
}
void BridgeInjectActionButtons(){
 if(!ptrKey_btn_on||!ptrKey_btn_trg)return;
 constexpr uint64_t action=uint64_t(KEY_BTN::KEY_A)|uint64_t(KEY_BTN::KEY_CK)|uint64_t(KEY_BTN::KEY_ACT_A)|uint64_t(KEY_BTN::KEY_PICK_UP)|uint64_t(KEY_BTN::KEY_OK);
 *reinterpret_cast<uint64_t*>(ptrKey_btn_on)|=action;
 *reinterpret_cast<uint64_t*>(ptrKey_btn_trg)|=action;
 if(auto joy=JoyPtr()){joy->on_14=JOY_BTN(joy->on_14|JOY_A);joy->trg_18=JOY_BTN(joy->trg_18|JOY_A);}
}
