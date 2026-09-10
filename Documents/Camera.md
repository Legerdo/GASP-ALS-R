# Camera

## Module

`GarCamera` is a separate UE module (`Source/GarCamera/`).  
Add it to your module's `PublicDependencyModuleNames` if you use it directly from C++.

---

## UGarGameplayCameraStateComponent

A `UPawnComponent` that drives UE5's Gameplay Camera system based on GAS gameplay tags.

### Key State

| Property | Type | Description |
| --- | --- | --- |
| `DesiredPerspective` | `FGameplayTag` | Replicated desired perspective (ThirdPerson / FirstPerson) |
| `Perspective` | `FGameplayTag` | Current active perspective |
| `DesiredShoulderMode` | `FGameplayTag` | Replicated shoulder side (Left / Right) |
| `ShoulderMode` | `FGameplayTag` | Current active shoulder mode |
| `FocalLength` | `float` | Current camera focal length (cm) |
| `bIsFocusPawn` | `bool` | True when the camera is focused on this pawn |

### Settings Asset

`UGarGameplayCameraStateSettings` controls per-perspective camera variable bindings  
(`UFloatCameraVariable`, `UVector3dCameraVariable`, `URotator3dCameraVariable`).

---

## Gameplay Tags (GarCameraGameplayTags)

Defined in `GarCameraGameplayTags.h`:

| Namespace | Tag Examples |
| --- | --- |
| `GarCameraPerspectiveTags` | `ThirdPerson`, `FirstPerson` |
| `GarCameraShoulderModeTags` | `Left`, `Right` |

---

## Usage

1. Add `UGarGameplayCameraStateComponent` to your pawn.
2. Assign a `UGarGameplayCameraStateSettings` data asset.
3. Call `SetDesiredPerspective()` / `SetDesiredShoulderMode()` to request camera changes.  
   Changes are replicated and validated against `PerspectiveChangeBlockTime`.

### UE 5.8 camera ownership

With `AGameplayCamerasPlayerCameraManager`, GAR disables the component's standalone
camera system during runtime initialization, before `BeginPlay`. Possession calls
the manager's `ActivateGameplayCamera`; unpossession and camera-state `EndPlay`
remove the context and its rigs before stopping the component. Repeated possession
notifications do not restart an already-active context.

Camera state reads the evaluator hosting the component's evaluation context, so
camera variables and FOV come from the manager in this mode. A regular
`APlayerCameraManager` retains the standalone fallback.

No engine patch or manually added `CineCameraComponent` is required. Blueprint
defaults remain unchanged for editor previews. GAR does not force `ViewTarget`
each tick or override the HUD's manual debug-target selection.

## Related

- [Character](Character.md)
- [Gameplay Tags](GameplayTags.md)
