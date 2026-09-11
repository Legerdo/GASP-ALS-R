# Mover

## Overview

`UGarCharacterMoverComponent` extends UE5's `UMoverComponent` and manages all movement simulation for `AGarCharacter`.  
Movement logic is split into **Mover Modes** (one active at a time) and **Mover Modifiers** (zero or more, stacked on top).

## Replicated State

| Property | Type | Description |
| --- | --- | --- |
| `RotationMode` | `FGameplayTag` | Current rotation mode tag |
| `Stance` | `FGameplayTag` | Current stance tag |
| `Gait` | `FGameplayTag` | Current gait tag |
| `bFacingUpward` | `bool` | Whether the ragdoll is facing upward (replicated) |

## Settings

| Property | Description |
| --- | --- |
| `LocomotionModeTags` | Tags that map to locomotion modes |
| `bTeleportPhysicsOnProxy` | When true, physics teleport is applied on simulated proxies |

---

## Mover Modes

Each mode derives from `FLayeredMoveBase` / `UMoverDataSourceBase` and handles a specific locomotion state.

| Class | Mode |
| --- | --- |
| `GarMoverWalkingMode` | Standard walking / running on ground |
| `GarMoverFallingMode` | Airborne (falling / jumping) |
| `GarMoverSlidingMode` | Sliding on the ground |
| `GarMoverTraversalMode` | Traversal action (climb/vault/etc.) |
| `GarMoverRagdollingMode` | Ragdoll physics-driven movement |

---

## Mover Modifiers

The legacy `GAR_USE_MOVEMENTMODIFIER` switch remains disabled. Stance geometry is
updated separately by `UGarCharacterMoverComponent::OnPreSimulate`:

- The recorded stance input drives interpolation using the Mover time step, not Character Tick.
- `FGarMoverCapsuleResizeEffect` applies the main/prone capsule geometry, weld/collision state,
  foot-preserving pivot adjustment, mesh offset, and eye height together without actor teleportation.
- `FGarMoverStanceState` persists intermediate dimensions and eye height across movement modes.
  It is restored before simulation and applied to interpolated proxies on finalization, without
  adding the pivot adjustment a second time.
- Finalization applies stance geometry/visuals without invalidating simulation caches. Restoring
  geometry before simulation invalidates only the floor query; the resize effect invalidates the floor
  and movement-base cache together with the output sync state's base, including collision-only changes.
  This keeps the later based movement tick from running with missing base information.
- Ragdoll input and the recorded `BlockUpdateCapsuleSize` flag suspend capsule changes;
  eye height continues to follow stance. Existing Character stance settings remain the authoring interface.
- This implementation targets game-thread Mover. ChaosMover's async effect API is not implemented.

`GAR.Mover.Stance` automation tests cover state/effect serialization and stationary stance
transitions with Character Tick disabled at 60/15 FPS on static and Movable floors, finalization cache
preservation, floor following, and collision-only effects. Full gameplay/network validation is still required.

Modifiers adjust movement parameters without replacing the active mode.  
Base class: `FGarMoverModifier`.

| Class | Effect |
| --- | --- |
| `FGarMoverGaitModifier` | Adjusts speed caps for the current gait |
| `FGarMoverRotationModifier` | Overrides rotation behaviour |
| `FGarMoverCrouchingModifier` | Applies crouching movement constraints |
| `FGarMoverLyingModifier` | Applies lying/prone movement constraints |
| `FGarMoverStanceModifier` | Base for stance-based modifiers |

---

## Constants

```cpp
static constexpr float MIN_FLOOR_DIST = 1.9f;  // cm above walkable floor
static constexpr float MAX_FLOOR_DIST = 2.4f;  // cm above walkable floor
```

## Related

- [Character](Character.md)
- [Gameplay Abilities](GameplayAbilities.md)
- [Physical Animation & Ragdolling](PhysicalAnimationRagdolling.md)
