#include "MetalFXAuthoring.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/StringOutputDevice.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "Stateless/NiagaraStatelessDistribution.h"
#include "Stateless/NiagaraStatelessEmitter.h"
#include "Stateless/NiagaraStatelessModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"

DEFINE_LOG_CATEGORY_STATIC(LogMetalFX, Log, All);

namespace
{
	/** A one-burst sprite system whose only emitter handle is cloned for every layer. */
	const TCHAR* TemplatePath = TEXT("/Niagara/DefaultAssets/Templates/Systems/DirectionalBurstLightweight.DirectionalBurstLightweight");

	/** Every system tints its particles with this user parameter (AMetalFX sets it per spawn). */
	const FName ColorParam(TEXT("User.Color"));

	FString Num(float Value)
	{
		return FString::SanitizeFloat(Value);
	}

	FString Join(std::initializer_list<float> Values)
	{
		TArray<FString> Parts;
		for (float Value : Values)
		{
			Parts.Add(Num(Value));
		}
		return FString::Join(Parts, TEXT(","));
	}

	// ---- Distribution text, in the format ExportText writes (see DescribeNiagaraSystem). Values are written as
	// editor channels; UpdateDistributions then fills Min/Max or the curve lookup tables from them. ----

	/** Same constant on every channel. */
	FString Const(float Value)
	{
		return FString::Printf(TEXT("(Mode=UniformConstant,ChannelConstantsAndRanges=(%s))"), *Num(Value));
	}

	/** One constant per channel (vectors, colours). */
	FString Consts(std::initializer_list<float> Values)
	{
		return FString::Printf(TEXT("(Mode=NonUniformConstant,ChannelConstantsAndRanges=(%s))"), *Join(Values));
	}

	/** Random between Min and Max, same roll on every channel. */
	FString Range(float Min, float Max)
	{
		return FString::Printf(TEXT("(Mode=UniformRange,ChannelConstantsAndRanges=(%s))"), *Join({Min, Max}));
	}

	/** One curve over normalized age, shared by every channel. Keys are (time, value) pairs. */
	FString Curve(std::initializer_list<FVector2f> Keys)
	{
		TArray<FString> Parts;
		for (const FVector2f& Key : Keys)
		{
			Parts.Add(FString::Printf(TEXT("(Time=%s,Value=%s)"), *Num(Key.X), *Num(Key.Y)));
		}
		return FString::Printf(TEXT("(Mode=UniformCurve,ChannelCurves=((Keys=(%s))))"), *FString::Join(Parts, TEXT(",")));
	}

	/** A colour scale over normalized age that only fades alpha (the materials multiply colour by alpha). */
	FString AlphaCurve(std::initializer_list<FVector2f> Keys)
	{
		TArray<FString> Parts;
		for (const FVector2f& Key : Keys)
		{
			Parts.Add(FString::Printf(TEXT("(Time=%s,Value=%s)"), *Num(Key.X), *Num(Key.Y)));
		}
		const FString One = TEXT("(Keys=((Time=0,Value=1)))");
		return FString::Printf(TEXT("(Mode=NonUniformCurve,ChannelCurves=(%s,%s,%s,(Keys=(%s))))"), *One, *One, *One, *FString::Join(Parts, TEXT(",")));
	}

	/**
	 * Sets one property from text.
	 *
	 * @param Object Object or module that owns the property.
	 * @param Name   Property name.
	 * @param Text   Value in ExportText format.
	 * @return False (with an error in the log) if the property is missing or the text didn't parse.
	 */
	bool Set(UObject* Object, const TCHAR* Name, const FString& Text)
	{
		FProperty* Property = Object ? FindFProperty<FProperty>(Object->GetClass(), Name) : nullptr;
		if (Property == nullptr)
		{
			UE_LOG(LogMetalFX, Error, TEXT("No property %s on %s"), Name, Object ? *Object->GetClass()->GetName() : TEXT("null"));
			return false;
		}
		FStringOutputDevice Errors;
		if (Property->ImportText_InContainer(*Text, Object, Object, PPF_None, &Errors) == nullptr || !Errors.IsEmpty())
		{
			UE_LOG(LogMetalFX, Error, TEXT("Could not set %s.%s = %s (%s)"), *Object->GetClass()->GetName(), Name, *Text, *Errors);
			return false;
		}
		return true;
	}

