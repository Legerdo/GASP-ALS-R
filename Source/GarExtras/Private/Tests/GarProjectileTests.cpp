// Copyright (c) SAM-tak. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GarProjectile.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarProjectileOwnerIgnoreTest, "GAR.Projectile.OwnerIgnore.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarProjectileOwnerIgnoreTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	// Actor initialization is required for ProcessEvent / RepNotify dispatch.
	World->InitializeActorsForPlay(FURL());

	AActor* FirstOwner = World->SpawnActor<AActor>();
	AActor* SecondOwner = World->SpawnActor<AActor>();
	AActor* OtherIgnoredActor = World->SpawnActor<AActor>();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Projectile = World->SpawnActor<AGarProjectile>(SpawnParameters);
	if (!TestNotNull(TEXT("Projectile"), Projectile)) return false;
	auto* Collision = Projectile->FindComponentByClass<UCapsuleComponent>();
	if (!TestNotNull(TEXT("Projectile collision"), Collision)) return false;
	Collision->IgnoreActorWhenMoving(OtherIgnoredActor, true);

	// Match the shipped ability: FireInDirection, followed by SetOwner.
	Projectile->FireInDirection(FVector::ForwardVector);
	Projectile->SetOwner(FirstOwner);
	TestTrue(TEXT("Late owner is ignored before any movement tick"), Collision->GetMoveIgnoreActors().Contains(FirstOwner));
	Projectile->SetOwner(FirstOwner);
	TestEqual(TEXT("Repeated SetOwner does not duplicate entries"), Collision->GetMoveIgnoreActors().Num(), 2);
	struct { AActor* NewOwner; } SetOwnerParameters{SecondOwner};
	Projectile->ProcessEvent(Projectile->FindFunctionChecked(TEXT("SetOwner")), &SetOwnerParameters);
	TestFalse(TEXT("Old owner is removed"), Collision->GetMoveIgnoreActors().Contains(FirstOwner));
	TestTrue(TEXT("SetOwner through reflection also ignores new owner"), Collision->GetMoveIgnoreActors().Contains(SecondOwner));
	Projectile->SetOwner(nullptr);
	TestFalse(TEXT("Clearing owner removes its ignore entry"), Collision->GetMoveIgnoreActors().Contains(SecondOwner));
	TestTrue(TEXT("Unrelated ignore entry is preserved"), Collision->GetMoveIgnoreActors().Contains(OtherIgnoredActor));

	const FTransform SpawnTransform(FVector(0, 1000, 0));
	auto* DeferredProjectile = World->SpawnActorDeferred<AGarProjectile>(AGarProjectile::StaticClass(), SpawnTransform,
		FirstOwner, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Deferred projectile"), DeferredProjectile)) return false;
	DeferredProjectile->FinishSpawning(SpawnTransform);
	TestTrue(TEXT("Deferred projectile completed component initialization"), DeferredProjectile->IsActorInitialized());
	auto* DeferredCollision = DeferredProjectile->FindComponentByClass<UCapsuleComponent>();
	TestTrue(TEXT("Spawn-time owner remains ignored after construction"), DeferredCollision->GetMoveIgnoreActors().Contains(FirstOwner));

	// Emulate property replication, which does not invoke SetOwner. This tests
	// RepNotify behavior without requiring a live network connection.
	auto* ReplicatedProjectile = World->SpawnActor<AGarProjectile>(SpawnParameters);
	if (!TestNotNull(TEXT("Replicated projectile"), ReplicatedProjectile)) return false;
	auto* ReplicatedCollision = ReplicatedProjectile->FindComponentByClass<UCapsuleComponent>();
	UFunction* OnRepOwner = ReplicatedProjectile->FindFunctionChecked(TEXT("OnRep_Owner"));
	ReplicatedProjectile->Owner = FirstOwner;
	ReplicatedProjectile->ProcessEvent(OnRepOwner, nullptr);
	TestTrue(TEXT("Replicated owner is ignored"), ReplicatedCollision->GetMoveIgnoreActors().Contains(FirstOwner));
	ReplicatedProjectile->Owner = SecondOwner;
	ReplicatedProjectile->ProcessEvent(OnRepOwner, nullptr);
	TestFalse(TEXT("Replication removes old owner ignore"), ReplicatedCollision->GetMoveIgnoreActors().Contains(FirstOwner));
	TestTrue(TEXT("Replication ignores new owner"), ReplicatedCollision->GetMoveIgnoreActors().Contains(SecondOwner));
	ReplicatedProjectile->Owner = nullptr;
	ReplicatedProjectile->ProcessEvent(OnRepOwner, nullptr);
	TestTrue(TEXT("Replicated owner removal clears ignore"), ReplicatedCollision->GetMoveIgnoreActors().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarProjectileOwnerSweepTest, "GAR.Projectile.OwnerIgnore.Sweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGarProjectileOwnerSweepTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	World->InitializeActorsForPlay(FURL());

	const auto SpawnBlocker = [World](const FVector& Location, ECollisionChannel ObjectType)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		auto* Collision = NewObject<UCapsuleComponent>(Actor);
		Actor->SetRootComponent(Collision);
		Actor->AddInstanceComponent(Collision);
		Collision->InitCapsuleSize(40.0f, 90.0f);
		Collision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Collision->SetCollisionObjectType(ObjectType);
		Collision->SetCollisionResponseToAllChannels(ECR_Block);
		Collision->SetGenerateOverlapEvents(false);
		Collision->SetWorldLocation(Location);
		Collision->RegisterComponent();
		return Actor;
	};
	AActor* Shooter = SpawnBlocker(FVector::ZeroVector, ECC_PhysicsBody);
	AActor* Target = SpawnBlocker(FVector(400, 0, 0), ECC_PhysicsBody);
	AActor* Wall = SpawnBlocker(FVector(0, 400, 0), ECC_WorldStatic);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Projectile = World->SpawnActor<AGarProjectile>(FVector(-200, 0, 0), FRotator::ZeroRotator, SpawnParameters);
	if (!TestNotNull(TEXT("Projectile"), Projectile)) return false;
	auto* Movement = Projectile->FindComponentByClass<UProjectileMovementComponent>();
	auto* Collision = Projectile->FindComponentByClass<UCapsuleComponent>();
	if (!TestNotNull(TEXT("Projectile movement"), Movement) || !TestNotNull(TEXT("Projectile collision"), Collision)) return false;
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	FHitResult Hit;
	Movement->SafeMoveUpdatedComponent(FVector(400, 0, 0), FQuat::Identity, true, Hit);
	TestTrue(TEXT("Baseline sweep really blocks on the shooter"), Hit.bBlockingHit && Hit.GetActor() == Shooter);

	Projectile->SetActorLocation(FVector(-200, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	Projectile->SetOwner(Shooter);
	Movement->SafeMoveUpdatedComponent(FVector(400, 0, 0), FQuat::Identity, true, Hit);
	TestFalse(TEXT("Sweep through owner does not hit"), Hit.bBlockingHit);
	TestTrue(TEXT("Sweep through owner reaches requested position"), Projectile->GetActorLocation().Equals(FVector(200, 0, 0), 0.01));
	Movement->SafeMoveUpdatedComponent(FVector(400, 0, 0), FQuat::Identity, true, Hit);
	TestTrue(TEXT("Another PhysicsBody actor is still hit"), Hit.bBlockingHit && Hit.GetActor() == Target);

	Projectile->SetActorLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
	Movement->SafeMoveUpdatedComponent(FVector(100, 0, 0), FQuat::Identity, true, Hit);
	TestFalse(TEXT("Starting inside owner is not a penetrating hit"), Hit.bBlockingHit || Hit.bStartPenetrating);
	TestTrue(TEXT("Starting inside owner has no depenetration offset"), Projectile->GetActorLocation().Equals(FVector(100, 0, 0), 0.01));
	Projectile->SetActorLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
	Movement->SafeMoveUpdatedComponent(FVector(0, 600, 0), FQuat::Identity, true, Hit);
	TestTrue(TEXT("WorldStatic geometry is still hit"), Hit.bBlockingHit && Hit.GetActor() == Wall);
	return true;
}

#endif
