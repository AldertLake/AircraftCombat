// -----------------------------------------------------
// Copyright   (c) 2023 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "DisplayComponent.h"
#include "AircraftDisplay.h"
#include "AircraftComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"

UAircraftDisplayComponent::UAircraftDisplayComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	TickWhenOffscreen = true;
	SetGenerateOverlapEvents(false);
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);

	BlendMode = EWidgetBlendMode::Transparent;
	WidgetClass = UAircraftDisplay::StaticClass();
	SetDrawSize(FVector2D(2000.0f, 2000.0f));
	TimingPolicy = EWidgetTimingPolicy::GameTime;
	bWindowFocusable = false;
}

void UAircraftDisplayComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UUserWidget* UserWidget = GetUserWidgetObject())
	{
		if (UAircraftDisplay* DisplayWidget = Cast<UAircraftDisplay>(UserWidget))
		{
			AircraftDisplayWidget = DisplayWidget;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("UAircraftDisplayComponent: Only children of UAircraftDisplay are supported! Found: %s on %s"), *GetNameSafe(UserWidget->GetClass()), *GetNameSafe(GetOwner()));
		}
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			DisplayMaterialTimerHandle,
			this,
			&UAircraftDisplayComponent::InitializeDisplayMaterial,
			0.1f,
			false
		);
	}
	else
	{
		InitializeDisplayMaterial();
	}
}

void UAircraftDisplayComponent::InitializeDisplayMaterial()
{
	if (!AircraftDisplayWidget)
	{
		if (UUserWidget* UserWidget = GetUserWidgetObject())
		{
			if (UAircraftDisplay* DisplayWidget = Cast<UAircraftDisplay>(UserWidget))
			{
				AircraftDisplayWidget = DisplayWidget;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("UAircraftDisplayComponent: Only children of UAircraftDisplay are supported! Found: %s on %s"), *GetNameSafe(UserWidget->GetClass()), *GetNameSafe(GetOwner()));
			}
		}
	}

	if (AircraftDisplayWidget)
	{
		AAircraftPawn* OwningAircraft = Cast<AAircraftPawn>(GetOwner());
		AircraftDisplayWidget->InitializeDisplay(OwningAircraft, this);
	}

	if (!bHasExternalDisplayMesh)
	{
		return;
	}

	UPrimitiveComponent* TargetMesh = GetDisplayMesh();
	if (!TargetMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("UAircraftDisplayComponent: DisplayMesh is not valid on %s!"), *GetNameSafe(GetOwner()));
		return;
	}

	UTextureRenderTarget2D* DisplayRenderTarget = GetRenderTarget();
	if (!DisplayRenderTarget)
	{
		UE_LOG(LogTemp, Error, TEXT("UAircraftDisplayComponent: GetRenderTarget() returned nullptr on %s!"), *GetNameSafe(GetOwner()));
		return;
	}

	UMaterialInterface* SourceMaterial = DisplayMaterial ? DisplayMaterial.Get() : nullptr;
	DisplayDynamicMaterial = TargetMesh->CreateDynamicMaterialInstance(SlotIndex, SourceMaterial, DisplayParameter);

	if (DisplayDynamicMaterial)
	{
		DisplayDynamicMaterial->SetTextureParameterValue(DisplayParameter, DisplayRenderTarget);
	}
}

UPrimitiveComponent* UAircraftDisplayComponent::GetDisplayMesh() const
{
	if (!bHasExternalDisplayMesh)
	{
		return nullptr;
	}

	if (DisplayMesh)
	{
		return DisplayMesh;
	}

	if (const AActor* Owner = GetOwner())
	{
		if (UPrimitiveComponent* ResolvedComp = Cast<UPrimitiveComponent>(DisplayMeshReference.GetComponent(const_cast<AActor*>(Owner))))
		{
			DisplayMesh = ResolvedComp;
			return ResolvedComp;
		}
	}

	return nullptr;
}