	/** FNiagaraDistributionBase isn't exported from Niagara, so its struct is looked up by name. */
	const UScriptStruct* DistributionStruct()
	{
		static const UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Niagara.NiagaraDistributionBase"));
		return Struct;
	}

	void UpdateDistributionsIn(const UStruct* Struct, void* Data);

	void UpdateValue(const UScriptStruct* Struct, void* Value)
	{
		if (Struct->IsChildOf(DistributionStruct()))
		{
			FNiagaraDistributionBase* Distribution = static_cast<FNiagaraDistributionBase*>(Value);
			if (!Distribution->IsBinding() && !Distribution->IsExpression()
				&& (Distribution->ChannelConstantsAndRanges.Num() > 0 || Distribution->ChannelCurves.Num() > 0))
			{
				Distribution->UpdateValuesFromDistribution();
			}
		}
		else
		{
			UpdateDistributionsIn(Struct, Value);
		}
	}

	/** Rebuilds every distribution's runtime values (Min/Max, curve tables) from its editor channels, recursively. */
	void UpdateDistributionsIn(const UStruct* Struct, void* Data)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(*It))
			{
				UpdateValue(StructProperty->Struct, StructProperty->ContainerPtrToValuePtr<void>(Data));
			}
			else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(*It))
			{
				if (const FStructProperty* Inner = CastField<FStructProperty>(ArrayProperty->Inner))
				{
					FScriptArrayHelper Array(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Data));
					for (int32 Index = 0; Index < Array.Num(); ++Index)
					{
						UpdateValue(Inner->Struct, Array.GetRawPtr(Index));
					}
				}
			}
		}
	}

	/** One emitter of a system being built, with helpers for its modules and renderer. */
	struct FLayer
	{
		UNiagaraStatelessEmitter* Emitter = nullptr;
		UMaterialInterface* SpriteMaterial = nullptr;
		bool bOk = true;

		UObject* Module(const TCHAR* ClassName)
		{
			UClass* Class = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Niagara.NiagaraStatelessModule_%s"), ClassName));
			UObject* Found = Class ? Emitter->GetModule(Class) : nullptr;
			if (Found == nullptr)
			{
				UE_LOG(LogMetalFX, Error, TEXT("Emitter %s has no %s module"), *Emitter->GetName(), ClassName);
				bOk = false;
			}
			return Found;
		}

		/** Enables a module and sets the given properties on it. */
		FLayer& Use(const TCHAR* ClassName, std::initializer_list<TPair<const TCHAR*, FString>> Properties = {})
		{
			if (UObject* Found = Module(ClassName))
			{
				bOk &= Set(Found, TEXT("bModuleEnabled"), TEXT("True"));
				for (const TPair<const TCHAR*, FString>& Property : Properties)
				{
					bOk &= Set(Found, Property.Key, Property.Value);
				}
			}
			return *this;
		}

		FLayer& Disable(const TCHAR* ClassName)
		{
			if (UObject* Found = Module(ClassName))
			{
				bOk &= Set(Found, TEXT("bModuleEnabled"), TEXT("False"));
			}
			return *this;
		}

		/** A single burst of Min..Max particles at SpawnTime; the emitter runs once for Duration seconds. */
		FLayer& Burst(int32 Min, int32 Max, float Duration, float SpawnTime = 0.f)
		{
			bOk &= Set(Emitter, TEXT("SpawnInfos"), FString::Printf(TEXT("((SourceId=%s,Type=Burst,SpawnTime=%s,Amount=(Mode=UniformRange,Min=%d,Max=%d)))"),
				*FGuid::NewGuid().ToString(), *Num(SpawnTime), Min, Max));
			bOk &= Set(Emitter, TEXT("EmitterState"), FString::Printf(TEXT("(LoopBehavior=Once,LoopDuration=%s)"), *Const(Duration)));
			return *this;
		}

		/** Keeps spawning forever: one particle per Loop seconds (Rate <= 0) or Rate particles a second. */
		FLayer& Looping(float Loop, float Rate)
		{
			const FString Spawn = Rate > 0.f
				? FString::Printf(TEXT("((SourceId=%s,Type=Rate,Rate=%s))"), *FGuid::NewGuid().ToString(), *Const(Rate))
				: FString::Printf(TEXT("((SourceId=%s,Type=Burst,SpawnTime=0,Amount=(Mode=UniformConstant,Min=1,Max=1)))"), *FGuid::NewGuid().ToString());
			bOk &= Set(Emitter, TEXT("SpawnInfos"), Spawn);
			bOk &= Set(Emitter, TEXT("EmitterState"), FString::Printf(TEXT("(LoopBehavior=Infinite,LoopDuration=%s)"), *Const(Loop)));
			return *this;
		}

		/** Culling box in the effect's own (unscaled) space. */
		FLayer& Bounds(float Extent)
		{
			bOk &= Set(Emitter, TEXT("FixedBounds"), FString::Printf(TEXT("(Min=(X=%s,Y=%s,Z=%s),Max=(X=%s,Y=%s,Z=%s),IsValid=True)"),
				*Num(-Extent), *Num(-Extent), *Num(-Extent), *Num(Extent), *Num(Extent), *Num(Extent)));
			return *this;
		}

		/** Configures the template's sprite renderer. */
		FLayer& Sprites(UMaterialInterface* Material, bool bVelocityAligned, int32 SortOrder)
		{
			for (UNiagaraRendererProperties* Renderer : Emitter->GetRenderers())
			{
				if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
				{
					Sprite->Material = Material;
					bOk &= Set(Sprite, TEXT("Alignment"), bVelocityAligned ? TEXT("VelocityAligned") : TEXT("Unaligned"));
					bOk &= Set(Sprite, TEXT("FacingMode"), TEXT("FaceCamera"));
					bOk &= Set(Sprite, TEXT("SortOrderHint"), FString::FromInt(SortOrder));
					Sprite->PostEditChange();
				}
			}
			return *this;
		}

		/** Swaps the sprite renderer for a mesh renderer showing Mesh with Material. */
		FLayer& Meshes(UStaticMesh* Mesh, UMaterialInterface* Material, int32 SortOrder)
		{
			for (UNiagaraRendererProperties* Renderer : TArray<UNiagaraRendererProperties*>(Emitter->GetRenderers()))
			{
				Emitter->RemoveRenderer(Renderer, FGuid());
			}
			UNiagaraMeshRendererProperties* MeshRenderer = NewObject<UNiagaraMeshRendererProperties>(Emitter, TEXT("MeshRenderer"), RF_Transactional);
			MeshRenderer->Meshes.SetNum(1);
			MeshRenderer->Meshes[0].Mesh = Mesh;
			bOk &= Set(MeshRenderer, TEXT("bOverrideMaterials"), TEXT("True"));
			MeshRenderer->OverrideMaterials.SetNum(1);
			MeshRenderer->OverrideMaterials[0].ExplicitMat = Material;
			bOk &= Set(MeshRenderer, TEXT("SortOrderHint"), FString::FromInt(SortOrder));
			Emitter->AddRenderer(MeshRenderer, FGuid());
			MeshRenderer->PostEditChange();
			return *this;
		}

		/** Particle colour comes from User.Color (Alpha scales it), or a fixed colour when Fixed is set. */
		FLayer& Color(TOptional<FLinearColor> Fixed = {})
		{
			UObject* Init = Module(TEXT("InitializeParticle"));
			if (Init == nullptr)
			{
				return *this;
			}
			if (Fixed.IsSet())
			{
				const FLinearColor& C = Fixed.GetValue();
				bOk &= Set(Init, TEXT("ColorDistribution"), Consts({C.R, C.G, C.B, C.A}));
				return *this;
			}
			FStructProperty* Property = FindFProperty<FStructProperty>(Init->GetClass(), TEXT("ColorDistribution"));
			FNiagaraDistributionBase* Distribution = Property ? Property->ContainerPtrToValuePtr<FNiagaraDistributionBase>(Init) : nullptr;
			if (Distribution == nullptr)
			{
				bOk = false;
				return *this;
			}
			Distribution->Mode = ENiagaraDistributionMode::Binding;
			Distribution->ParameterBinding = FNiagaraVariableBase(FNiagaraTypeDefinition::GetColorDef(), ColorParam);
			return *this;
		}
	};

	/** A system being built: the template's handle is cloned once per layer. */
	struct FSystemBuilder
	{
		UNiagaraSystem* System = nullptr;
		UPackage* Package = nullptr;
		TArray<FLayer> Layers;

		/**
		 * Creates (or replaces) Folder/Name as a copy of the template and splits it into one emitter per layer name.
		 */
		bool Begin(const FString& Folder, const FString& Name, std::initializer_list<const TCHAR*> LayerNames)
		{
			UNiagaraSystem* Template = LoadObject<UNiagaraSystem>(nullptr, TemplatePath);
			if (Template == nullptr || Template->GetEmitterHandles().Num() != 1)
			{
				UE_LOG(LogMetalFX, Error, TEXT("Template %s is missing or changed"), TemplatePath);
				return false;
			}
			const FString PackageName = Folder / Name;
			Package = CreatePackage(*PackageName);
			Package->FullyLoad();
			const bool bExisted = FindObject<UNiagaraSystem>(Package, *Name) != nullptr;
			if (UObject* Old = FindObject<UObject>(Package, *Name))
			{
				// Move the previous version out of the way so the rebuilt system can take its name.
				Old->ClearFlags(RF_Public | RF_Standalone);
				Old->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			}
			System = DuplicateObject<UNiagaraSystem>(Template, Package, *Name);
			System->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
			if (!bExisted)
			{
				FAssetRegistryModule::AssetCreated(System);
			}

			FNiagaraEmitterHandle First = System->GetEmitterHandles()[0];
			for (int32 Index = 1; Index < int32(LayerNames.size()); ++Index)
			{
				System->DuplicateEmitterHandle(First, FName(LayerNames.begin()[Index]));
			}
			TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
			for (int32 Index = 0; Index < Handles.Num(); ++Index)
			{
				Handles[Index].SetName(FName(LayerNames.begin()[Index]), *System);
				FLayer& Layer = Layers.AddDefaulted_GetRef();
				Layer.Emitter = Handles[Index].GetStatelessEmitter();
				if (Layer.Emitter == nullptr)
				{
					UE_LOG(LogMetalFX, Error, TEXT("%s: layer %d has no lightweight emitter"), *Name, Index);
					return false;
				}
				// Start every layer from a clean slate: the template enables a few forces we don't always want.
				for (const TCHAR* Optional : { TEXT("GravityForce"), TEXT("Drag"), TEXT("AddVelocity"), TEXT("ScaleColor"), TEXT("ScaleSpriteSize"),
					TEXT("ShapeLocation"), TEXT("AccelerationForce"), TEXT("ScaleMeshSize"), TEXT("SpriteRotationRate") })
				{
					Layer.Disable(Optional);
				}
				// Effects are sized by the component's scale (AMetalFX scales them to the requested size).
				Layer.Use(TEXT("ApplyOwnerScaleToAttributes"), { { TEXT("SystemScaleData"), TEXT("(bScaleInitialSpriteSize=True,bScaleInitialSpriteSizeBySingleAxis=True)") } });
				Layer.Color();
				Layer.Bounds(200.f);
			}

			FNiagaraUserRedirectionParameterStore& User = System->GetExposedParameters();
			const FNiagaraVariable ColorVariable(FNiagaraTypeDefinition::GetColorDef(), ColorParam);
			User.AddParameter(ColorVariable, true, true);
			User.SetParameterValue(FLinearColor::White, ColorVariable, true);
			return true;
		}

		FLayer& operator[](int32 Index) { return Layers[Index]; }

		/** Rebuilds the distributions, compiles and saves. */
		bool Finish()
		{
			bool bOk = true;
			for (FLayer& Layer : Layers)
			{
				bOk &= Layer.bOk;
				TArray<UObject*> Inner;
				GetObjectsWithOuter(Layer.Emitter, Inner, EGetObjectsFlags::IncludeNestedObjects);
				Inner.Add(Layer.Emitter);
				for (UObject* Object : Inner)
				{
					UpdateDistributionsIn(Object->GetClass(), Object);
				}
				for (UObject* Object : Inner)
				{
					Object->PostEditChange();
				}
			}
			System->PostEditChange();
			System->RequestCompile(false);
			System->WaitForCompilationComplete(true, false);

			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			Package->MarkPackageDirty();
			if (!UPackage::SavePackage(Package, System, *Filename, Args))
			{
				UE_LOG(LogMetalFX, Error, TEXT("Could not save %s"), *Filename);
				return false;
			}
			UE_LOG(LogMetalFX, Display, TEXT("Built %s (%d layers)%s"), *System->GetPathName(), Layers.Num(), bOk ? TEXT("") : TEXT(" WITH ERRORS"));
			return bOk;
		}
	};

	FVector2f K(float Time, float Value)
	{
		return FVector2f(Time, Value);
	}
}

