// One end of a linked pair of portals: the hangar mouth on a capital ship's hull and the matching
// mouth of its interior hangar, kilometres away. Each end is a window that shows the view through the
// other end, and pawns that fly through it come out of the other end at the same spot, still moving.
//
// Frames: each portal's +X points out of its hangar (the hull's into space, the interior's into the
// empty void past the mouth). Ends map one to one in those local frames, so standing 30 m inside the
// interior hangar looks out from 30 m inside the hull recess.
//
// The window is a scene capture placed where the viewer's camera would be at the other end, rendered
// to a screen-sized texture that the surface samples in screen space (so it lines up pixel for pixel).
// It only renders while the local player's camera is on this end's side, close enough, and the
// surface is on screen.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MetalPortal.generated.h"

class USceneCaptureComponent2D;
class USpringArmComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTextureRenderTarget2D;

UCLASS(NotPlaceable)
class AMetalPortal : public AActor
{
	GENERATED_BODY()

public:
	AMetalPortal();

	/**
	 * Connects this end to Other. RoomSide is +1 if this end's own world is in front of it (+X: the hull's
	 * end, open space) or -1 if it's behind it (the interior end, the hangar); crossing to the other side
	 * of the plane goes through. Size is the opening in cm (width, height).
	 */
	void Link(AMetalPortal* Other, float InRoomSide, const FVector2D& InSize, UMaterialInterface* SurfaceMaterial);
	/** Actors the capture for this end must not see (the far end's own ship hull, the portal surfaces). */
	void AddHiddenActor(AActor* Actor);

	AMetalPortal* GetLinked() const { return Linked.Get(); }
	/** A world transform near this end, moved to the same place relative to the linked end. */
	FTransform MapToLinked(const FTransform& World) const;
	/** Positive on this end's own side. */
	float GetSideDistance(const FVector& WorldLocation) const;
	bool IsWithinOpening(const FVector& WorldLocation, float Margin = 0.f) const;

	/** The window only renders while the viewer is closer than this (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	float CaptureDistance = 250000.f;

	/** Window texture size relative to the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization", meta=(ClampMin="0.1", ClampMax="1"))
	float ResolutionScale = 0.5f;

	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> Surface;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneCaptureComponent2D> Capture;

private:
	void UpdateWindow();
	void UpdateTransit();
	void SendThrough(AActor* Actor);
	void EnsureRenderTarget();

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;

	TWeakObjectPtr<AMetalPortal> Linked;
	/** Last side distance of every actor near the opening, to catch the frame it crosses. */
	TMap<TWeakObjectPtr<AActor>, float> LastSide;
	struct FLagRestore
	{
		TWeakObjectPtr<USpringArmComponent> Boom;
		/** Ticks left before the boom's camera lag is switched back on. */
		int32 Frames = 0;
	};
	TArray<FLagRestore> LagRestores;
	FVector2D Size = FVector2D(8000.f, 3000.f);
	float RoomSide = 1.f;
};
