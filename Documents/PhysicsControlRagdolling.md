# Physics Control & Ragdolling

## Overview

`UGarPhysicsControlComponent` is GAR's sole physical-animation and ragdoll component. It extends
Epic's `UPhysicsControlComponent`; `UGarPhysicalAnimationComponent` is not part of GAR anymore.

The component instantiates Controls and Body Modifiers from its `PhysicsControlAsset`. It then
selects named Control Profiles from gameplay tags and manages the ragdoll lifecycle.

### Required Project Settings

- `Project Settings > Physics > Enable Physics Prediction` — ON for multiplayer.
- `Project Settings > Physics > Substepping` — ON.
- `Project Settings > Physics > Substepping Async` — optional.

## Configuration

GAR now contains `PCA_Gar_Character`, copied from the sample's `PCA_SandboxCharacter` and
retargeted to GAR's `PA_UEFN_Mannequin`. `B_Gar_Character` assigns it to the inherited
`PhysicsControlAsset` property. The asset defines the `All` Control/Body Modifier set.

| Property | Purpose |
| --- | --- |
| `DefaultControlProfileName` | Profile active if no gameplay-tag profile matches. |
| `ControlProfileByTag` | Gameplay tag to Control Profile mapping. More-specific tags win. |
| `CurveSetMappings` | Animation curve to Control Set and Body Modifier Set mapping. A curve value of one suppresses the control strength. |
| `DefaultRagdollTag` / `DefaultRagdollSettings` | GAR's built-in `Gar.LocomotionAction.Unconsious` full-ragdoll setup. |
| `RagdollSettingsByTag` | Optional gameplay-tag-specific full-ragdoll overrides. |
| `TopBoneName` | Body tracked by the GAR Ragdoll Mover, normally `pelvis`. |

Control Profiles replace the former Physical Animation Profile/Constraint Profile/Chooser
combination. Author the strength, damping, collision, body movement type, and named sets in the
Physics Control Asset, not in a Skeletal Mesh Physics Asset profile.

## Ragdoll lifecycle

`UGarGameplayAbility_Ragdolling` calls `StartRagdoll()` and `StopRagdoll()` using its asset tag.
While active, GAR:

1. invokes the configured ragdoll Control Profile;
2. sets all Body Modifiers to `Simulated` and enables physics collision;
3. disables Controls by default for a full ragdoll, or preserves them when configured;
4. drives the Mover capsule from the `TopBoneName` physics body;
5. freezes settled bodies by changing their Body Modifiers to `Kinematic` and storing a pose snapshot;
6. restores capsule collision and queues the get-up orientation correction when ending.

`UGarMoverRagdollingMode`, the ragdoll ability/task, and the linked animation layer depend only
on `UGarPhysicsControlComponent`'s public ragdoll state. They do not access Physics Control
records directly.

### Mesh component frame versus physics bodies

`AGarCharacter` creates a `UGarSkeletalMeshComponent` under the existing `CharacterMesh`
subobject name; Blueprint-facing mesh access remains `USkeletalMeshComponent`.
When `PhysicsTransformUpdateMode` is `ComponentTransformIsKinematic`, Mover owns the component
frame even while the root body is simulated. In that case, ordinary component moves use
`MOVECOMP_SkipPhysicsMove`, including Mover's finalization and visual smoothing updates.
They do not teleport the ragdoll bodies or change their velocities. Explicit physics teleports
and normal updates while the root body is kinematic retain the engine's behavior.

The Physics Control runtime tests reject simulated-mesh movement warnings and verify that
visual-frame changes and finalization preserve simulated body transforms and velocities.

## Migration from legacy GAR content

`Scripts/MigrateGarPhysicsControl.py` is idempotent and has already been applied to the shipped
GAR content through Unreal Editor. It creates `PCA_Gar_Character`, assigns it to
`B_Gar_Character`, and rebinds the GettingUp predicate node to
`UGarPhysicsControlComponent`. It also migrates `B_GarExtra_Projectile`'s
legacy `IsBoneUnderSimulation` call to `IsBoneSimulatingPhysics`, which
queries the current Chaos body state through the new component, and changes
`W_GarExtra_Hud`'s `PADebugDisplayName` call to `PhysicsControlDebugDisplayName`.

`UGarPhysicalAnimationComponent` no longer exists. `AGarCharacter::PhysicalAnimation` remains
only as a deprecated Blueprint migration alias to the same `PhysicsControl` object, preserving
existing graph pins; all new C++ and Blueprint work should use `PhysicsControl`. Legacy
`UGarRagdollingSettings` and Physical Animation chooser assets do not configure the new system.