FString UMetalFXAuthoring::DescribeNiagaraSystem(const FString& SystemPath)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (System == nullptr)
	{
		return FString::Printf(TEXT("No Niagara system at %s"), *SystemPath);
	}
	FString Out = FString::Printf(TEXT("System %s, %d emitter handles\n"), *System->GetPathName(), System->GetEmitterHandles().Num());
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		const UObject* Stateless = Handle.GetStatelessEmitter();
		const FString StatelessName = Stateless ? Stateless->GetPathName() : FString(TEXT("none"));
		Out += FString::Printf(TEXT("  handle %s mode=%d stateless=%s\n"), *Handle.GetName().ToString(), int32(Handle.GetEmitterMode()), *StatelessName);
	}
	TArray<UObject*> Inner;
	GetObjectsWithOuter(System, Inner, EGetObjectsFlags::IncludeNestedObjects);
	for (UObject* Object : Inner)
	{
		Out += FString::Printf(TEXT("OBJECT %s (%s)\n"), *Object->GetPathName(System), *Object->GetClass()->GetName());
		for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Edit))
			{
				continue;
			}
			FString Value;
			It->ExportText_InContainer(0, Value, Object, Object, Object, PPF_None);
			if (Value.Len() > 600)
			{
				Value = Value.Left(600) + TEXT("...");
			}
			Out += FString::Printf(TEXT("    %s = %s\n"), *It->GetName(), *Value);
		}
	}
	UE_LOG(LogMetalFX, Display, TEXT("%s"), *Out);
	return Out;
}

