#include "MetalPortal.h"

#include "MetalBotCharacter.h"
#include "MetalProjectile.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Actors are tracked this far (cm) in front of and behind the plane. */
	constexpr float TrackDepth = 3000.f;
}

AMetalPortal::AMetalPortal()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the player camera has moved this frame, so the window matches the view being rendered.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Surface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Surface"));
	Surface->SetupAttachment(RootComponent);
	Surface->SetStaticMesh(ConstructorHelpers::FObjectFinder<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane.Plane")).Object);
	Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Surface->SetCastShadow(false);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(RootComponent);
	// Placed in world space every frame, at the far end.
	Capture->SetUsingAbsoluteLocation(true);
	Capture->SetUsingAbsoluteRotation(true);
	Capture->SetUsingAbsoluteScale(true);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	// Linear HDR: the main view applies exposure and tonemapping once, so both sides match.
	Capture->CaptureSource = SCS_SceneColorHDRNoAlpha;
	Capture->ShowFlags.SetMotionBlur(false);
}

void AMetalPortal::Link(AMetalPortal* Other, float InRoomSide, const FVector2D& InSize, UMaterialInterface* InSurfaceMaterial)
{
	Linked = Other;
	RoomSide = InRoomSide >= 0.f ? 1.f : -1.f;
	Size = InSize;

	// The engine plane is 1 m square facing +Z: stand it up across the opening, facing this end's side.
	const FMatrix Facing = FRotationMatrix::MakeFromZX(FVector::ForwardVector * RoomSide, FVector::UpVector);
	Surface->SetRelativeRotation(Facing.Rotator());
	Surface->SetRelativeScale3D(FVector(Size.Y / 100.f, Size.X / 100.f, 1.f));
	if (InSurfaceMaterial)
	{
		SurfaceMaterial = UMaterialInstanceDynamic::Create(InSurfaceMaterial, this);
		Surface->SetMaterial(0, SurfaceMaterial);
	}
	AddHiddenActor(this);
	AddHiddenActor(Other);
}

void AMetalPortal::AddHiddenActor(AActor* Actor)
{
	if (Actor)
	{
		Capture->HiddenActors.AddUnique(Actor);
	}
}

FTransform AMetalPortal::MapToLinked(const FTransform& World) const
{
	const AMetalPortal* Other = Linked.Get();
	return Other ? World.GetRelativeTransform(GetActorTransform()) * Other->GetActorTransform() : World;
}

float AMetalPortal::GetSideDistance(const FVector& WorldLocation) const
{
	return FVector::DotProduct(WorldLocation - GetActorLocation(), GetActorForwardVector()) * RoomSide;
}

bool AMetalPortal::IsWithinOpening(const FVector& WorldLocation, float Margin) const
{
	const FVector Local = GetActorTransform().InverseTransformPositionNoScale(WorldLocation);
	return FMath::Abs(Local.Y) <= Size.X * 0.5f + Margin && FMath::Abs(Local.Z) <= Size.Y * 0.5f + Margin;
}

void AMetalPortal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Linked.IsValid())
	{
		return;
	}
	UpdateTransit();
	UpdateWindow();
	for (int32 Index = LagRestores.Num() - 1; Index >= 0; --Index)
	{
		if (--LagRestores[Index].Frames <= 0)
		{
			if (USpringArmComponent* Boom = LagRestores[Index].Boom.Get())
			{
				Boom->bEnableCameraLag = true;
			}
			LagRestores.RemoveAt(Index);
		}
	}
}

// --------------------------------------------------------------------------------------------
// Window
// --------------------------------------------------------------------------------------------

void AMetalPortal::EnsureRenderTarget()
{
	FVector2D Viewport(1280.f, 720.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Viewport);
	}
	const int32 Width = FMath::Max(64, FMath::RoundToInt(Viewport.X * ResolutionScale));
	const int32 Height = FMath::Max(64, FMath::RoundToInt(Viewport.Y * ResolutionScale));
	if (RenderTarget && RenderTarget->SizeX == Width && RenderTarget->SizeY == Height)
	{
		return;
	}
	RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(this, Width, Height, RTF_RGBA16f, FLinearColor::Black);
	Capture->TextureTarget = RenderTarget;
	if (SurfaceMaterial)
	{
		SurfaceMaterial->SetTextureParameterValue(TEXT("PortalTexture"), RenderTarget);
	}
}

