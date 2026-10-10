package dev.re4craft.test;

import dev.skycraft.SkyCraft;
import dev.skycraft.client.SkyClient;
import dev.skycraft.combat.SkyCombat;
import dev.skycraft.combat.SkyrimActorEntity;
import dev.skycraft.link.SkyLink;
import dev.skycraft.world.SkyCollision;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import net.fabricmc.api.ModInitializer;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.loader.api.FabricLoader;
import net.minecraft.core.BlockPos;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityTypes;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.ai.goal.GoalSelector;
import net.minecraft.world.entity.animal.golem.IronGolem;
import net.minecraft.world.entity.monster.zombie.Zombie;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.pathfinder.WalkNodeEvaluator;
import net.minecraft.world.level.pathfinder.PathType;
import net.minecraft.world.phys.Vec3;

/** Development-only Fabric mod. Never included in the installer or guest JAR. */
public class GuestIntegration implements ModInitializer {
 private int readyTicks;
 private boolean done;
 private final List<String> results = new ArrayList<>();
 private void check(boolean ok,String name){results.add((ok?"PASS ":"FAIL ")+name);}
 @Override public void onInitialize(){ServerTickEvents.END_SERVER_TICK.register(server->{
  if(done||!SkyLink.active()||!SkyClient.sky().inGame()||server.getPlayerList().getPlayers().isEmpty())return;
  var player=server.getPlayerList().getPlayers().getFirst();
  var pos=player.blockPosition();
  if(!SkyCollision.isKnown(pos.getX(),pos.getY()-1,pos.getZ())||++readyTicks<100)return;
  done=true;var level=player.level();var p=player.position();
  SkyrimActorEntity proxy=null;IronGolem golem=null;Zombie shield=null;
  try{
   var pathType=WalkNodeEvaluator.class.getDeclaredMethod("getPathTypeFromState",net.minecraft.world.level.BlockGetter.class,BlockPos.class);
   pathType.setAccessible(true);
   BlockPos nativeFloor=null;
   for(int dy=-3;dy<=0&&nativeFloor==null;dy++){
    var cell=pos.offset(0,dy,0);var shape=SkyCollision.shapeAt(cell);
    if(shape!=null&&!shape.isEmpty()&&level.getBlockState(cell).isAir())nativeFloor=cell;
   }
   check(nativeFloor!=null,"native floor available beneath player");
   if(nativeFloor!=null)check(pathType.invoke(null,level,nativeFloor)==PathType.BLOCKED,"pathfinder sees RE4 floor in Minecraft air");
   proxy=new SkyrimActorEntity(SkyCombat.SKYRIM_ACTOR,level);proxy.setFormId(0x7ffffffe);
   proxy.snapTo(p.x+2,p.y,p.z,0,0);level.addFreshEntity(proxy);
   golem=new IronGolem(EntityTypes.IRON_GOLEM,level);golem.snapTo(p.x,p.y,p.z,0,0);golem.setOnGround(true);
   var selector=Mob.class.getDeclaredField("targetSelector");selector.setAccessible(true);
   for(int tick=0;tick<100;tick++)((GoalSelector)selector.get(golem)).tick();
   check(golem.getTarget() instanceof SkyrimActorEntity,"vanilla golem detects RE4 proxy");
   golem.doHurtTarget(level,proxy);var hit=proxy.takeHit();
   check(hit!=null&&hit[0]>0,"golem damage enters bridge combat");
   var path=golem.getNavigation().createPath(BlockPos.containing(p.x+3,p.y,p.z),0);
   check(path!=null&&path.getNodeCount()>1,"mob path traverses RE4 floor");
   shield=new Zombie(EntityTypes.ZOMBIE,level);shield.snapTo(p.x,p.y,p.z,0,0);
   shield.setNoAi(true);shield.setNoGravity(true);shield.noPhysics=true;
   shield.setItemSlot(EquipmentSlot.OFFHAND,new ItemStack(Items.SHIELD));shield.startUsingItem(InteractionHand.OFF_HAND);
   for(int tick=0;tick<8;tick++)shield.tick();
   check(shield.isBlocking(),"shield raised after vanilla use delay");
   var holder=level.damageSources().mobAttack(proxy).typeHolder();
   float health=shield.getHealth();
   var front=new net.minecraft.world.damagesource.DamageSource(holder,new Vec3(shield.getX(),shield.getY()+1,shield.getZ()+3));
   shield.hurtServer(level,front,4);
   check(shield.getHealth()==health,"directional native melee blocked from front");
   shield.setInvulnerableTime(0);
   var rear=new net.minecraft.world.damagesource.DamageSource(holder,new Vec3(shield.getX(),shield.getY()+1,shield.getZ()-3));
   shield.hurtServer(level,rear,4);
   check(shield.getHealth()<health,"shield allows damage from behind");
  }catch(Throwable ex){results.add("ERROR "+ex);SkyCraft.LOG.error("RE4Craft integration test",ex);}
  finally{if(proxy!=null)proxy.discard();if(golem!=null)golem.discard();if(shield!=null)shield.discard();}
  try{Files.write(FabricLoader.getInstance().getGameDir().resolve("re4craft-integration-results.txt"),results);}catch(Exception ex){SkyCraft.LOG.error("RE4Craft test report",ex);}
  for(String result:results)SkyCraft.LOG.info("RE4CRAFT_TEST {}",result);
 });}
}
