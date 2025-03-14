// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftCombatDebug.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatDebugTextTest,
	"AircraftCombat.Debug.ActorTextOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatDebugTextTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).RequiresHitProxies(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None,
		nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("debug text world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AActor* Target = World->SpawnActor<AActor>();
	AActor* RadarSource = World->SpawnActor<AActor>();
	AActor* RWRSource = World->SpawnActor<AActor>();
	World->BeginPlay();

	using FPriority = FAircraftCombatDebug::EActorDebugTextPriority;
	auto CheckSelection = [&](const TCHAR* Description, const UObject* ExpectedSource, FPriority ExpectedPriority)
	{
		const UObject* SelectedSource = nullptr;
		FPriority SelectedPriority = FPriority::None;
		TestTrue(Description, FAircraftCombatDebug::GetActorDebugTextSelectionForTest(
			Target, SelectedSource, SelectedPriority));
		TestTrue(FString::Printf(TEXT("%s source"), Description), SelectedSource == ExpectedSource);
		TestEqual(FString::Printf(TEXT("%s priority"), Description), SelectedPriority, ExpectedPriority);
	};

	FAircraftCombatDebug::ResetActorDebugTextCache();
	FAircraftCombatDebug::DrawPrioritizedActorDebugText(World, RadarSource, Target,
		FPriority::Tracked, TEXT("RADAR"), FColor::Green);
	CheckSelection(TEXT("radar initial selection"), RadarSource, FPriority::Tracked);
	FAircraftCombatDebug::DrawPrioritizedActorDebugText(World, RWRSource, Target,
		FPriority::RWRThreat, TEXT("RWR"), FColor::Yellow);
	CheckSelection(TEXT("higher RWR selection"), RWRSource, FPriority::RWRThreat);
	FAircraftCombatDebug::DrawPrioritizedActorDebugText(World, RadarSource, Target,
		FPriority::Tracked, TEXT("RADAR AGAIN"), FColor::Green);
	CheckSelection(TEXT("lower radar does not displace RWR"), RWRSource, FPriority::RWRThreat);
	FAircraftCombatDebug::DrawPrioritizedActorDebugText(World, RWRSource, Target,
		FPriority::RWRThreat, TEXT("RWR UPDATED"), FColor::Yellow);
	CheckSelection(TEXT("same RWR source refreshes"), RWRSource, FPriority::RWRThreat);
	World->Tick(LEVELTICK_All, 0.3f);
	FAircraftCombatDebug::DrawPrioritizedActorDebugText(World, RadarSource, Target,
		FPriority::Tracked, TEXT("RADAR RECOVERS"), FColor::Green);
	CheckSelection(TEXT("expired RWR yields to radar"), RadarSource, FPriority::Tracked);

	FAircraftCombatDebug::ResetActorDebugTextCache();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