void AMetalPortal::UpdateWindow()
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (Camera == nullptr)
	{
		return;
	}
	// Rendering a second view is expensive, so only capture when the viewer is on our side of the plane,
	// within CaptureDistance, and the surface was drawn recently (i.e. is actually on screen).
	const FVector ViewLocation = Camera->GetCameraLocation();
	const bool bActive = GetSideDistance(ViewLocation) > 0.f
		&& FVector::DistSquared(ViewLocation, GetActorLocation()) < FMath::Square(CaptureDistance)
		&& Surface->WasRecentlyRendered(0.25f);
	Capture->bCaptureEveryFrame = bActive;
	if (!bActive)
	{
		return;
	}
	EnsureRenderTarget();
	// Put the capture where the viewer's camera would be if it stood at the far end, with the same FOV.
	// The surface samples the result in screen space, so the two images line up exactly.
	Capture->SetWorldTransform(MapToLinked(FTransform(Camera->GetCameraRotation(), ViewLocation)));
	Capture->FOVAngle = Camera->GetFOVAngle();
}

// --------------------------------------------------------------------------------------------
// Transit
// --------------------------------------------------------------------------------------------

void AMetalPortal::UpdateTransit()
{
	TArray<AActor*> Nearby;
	const float Reach = Size.GetMax() + TrackDepth;
	const FVector Center = GetActorLocation();
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		if (FVector::DistSquared(It->GetActorLocation(), Center) < FMath::Square(Reach))
		{
			Nearby.Add(*It);
		}
	}
	for (TActorIterator<AMetalProjectile> It(GetWorld()); It; ++It)
	{
		if (FVector::DistSquared(It->GetActorLocation(), Center) < FMath::Square(Reach))
		{
			Nearby.Add(*It);
		}
	}

	for (AActor* Actor : Nearby)
	{
		const FVector Location = Actor->GetActorLocation();
		const float Side = GetSideDistance(Location);
		// Copy it: Add() below overwrites the same slot.
		const float* LastPtr = LastSide.Find(Actor);
		const float Last = LastPtr ? *LastPtr : -1.f;
		LastSide.Add(Actor, Side);
		// A crossing is a sign change from our side (positive) to the far side between two ticks, inside the
		// opening. Actors seen for the first time count as already on the far side, so nothing teleports
		// just for appearing near the portal.
		if (Last > 0.f && Side <= 0.f && IsWithinOpening(Location, 100.f))
		{
			SendThrough(Actor);
		}
	}
	// Forget actors that left (or were destroyed).
	for (auto It = LastSide.CreateIterator(); It; ++It)
	{
		const AActor* Actor = It.Key().Get();
		if (Actor == nullptr || FVector::DistSquared(Actor->GetActorLocation(), Center) >= FMath::Square(Reach))
		{
			It.RemoveCurrent();
		}
	}
}

void AMetalPortal::SendThrough(AActor* Actor)
{
	AMetalPortal* Other = Linked.Get();
	if (Cast<AMetalProjectile>(Actor))
	{
		// Shots don't carry across (yet): they fizzle at the threshold.
		Actor->Destroy();
		return;
	}
	APawn* Pawn = Cast<APawn>(Actor);
	if (Pawn == nullptr || Other == nullptr)
	{
		return;
	}

	// Rotation from this end's frame to the far end's; applied to velocity and the view direction.
	const FQuat Turn = Other->GetActorQuat() * GetActorQuat().Inverse();
	const FTransform Arrival = MapToLinked(Pawn->GetActorTransform());
	const FVector Velocity = Pawn->GetVelocity();
	// Pawns stay upright: only the yaw of the turn applies to the body.
	Pawn->TeleportTo(Arrival.GetLocation(), FRotator(0.f, Arrival.Rotator().Yaw, 0.f), false, true);
	if (UCharacterMovementComponent* Move = Pawn->FindComponentByClass<UCharacterMovementComponent>())
	{
		Move->Velocity = Turn.RotateVector(Velocity);
	}
	if (AController* Controller = Pawn->GetController())
	{
		Controller->SetControlRotation((Turn * Controller->GetControlRotation().Quaternion()).Rotator());
	}
	// The chase camera's lag would sweep it kilometres across the world after the pawn: switch the lag
	// off until the boom has snapped to the new spot.
	if (const AMetalBotCharacter* Bot = Cast<AMetalBotCharacter>(Pawn))
	{
		if (USpringArmComponent* Boom = Bot->GetCameraBoom(); Boom && Boom->bEnableCameraLag)
		{
			Boom->bEnableCameraLag = false;
			Other->LagRestores.Add({Boom, 2});
		}
	}
	// It arrives on the far end's own side; record that so it doesn't bounce straight back.
	Other->LastSide.Add(Pawn, Other->GetSideDistance(Pawn->GetActorLocation()));
	UE_LOG(LogMetal, Display, TEXT("%s went through %s to %s"), *Pawn->GetName(), *GetName(), *Pawn->GetActorLocation().ToCompactString());
}
