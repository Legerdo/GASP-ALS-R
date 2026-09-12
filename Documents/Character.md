# Character

## Overview

`AGarCharacter` is the central actor class of this plugin. It is a `APawn` subclass that implements `IAbilitySystemInterface`, `IGameplayCueInterface`, `IGameplayTagAssetInterface`, and `IMoverInputProducerInterface`.

It owns and coordinates all major subsystems:

| Component | Class | Role |
| --- | --- | --- |
| Capsule | `UCapsuleComponent` | Primary collision capsule |
| ProneCapsule | `UCapsuleComponent` | Alternative capsule used when prone |
| Mesh | `USkeletalMeshComponent` | Main skeletal mesh |
| CharacterMover | `UGarCharacterMoverComponent` | Mover-based movement |
| MotionWarping | `UMotionWarpingComponent` | Motion warping for abilities |
| AbilitySystem | `UGarAbilitySystemComponent` | GAS ability system |
| PhysicalAnimation | `UGarPhysicalAnimationComponent` | Physical animation / ragdolling |
| OverlayModeComponent | `UGarOverlayModeComponent` | Overlay layer management |
| DeltaOverlayModeComponent | `UGarDeltaOverlayModeComponent` | Delta overlay layer management |
| OverrideModeComponent | `UGarOverrideModeComponent` | Override anim layer management |

## Stance API

| Method | Description |
| --- | --- |
| `Crouch()` | Transition to Crouching stance |
| `UnCrouch()` | Transition to Standing stance |
| `Prone()` | Transition to LyingFront (prone) stance |
| `Supine()` | Transition to LyingBack (supine) stance |
| `IsCrouching()` | Returns true when Crouching |
| `CanLie()` | Returns true when lying is permitted |

## Input Stance vs Desired Stance

`SetInputStance()` is the internal setter used by abilities and the ragdoll system.  
`SetDesiredStance()` / `ApplyDesiredStance()` is the player-intent layer that feeds `InputStance` based on desired tags and current capability checks.

## Events (C++ delegates)

| Delegate | Signature |
| --- | --- |
| `OnPossessorChanged` | `(AController*)` |
| `OnSetupPlayerInputComponent` | `(UInputComponent*)` |
| `OnTick` | `(float DeltaTime)` |
| `OnChangeGameplayTag` | `(const FGameplayTag&)` |

## Settings Asset

`UGarCharacterSettings` contains the desired-to-actual tag mapping table and other per-character tuning values.  
Assign it in the Blueprint default `Settings` property.

## Mesh Initialization Workaround: Use Ref Pose on Init Anim

Adopted on 2026-09-13 with the project's custom UE 5.8.2 build.

`/GAR/Core/B_Gar_Character` intentionally enables **Use Ref Pose on Init Anim** on
its `CharacterMesh` component (Details > Animation > advanced properties).
The C++ property is `USkeletalMeshComponent::bUseRefPoseOnInitAnim`; this is a
component setting, not a SkeletalMesh asset setting. `B_GarExtra_Character`
inherits the enabled value.

### Reason and Investigation

- Compiling `B_Gar_Character` could collapse level-placed `B_GarExtra_Character`
  meshes at the component origin. All 95 component-space bone transforms were
  Identity. Reopening the level restored the pose.
- The same immediate failure was reproduced with standard engine components
  without GAR character logic or PhysicsControl. This is not a retained workaround
  for the removed PhysicalAnimationComponent.
- Engine source inspection found that transform buffers start as Identity, while
  animation update/evaluation is skipped during Blueprint reinstancing
  (`GIsReinstancing`). The normal editor initialization path can therefore leave
  the pose unevaluated. With `Update Animation in Editor` disabled, subsequent
  bone-transform refresh normally depends on a LOD change.
- GAR's mannequin mesh has only LOD0; the Sample mesh has three LODs. In isolated
  tests, the Sample mesh stayed collapsed at fixed LOD0 but recovered after
  LOD0 -> LOD1 -> LOD0. Moving the camera cannot trigger this recovery on GAR's
  single-LOD mesh. This establishes a recovery-path difference, not a complete
  diagnosis of the original Sample actors' non-reproduction.
- Enabling **Use Ref Pose on Init Anim** initializes a valid reference pose without
  requiring the first animation evaluation. This prevented the collapse in the
  isolated test; the user then confirmed the fix on the actual GAR character and
  adopted the setting.

The setting changes the **initial pose**, not ongoing animation playback; it does
not permanently force the reference pose. It is also **not editor-only** and can
affect runtime initialization. Keep it enabled intentionally. After an engine
upgrade, retest parent-BP compilation with a placed child character and check
PIE/spawn initialization before considering removal.

## Related

- [Mover](Mover.md)
- [Overlay System](OverlaySystem.md)
- [Physical Animation & Ragdolling](PhysicalAnimationRagdolling.md)
- [Gameplay Abilities](GameplayAbilities.md)
