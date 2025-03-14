// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "RadarOperatorLink.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

ARadarOperatorLink::ARadarOperatorLink()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	bAlwaysRelevant = false;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = false;
}

void ARadarOperatorLink::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Radar();
}

void ARadarOperatorLink::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IsValid(Radar)) Radar->UnregisterLocalOperatorLink(this);
	Super::EndPlay(EndPlayReason);
}

void ARadarOperatorLink::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARadarOperatorLink, Radar);
	DOREPLIFETIME(ARadarOperatorLink, Snapshot);
	DOREPLIFETIME(ARadarOperatorLink, bCanControl);
}

void ARadarOperatorLink::Initialize(UAircraftRadarComponent* InRadar)
{
	check(HasAuthority());
	Radar = InRadar;
	ForceNetUpdate();
}

void ARadarOperatorLink::SetCanControl(bool bInCanControl)
{
	check(HasAuthority());
	bCanControl = bInCanControl;
	if (!bCanControl) CursorInput = FVector2D::ZeroVector;
	ForceNetUpdate();
}

void ARadarOperatorLink::PushSnapshot(const FRadarOperatorSnapshot& NewSnapshot)
{
	if (!HasAuthority()) return;
	Snapshot = NewSnapshot;
	ForceNetUpdate();
}

void ARadarOperatorLink::PushEvent(const FRadarOperatorEvent& Event, int32 MinimumSnapshotRevision)
{
	if (HasAuthority()) ClientRadarEvent(Event, MinimumSnapshotRevision);
}

void ARadarOperatorLink::OnRep_Radar()
{
	if (Radar && !HasAuthority())
	{
		Radar->RegisterLocalOperatorLink(this);
		OnRep_Snapshot();
		FlushPendingResults();
	}
}

void ARadarOperatorLink::OnRep_Snapshot()
{
	if (Radar && Snapshot.Revision > AppliedRevision && !HasAuthority())
	{
		AppliedRevision = Snapshot.Revision;
		Radar->ApplyOperatorSnapshot(Snapshot);
	}
	FlushPendingEvents();
}

void ARadarOperatorLink::ClientRadarEvent_Implementation(FRadarOperatorEvent Event,
	int32 MinimumSnapshotRevision)
{
	if (HasAuthority()) return; // The server already dispatched this locally.
	FPendingEvent Pending;
	Pending.Event = MoveTemp(Event);
	Pending.MinimumSnapshotRevision = MinimumSnapshotRevision;
	PendingEvents.Add(MoveTemp(Pending));
	FlushPendingEvents();
}

void ARadarOperatorLink::FlushPendingEvents()
{
	if (!Radar || HasAuthority()) return;
	while (!PendingEvents.IsEmpty() && AppliedRevision >= PendingEvents[0].MinimumSnapshotRevision)
	{
		Radar->ApplyOperatorEvent(PendingEvents[0].Event);
		PendingEvents.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

int32 ARadarOperatorLink::SubmitCommand(ERadarCommandType Command, int32 IntValue, float ValueA,
	float ValueB, const FVector& WorldValue, const FRadarCursorState& Cursor, ERadarDisplayGeometry Geometry,
	int32 ViewRevision)
{
	const int32 RequestID = NextRequestID++;
	if (HasAuthority())
	{
		ServerSubmitCommand_Implementation(RequestID, Command, IntValue, ValueA, ValueB, WorldValue, Cursor, Geometry, ViewRevision);
	}
	else
	{
		ServerSubmitCommand(RequestID, Command, IntValue, ValueA, ValueB, WorldValue, Cursor, Geometry, ViewRevision);
	}
	return RequestID;
}

void ARadarOperatorLink::SubmitCursorInput(float X, float Y)
{
	if (!Radar || !Radar->IsTargetCursorEnabled()) return;
	const FVector2D Input(FMath::Clamp(X, -1.0f, 1.0f), FMath::Clamp(Y, -1.0f, 1.0f));
	const int32 ViewRevision = Radar->GetDisplayViewRevision();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (!HasAuthority() && Input.Equals(LastSentCursorInput) &&
		ViewRevision == LastSentCursorInputViewRevision && Now - LastSentCursorInputTime < 0.05f)
		return;
	LastSentCursorInput = Input;
	LastSentCursorInputTime = Now;
	LastSentCursorInputViewRevision = ViewRevision;
	if (HasAuthority()) ServerSubmitCursorInput_Implementation(Input.X, Input.Y, ViewRevision);
	else ServerSubmitCursorInput(Input.X, Input.Y, ViewRevision);
}

FVector2D ARadarOperatorLink::GetActiveCursorInput(float WorldTime) const
{
	return Radar && Radar->IsTargetCursorEnabled() && bCanControl &&
		LastCursorInputViewRevision == Radar->GetDisplayViewRevision() &&
		WorldTime - LastCursorInputTime <= 0.2f ? CursorInput : FVector2D::ZeroVector;
}

void ARadarOperatorLink::ServerSubmitCursorInput_Implementation(float X, float Y, int32 ViewRevision)
{
	if (!Radar || !Radar->IsTargetCursorEnabled() || !bCanControl || !Radar->IsAuthorizedOperatorLink(this)) return;
	if (ViewRevision != Radar->GetDisplayViewRevision()) return;
	CursorInput = FVector2D(FMath::Clamp(X, -1.0f, 1.0f), FMath::Clamp(Y, -1.0f, 1.0f));
	LastCursorInputTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	LastCursorInputViewRevision = ViewRevision;
}

void ARadarOperatorLink::ServerSubmitCommand_Implementation(int32 RequestID, ERadarCommandType Command,
	int32 IntValue, float ValueA, float ValueB, FVector WorldValue, FRadarCursorState Cursor,
	ERadarDisplayGeometry Geometry, int32 ViewRevision)
{
	int32 TrackID = -1;
	const bool bViewMatches = Radar &&
		((Command != ERadarCommandType::SetCursor && Command != ERadarCommandType::DesignateCursor) ||
			ViewRevision == Radar->GetDisplayViewRevision());
	const bool bSuccess = Radar && bCanControl && Radar->IsAuthorizedOperatorLink(this) &&
		bViewMatches &&
		Radar->ExecuteOperatorCommand(Command, IntValue, ValueA, ValueB, WorldValue, Cursor, Geometry,
			TrackID, ViewRevision);
	if (Radar) Radar->OnRadarCommandResult.Broadcast(RequestID, Command, bSuccess, TrackID);
	ClientCommandResult(RequestID, Command, bSuccess, TrackID);
}

void ARadarOperatorLink::ClientCommandResult_Implementation(int32 RequestID, ERadarCommandType Command,
	bool bSuccess, int32 TrackID)
{
	if (HasAuthority()) return; // Already dispatched by the server command handler.
	PendingResults.Add({RequestID, Command, bSuccess, TrackID});
	FlushPendingResults();
}

void ARadarOperatorLink::FlushPendingResults()
{
	if (!Radar || HasAuthority()) return;
	for (const FPendingResult& Result : PendingResults)
	{
		Radar->OnRadarCommandResult.Broadcast(Result.RequestID, Result.Command,
			Result.bSuccess, Result.TrackID);
	}
	PendingResults.Reset();
}
