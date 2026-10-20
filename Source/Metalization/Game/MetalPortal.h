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

/**
 * One end of a two-way portal. Spawned in pairs by AMetalCapitalShip::CreatePortals and never placed by
 * hand. Each tick it teleports pawns that cross its plane inside the opening (destroying projectiles),
 * and renders the view through the other end into its surface while the local camera can see it.
 */
UCLASS(NotPlaceable)
class AMetalPortal : public AActor
{
	GENERATED_BODY()

public:
	/** Creates the window plane and the scene capture, ticking after the camera has updated. */
	AMetalPortal();

	/**
	 * Connects this end to Other. RoomSide is +1 if this end's own world is in front of it (+X: the hull's
	 * end, open space) or -1 if it's behind it (the interior end, the hangar); crossing to the other side
	 * of the plane goes through. Size is the opening in cm (width, height).
	 *
	 * @param Other           The far end (link both ends, each to the other).
	 * @param InRoomSide      +1 or -1 as described above.
	 * @param InSize          Opening width and height in cm.
	 * @param SurfaceMaterial Window material sampling a "PortalTexture" parameter in screen space; may be null.
	 */
	void Link(AMetalPortal* Other, float InRoomSide, const FVector2D& InSize, UMaterialInterface* SurfaceMaterial);

	/**
	 * Actors the capture for this end must not see (the far end's own ship hull, the portal surfaces).
	 *
	 * @param Actor Actor to hide from this end's capture.
	 */
	void AddHiddenActor(AActor* Actor);

	/** @return The far end, or null before Link. */
	AMetalPortal* GetLinked() const { return Linked.Get(); }

	/**
	 * A world transform near this end, moved to the same place relative to the linked end.
	 *
	 * @param World Transform relative to this end.
	 * @return The equivalent transform at the linked end (unchanged if unlinked).
	 */
	FTransform MapToLinked(const FTransform& World) const;

	/**
	 * Positive on this end's own side.
	 *
	 * @param WorldLocation Point to test.
	 * @return Signed distance from the portal plane in cm.
	 */
	float GetSideDistance(const FVector& WorldLocation) const;

	/**
	 * @param WorldLocation Point to test (only its position across the plane matters).
	 * @param Margin        Extra tolerance in cm around the opening.
	 * @return True if the point lies inside the opening's rectangle.
	 */
	bool IsWithinOpening(const FVector& WorldLocation, float Margin = 0.f) const;

	/** The window only renders while the viewer is closer than this (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization")
	float CaptureDistance = 250000.f;

	/** Window texture size relative to the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Metalization", meta=(ClampMin="0.1", ClampMax="1"))
	float ResolutionScale = 0.5f;

	/** Handles crossings, updates the window, and re-enables camera lag on pawns that came through. */
	virtual void Tick(float DeltaSeconds) override;

protected:
	/** The window: an engine plane stretched across the opening, facing this end's side. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UStaticMeshComponent> Surface;

	/** Renders the far end's view into RenderTarget. Positioned in world space at the far end each frame. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USceneCaptureComponent2D> Capture;

private:
	/** Turns the capture on only when worthwhile, and moves it to the viewer's mirrored position at the far end. */
	void UpdateWindow();

	/** Tracks pawns and projectiles near the opening and sends through any that crossed the plane this frame. */
	void UpdateTransit();

	/**
	 * Teleports a pawn to the matching spot at the far end, keeping its velocity and view direction
	 * (rotated into the far end's frame). Projectiles are destroyed instead.
	 *
	 * @param Actor The actor that just crossed.
	 */
	void SendThrough(AActor* Actor);

	/** (Re)creates the render target whenever the viewport size changes. */
	void EnsureRenderTarget();

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;

	/** The other end. */
	TWeakObjectPtr<AMetalPortal> Linked;
	/** Last side distance of every actor near the opening, to catch the frame it crosses. */
	TMap<TWeakObjectPtr<AActor>, float> LastSide;
	/** A camera boom whose lag was switched off for a teleport, waiting to be switched back on. */
	struct FLagRestore
	{
		TWeakObjectPtr<USpringArmComponent> Boom;
		/** Ticks left before the boom's camera lag is switched back on. */
		int32 Frames = 0;
	};
	TArray<FLagRestore> LagRestores;
	/** Opening width and height, cm. */
	FVector2D Size = FVector2D(8000.f, 3000.f);
	/** +1 if this end's own side is in front of it (+X), -1 if behind. */
	float RoomSide = 1.f;
};