int32 UMetalFXAuthoring::BuildEffectSystems(const FString& Folder, UMaterialInterface* SpriteMaterial, UMaterialInterface* HotMaterial,
	UMaterialInterface* SmokeMaterial, UMaterialInterface* FireMaterial, UMaterialInterface* BeamMaterial, UStaticMesh* BeamMesh)
{
	if (!SpriteMaterial || !HotMaterial || !SmokeMaterial || !FireMaterial || !BeamMaterial || !BeamMesh || DistributionStruct() == nullptr)
	{
		UE_LOG(LogMetalFX, Error, TEXT("BuildEffectSystems: missing material, mesh or Niagara types"));
		return 0;
	}
	int32 Built = 0;
	// Every system is authored at a reference size (noted per system); AMetalFX scales the component to fit.
	// The effects face down local +X where a direction matters (muzzle flashes, impacts).

	// Muzzle flash, reference size 10 cm: a hot star, flame petals and a few sparks down +X.
	{
		FSystemBuilder B;
		if (B.Begin(Folder, TEXT("NS_MuzzleFlash"), { TEXT("Flash"), TEXT("Petals"), TEXT("Sparks") }))
		{
			B[0].Burst(2, 3, 0.1f).Sprites(HotMaterial, false, 2)
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(0.04f, 0.07f) }, { TEXT("SpriteSizeDistribution"), Range(16.f, 26.f) },
					{ TEXT("SpriteRotationDistribution"), Range(0.f, 360.f) } })
				.Use(TEXT("ScaleSpriteSize"), { { TEXT("ScaleDistribution"), Curve({ K(0.f, 0.7f), K(1.f, 1.3f) }) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(1.f, 0.f) }) } });
			B[1].Burst(3, 4, 0.1f).Sprites(SpriteMaterial, true, 1)
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(0.05f, 0.08f) }, { TEXT("SpriteSizeDistribution"), Consts({ 7.f, 26.f }) } })
				.Use(TEXT("AddVelocity"), { { TEXT("VelocityType"), TEXT("InCone") }, { TEXT("ConeRotationType"), TEXT("Direction") },
					{ TEXT("ConeDirection"), Consts({ 1.f, 0.f, 0.f }) }, { TEXT("ConeAngle"), TEXT("22") }, { TEXT("ConeVelocityDistribution"), Range(250.f, 450.f) } })
				.Use(TEXT("Drag"), { { TEXT("DragDistribution"), Const(12.f) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(1.f, 0.f) }) } });
			B[2].Burst(3, 6, 0.3f).Sprites(HotMaterial, true, 3)
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(0.08f, 0.18f) }, { TEXT("SpriteSizeDistribution"), Consts({ 1.2f, 8.f }) } })
				.Use(TEXT("AddVelocity"), { { TEXT("VelocityType"), TEXT("InCone") }, { TEXT("ConeRotationType"), TEXT("Direction") },
					{ TEXT("ConeDirection"), Consts({ 1.f, 0.f, 0.f }) }, { TEXT("ConeAngle"), TEXT("35") }, { TEXT("ConeVelocityDistribution"), Range(700.f, 1500.f) } })
				.Use(TEXT("Drag"), { { TEXT("DragDistribution"), Const(5.f) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(1.f, 0.f) }) } });
			Built += B.Finish();
		}
	}

	// Impact, reference size 10 cm: a flash, sparks bouncing off along +X (the surface normal) and a dust puff.
	{
		FSystemBuilder B;
		if (B.Begin(Folder, TEXT("NS_Impact"), { TEXT("Flash"), TEXT("Sparks"), TEXT("Dust") }))
		{
			B[0].Burst(1, 2, 0.1f).Sprites(HotMaterial, false, 2)
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(0.05f, 0.09f) }, { TEXT("SpriteSizeDistribution"), Range(14.f, 22.f) },
					{ TEXT("SpriteRotationDistribution"), Range(0.f, 360.f) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(1.f, 0.f) }) } });
			B[1].Burst(7, 12, 0.6f).Sprites(HotMaterial, true, 3).Bounds(400.f)
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(0.18f, 0.45f) }, { TEXT("SpriteSizeDistribution"), Consts({ 1.2f, 6.f }) } })
				.Use(TEXT("AddVelocity"), { { TEXT("VelocityType"), TEXT("InCone") }, { TEXT("ConeRotationType"), TEXT("Direction") },
					{ TEXT("ConeDirection"), Consts({ 1.f, 0.f, 0.f }) }, { TEXT("ConeAngle"), TEXT("70") }, { TEXT("ConeVelocityDistribution"), Range(250.f, 750.f) } })
				.Use(TEXT("AccelerationForce"), { { TEXT("AccelerationDistribution"), Consts({ 0.f, 0.f, -980.f }) }, { TEXT("CoordinateSpace"), TEXT("World") } })
				.Use(TEXT("Drag"), { { TEXT("DragDistribution"), Const(1.5f) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(0.6f, 0.8f), K(1.f, 0.f) }) } });
			B[2].Burst(1, 2, 1.f).Sprites(SmokeMaterial, false, 0).Color(FLinearColor(0.3f, 0.28f, 0.25f, 0.45f))
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(0.5f, 0.8f) }, { TEXT("SpriteSizeDistribution"), Range(10.f, 16.f) },
					{ TEXT("SpriteRotationDistribution"), Range(0.f, 360.f) } })
				.Use(TEXT("AddVelocity"), { { TEXT("VelocityType"), TEXT("InCone") }, { TEXT("ConeRotationType"), TEXT("Direction") },
					{ TEXT("ConeDirection"), Consts({ 1.f, 0.f, 0.f }) }, { TEXT("ConeAngle"), TEXT("30") }, { TEXT("ConeVelocityDistribution"), Range(20.f, 60.f) } })
				.Use(TEXT("ScaleSpriteSize"), { { TEXT("ScaleDistribution"), Curve({ K(0.f, 1.f), K(1.f, 2.6f) }) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(1.f, 0.f) }) } });
			Built += B.Finish();
		}
	}

	// Explosion, reference radius 100 cm: white-hot core, fireball puffs, flying sparks and a rising smoke cloud.
	{
		FSystemBuilder B;
		if (B.Begin(Folder, TEXT("NS_Explosion"), { TEXT("Core"), TEXT("Fireball"), TEXT("Sparks"), TEXT("Smoke") }))
		{
			B[0].Burst(1, 1, 0.2f).Sprites(HotMaterial, false, 3)
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Const(0.14f) }, { TEXT("SpriteSizeDistribution"), Const(170.f) } })
				.Use(TEXT("ScaleSpriteSize"), { { TEXT("ScaleDistribution"), Curve({ K(0.f, 0.6f), K(1.f, 1.4f) }) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(1.f, 0.f) }) } });
			B[1].Burst(9, 12, 0.7f).Sprites(FireMaterial, false, 1).Bounds(400.f)
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(0.3f, 0.55f) }, { TEXT("SpriteSizeDistribution"), Range(55.f, 95.f) },
					{ TEXT("SpriteRotationDistribution"), Range(0.f, 360.f) } })
				.Use(TEXT("ShapeLocation"), { { TEXT("ShapePrimitive"), TEXT("Sphere") }, { TEXT("SphereRadius"), Const(25.f) } })
				.Use(TEXT("AddVelocity"), { { TEXT("VelocityType"), TEXT("FromPoint") }, { TEXT("PointVelocityDistribution"), Range(60.f, 200.f) } })
				.Use(TEXT("Drag"), { { TEXT("DragDistribution"), Const(3.f) } })
				.Use(TEXT("SpriteRotationRate"), { { TEXT("RotationRateDistribution"), Range(-90.f, 90.f) } })
				.Use(TEXT("ScaleSpriteSize"), { { TEXT("ScaleDistribution"), Curve({ K(0.f, 0.5f), K(0.3f, 1.2f), K(1.f, 1.7f) }) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(0.4f, 0.8f), K(1.f, 0.f) }) } });
			B[2].Burst(20, 30, 1.2f).Sprites(HotMaterial, true, 4).Bounds(1500.f)
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(0.4f, 0.95f) }, { TEXT("SpriteSizeDistribution"), Consts({ 2.5f, 14.f }) } })
				.Use(TEXT("ShapeLocation"), { { TEXT("ShapePrimitive"), TEXT("Sphere") }, { TEXT("SphereRadius"), Const(15.f) } })
				.Use(TEXT("AddVelocity"), { { TEXT("VelocityType"), TEXT("FromPoint") }, { TEXT("PointVelocityDistribution"), Range(500.f, 1300.f) } })
				.Use(TEXT("AccelerationForce"), { { TEXT("AccelerationDistribution"), Consts({ 0.f, 0.f, -980.f }) }, { TEXT("CoordinateSpace"), TEXT("World") } })
				.Use(TEXT("Drag"), { { TEXT("DragDistribution"), Const(1.5f) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(0.7f, 0.9f), K(1.f, 0.f) }) } });
			B[3].Burst(6, 8, 2.2f, 0.05f).Sprites(SmokeMaterial, false, 0).Bounds(600.f).Color(FLinearColor(0.08f, 0.07f, 0.065f, 0.6f))
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(1.2f, 2.f) }, { TEXT("SpriteSizeDistribution"), Range(60.f, 100.f) },
					{ TEXT("SpriteRotationDistribution"), Range(0.f, 360.f) } })
				.Use(TEXT("ShapeLocation"), { { TEXT("ShapePrimitive"), TEXT("Sphere") }, { TEXT("SphereRadius"), Const(35.f) } })
				.Use(TEXT("AddVelocity"), { { TEXT("VelocityType"), TEXT("FromPoint") }, { TEXT("PointVelocityDistribution"), Range(30.f, 90.f) } })
				.Use(TEXT("AccelerationForce"), { { TEXT("AccelerationDistribution"), Consts({ 0.f, 0.f, 60.f }) } })
				.Use(TEXT("Drag"), { { TEXT("DragDistribution"), Const(1.f) } })
				.Use(TEXT("SpriteRotationRate"), { { TEXT("RotationRateDistribution"), Range(-25.f, 25.f) } })
				.Use(TEXT("ScaleSpriteSize"), { { TEXT("ScaleDistribution"), Curve({ K(0.f, 0.6f), K(1.f, 2.2f) }) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 0.f), K(0.15f, 1.f), K(1.f, 0.f) }) } });
			Built += B.Finish();
		}
	}

	// Generic burst (pickups, consoles, sparks of light), reference radius 100 cm: a swelling glow and drifting motes.
	{
		FSystemBuilder B;
		if (B.Begin(Folder, TEXT("NS_Burst"), { TEXT("Glow"), TEXT("Motes") }))
		{
			B[0].Burst(1, 1, 0.4f).Sprites(SpriteMaterial, false, 1)
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Const(0.25f) }, { TEXT("SpriteSizeDistribution"), Const(200.f) } })
				.Use(TEXT("ScaleSpriteSize"), { { TEXT("ScaleDistribution"), Curve({ K(0.f, 0.3f), K(1.f, 1.f) }) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(1.f, 0.f) }) } });
			B[1].Burst(10, 14, 0.8f).Sprites(HotMaterial, false, 2).Bounds(300.f)
				.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(0.25f, 0.55f) }, { TEXT("SpriteSizeDistribution"), Range(5.f, 10.f) } })
				.Use(TEXT("ShapeLocation"), { { TEXT("ShapePrimitive"), TEXT("Sphere") }, { TEXT("SphereRadius"), Const(20.f) } })
				.Use(TEXT("AddVelocity"), { { TEXT("VelocityType"), TEXT("FromPoint") }, { TEXT("PointVelocityDistribution"), Range(150.f, 400.f) } })
				.Use(TEXT("Drag"), { { TEXT("DragDistribution"), Const(3.f) } })
				.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(1.f, 0.f) }) } });
			Built += B.Finish();
		}
	}

	// Beams are a unit cylinder (100 cm along local Z, centred) that AMetalFX stretches between the end points:
	// a bright core, a faint halo and crackling sparks spread along it.
	auto BuildBeam = [&](const TCHAR* Name, bool bPersistent) -> bool
	{
		FSystemBuilder B;
		if (!B.Begin(Folder, Name, { TEXT("Core"), TEXT("Halo"), TEXT("Crackle") }))
		{
			return false;
		}
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FLayer& Layer = B[Index];
			const float Width = Index == 0 ? 1.f : 3.f;
			Layer.Meshes(BeamMesh, BeamMaterial, Index == 0 ? 2 : 1).Bounds(100.f)
				.Use(TEXT("InitializeParticle"), { { TEXT("MeshScaleDistribution"), Consts({ Width, Width, 1.f }) } });
			if (bPersistent)
			{
				// One particle per second, each living a little longer than its loop, so the beam never blinks.
				Layer.Looping(1.f, 0.f).Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Const(1.05f) } });
				if (Index == 1)
				{
					Layer.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), Consts({ 1.f, 1.f, 1.f, 0.25f }) } });
				}
			}
			else
			{
				Layer.Burst(1, 1, 0.2f).Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Const(0.1f) } })
					.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, Index == 0 ? 1.f : 0.3f), K(1.f, 0.f) }) } })
					.Use(TEXT("ScaleMeshSize"), { { TEXT("ScaleDistribution"), FString::Printf(TEXT("(Mode=NonUniformCurve,ChannelCurves=(%s,%s,%s))"),
						TEXT("(Keys=((Time=0,Value=1),(Time=1,Value=0)))"), TEXT("(Keys=((Time=0,Value=1),(Time=1,Value=0)))"), TEXT("(Keys=((Time=0,Value=1)))")) } });
			}
		}
		FLayer& Crackle = B[2];
		Crackle.Sprites(HotMaterial, false, 3).Bounds(100.f)
			.Use(TEXT("InitializeParticle"), { { TEXT("LifetimeDistribution"), Range(0.05f, 0.12f) }, { TEXT("SpriteSizeDistribution"), Range(2.f, 5.f) } })
			.Use(TEXT("ShapeLocation"), { { TEXT("ShapePrimitive"), TEXT("Box") }, { TEXT("BoxSize"), Consts({ 60.f, 60.f, 100.f }) } })
			.Use(TEXT("ScaleColor"), { { TEXT("ScaleDistribution"), AlphaCurve({ K(0.f, 1.f), K(1.f, 0.f) }) } });
		// Sprites aren't stretched by the beam's thin X/Y scale, so their size is in plain centimetres.
		Crackle.Use(TEXT("ApplyOwnerScaleToAttributes"), { { TEXT("SystemScaleData"), TEXT("(bScaleInitialSpriteSize=False)") } });
		if (bPersistent)
		{
			Crackle.Looping(1.f, 45.f);
		}
		else
		{
			Crackle.Burst(3, 5, 0.2f);
		}
		return B.Finish();
	};
	Built += BuildBeam(TEXT("NS_Beam"), true);
	Built += BuildBeam(TEXT("NS_Tracer"), false);
	return Built;
}
