// Copyright (c) 2026 RE4Craft contributors. MIT; see licenses/SkyCraft-MIT.txt.
package dev.skycraft.mixin;

import dev.skycraft.world.SkyCollision;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.pathfinder.PathType;
import net.minecraft.world.level.pathfinder.WalkNodeEvaluator;
import net.minecraft.world.phys.shapes.VoxelShape;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Native surfaces participate in vanilla mob pathfinding as well as movement. */
@Mixin(WalkNodeEvaluator.class)
public abstract class WalkNodeEvaluatorMixin {
 @Inject(method = "getPathTypeFromState", at = @At("RETURN"), cancellable = true)
 private static void re4craft$nativePathType(BlockGetter level, BlockPos pos, CallbackInfoReturnable<PathType> result) {
  PathType original = result.getReturnValue();
  if (original != PathType.OPEN && original != PathType.WALKABLE) return;
  VoxelShape nativeShape = SkyCollision.shapeAt(pos);
  if (nativeShape != null && !nativeShape.isEmpty()) result.setReturnValue(PathType.BLOCKED);
 }

 @Inject(method = "getFloorLevel(Lnet/minecraft/world/level/BlockGetter;Lnet/minecraft/core/BlockPos;)D", at = @At("RETURN"), cancellable = true)
 private static void re4craft$nativeFloor(BlockGetter level, BlockPos pos, CallbackInfoReturnable<Double> result) {
  BlockPos below = pos.below();
  VoxelShape nativeShape = SkyCollision.shapeAt(below);
  if (nativeShape != null && !nativeShape.isEmpty())
   result.setReturnValue(Math.max(result.getReturnValue(), below.getY() + nativeShape.max(Direction.Axis.Y)));
 }
}
