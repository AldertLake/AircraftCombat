// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// -----------------------------------------------------

#include "AircraftRadarComponent.h"
#include "RadarOperatorLink.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

bool UAircraftRadarComponent::ExecuteOrSubmitCommand(ERadarCommandType Command, int32 IntValue,
	float ValueA, float ValueB, const FVector& WorldValue, const FRadarCursorState& Cursor,
	ERadarDisplayGeometry Geometry, bool bAuthoritative, APlayerController* Controller)
{
	const bool bHasAuthority = !GetOwner() || GetOwner()->HasAuthority();
	const bool bIsGameWorld = GetWorld() && GetWorld()->IsGameWorld();

	if (!bIsGameWorld || (bHasAuthority && bAuthoritative))
	{
		int32 OutTrackID = -1;
		const bool bSuccess = ExecuteOperatorCommand(Command, IntValue, ValueA, ValueB, WorldValue, Cursor, Geometry, OutTrackID, DisplayViewRevision);
		if (bHasAuthority && bIsGameWorld && bSuccess)
		{
			PublishOperatorSnapshot();
		}
		return bSuccess;
	}

	if (bHasAuthority)
	{
		if (!Controller || ActiveRadarOperator.Get() != Controller)
		{
			return false;
		}
		int32 OutTrackID = -1;
		const bool bSuccess = ExecuteOperatorCommand(Command, IntValue, ValueA, ValueB, WorldValue, Cursor, Geometry, OutTrackID, DisplayViewRevision);
		OnRadarCommandResult.Broadcast(0, Command, bSuccess, OutTrackID);
		if (bSuccess)
		{
			PublishOperatorSnapshot();
		}
		return bSuccess;
	}

	if (ARadarOperatorLink* Link = LocalOperatorLink.Get())
	{
		if (Controller && Link->GetOwner() != Controller)
		{
			return false;
		}
		const int32 RequestID = Link->SubmitCommand(Command, IntValue, ValueA, ValueB, WorldValue, Cursor, Geometry, DisplayViewRevision);
		return RequestID != INDEX_NONE;
	}

	return false;
}

bool UAircraftRadarComponent::SetRadarMode(ERadarOperatingMode NewMode, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::SetMode, static_cast<int32>(NewMode), 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::SetDisplayRange(float NewRangeCm, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::SetRange, 0, NewRangeCm, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::CycleRangeScale(bool bIncrease, bool bAuthoritative, APlayerController* Controller)
{
	if (RangeScalePresets.IsEmpty()) return false;
	int32 CurrentIdx = 0;
	float BestDiff = TNumericLimits<float>::Max();
	for (int32 i = 0; i < RangeScalePresets.Num(); ++i)
	{
		const float Diff = FMath::Abs(RangeScalePresets[i] - CurrentDisplayRange);
		if (Diff < BestDiff)
		{
			BestDiff = Diff;
			CurrentIdx = i;
		}
	}
	const int32 Count = RangeScalePresets.Num();
	const int32 NextIdx = bIncrease ? (CurrentIdx + 1) % Count : (CurrentIdx - 1 + Count) % Count;
	return SetDisplayRange(RangeScalePresets[NextIdx], bAuthoritative, Controller);
}

bool UAircraftRadarComponent::SetACMSubMode(ERadarACMSubMode NewSubMode, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::SetACMMode, static_cast<int32>(NewSubMode), 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::SetScanVolume(float InAzimuthWidth, float InElevationHeight, int32 InBars, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::SetScanVolume, InBars, InAzimuthWidth, InElevationHeight,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::ApplyScanSizePreset(ERadarScanSize Preset, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::ApplyScanPreset, static_cast<int32>(Preset), 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::OffsetScanCenter(float AzimuthDelta, float ElevationDelta, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::OffsetScanCenter, 0, AzimuthDelta, ElevationDelta,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::DesignateSpotlightPoint(const FVector& WorldLocation, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::DesignateSpotlight, 0, 0.0f, 0.0f,
		WorldLocation, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::DesignateSpotlightActor(AActor* Actor, bool bAuthoritative, APlayerController* Controller)
{
	if (!IsValid(Actor)) return false;
	return DesignateSpotlightPoint(Actor->GetActorLocation(), bAuthoritative, Controller);
}

bool UAircraftRadarComponent::ClearSpotlightTarget(bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::ClearSpotlight, 0, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::SetHelmetLookDirection(const FVector& WorldDirection, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::HelmetCue, 0, 0.0f, 0.0f,
		WorldDirection.GetSafeNormal(), FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::LockTrack(int32 TrackID, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::LockTrack, TrackID, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::LockActor(AActor* TargetActor, bool bAuthoritative, APlayerController* Controller)
{
	if (!IsValid(TargetActor)) return false;
	FRadarTrack Track;
	if (GetTrackByActor(TargetActor, Track))
	{
		return LockTrack(Track.TrackID, bAuthoritative, Controller);
	}
	if (bAuthoritative)
	{
		return AcquireOrLockActor(TargetActor, true);
	}
	return false;
}

bool UAircraftRadarComponent::BugTrack(int32 TrackID, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::BugTrack, TrackID, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::BreakLock(bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::BreakLock, 0, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::ClearBugTrack(bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::ClearBug, 0, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::MoveTDCCursor(FVector2D DeltaAxis, bool bAuthoritative, APlayerController* Controller)
{
	if (!bEnableTargetCursor) return false;
	if (!FMath::IsFinite(DeltaAxis.X) || !FMath::IsFinite(DeltaAxis.Y)) return false;

	const bool bHasAuthority = !GetOwner() || GetOwner()->HasAuthority();
	const bool bIsGameWorld = GetWorld() && GetWorld()->IsGameWorld();

	if (!bIsGameWorld || (bHasAuthority && bAuthoritative))
	{
		return ExecuteAuthoritativeMoveTDCCursor(DeltaAxis);
	}

	if (bHasAuthority)
	{
		if (!Controller || ActiveRadarOperator.Get() != Controller) return false;
		return ExecuteAuthoritativeMoveTDCCursor(DeltaAxis);
	}

	if (ARadarOperatorLink* Link = LocalOperatorLink.Get())
	{
		if (!Link->CanControl() || (Controller && Link->GetOwner() != Controller)) return false;
		Link->SubmitCursorInput(DeltaAxis.X, DeltaAxis.Y);
		return true;
	}

	return false;
}

bool UAircraftRadarComponent::SetTDCCursorFromDisplayPosition(const FVector2D& DisplayPosition, const FVector2D& WidgetSize, bool bAuthoritative, APlayerController* Controller)
{
	if (!bEnableTargetCursor) return false;
	const FRadarDisplayProjection Projection = MakeDisplayProjection(FVector2D::ZeroVector, WidgetSize,
		DisplayView.ActiveGeometry, DisplayView.bHeadingUp);
	FRadarCursorState Cursor;
	if (!ResolveDisplayPointToCursor(DisplayPosition + WidgetSize * 0.5f, Projection, Cursor)) return false;
	return ExecuteOrSubmitCommand(ERadarCommandType::SetCursor, 0, 0.0f, 0.0f,
		FVector::ZeroVector, Cursor, DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::DesignateUnderCursor(bool bAuthoritative, APlayerController* Controller)
{
	if (!bEnableTargetCursor) return false;
	return ExecuteOrSubmitCommand(ERadarCommandType::DesignateCursor, 0, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::SetDisplayGeometry(ERadarDisplayGeometry Geometry, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::SetDisplayGeometry, static_cast<int32>(Geometry), 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), Geometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::SetDisplayHeadingUp(bool bHeadingUp, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::SetDisplayHeadingUp, bHeadingUp ? 1 : 0, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::DesignateLinkedTrack(int32 TrackID, bool bAuthoritative, APlayerController* Controller)
{
	return ExecuteOrSubmitCommand(ERadarCommandType::DesignateLinkedTrack, TrackID, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, bAuthoritative, Controller);
}

bool UAircraftRadarComponent::GrantRadarAccess(APlayerController* Controller)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Controller) || !GetWorld()) return false;
	for (ARadarOperatorLink* Link : OperatorLinks)
	{
		if (IsValid(Link) && Link->GetOwner() == Controller) return true;
	}
	FActorSpawnParameters Parameters;
	Parameters.Owner = Controller;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARadarOperatorLink* Link = GetWorld()->SpawnActor<ARadarOperatorLink>(ARadarOperatorLink::StaticClass(), Parameters);
	if (!Link) return false;
	Link->Initialize(this);
	OperatorLinks.Add(Link);
	if (!ActiveRadarOperator.IsValid()) SetActiveRadarOperator(Controller);
	PublishOperatorSnapshot();
	return true;
}

void UAircraftRadarComponent::RevokeRadarAccess(APlayerController* Controller)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Controller) return;
	for (int32 Index = OperatorLinks.Num() - 1; Index >= 0; --Index)
	{
		ARadarOperatorLink* Link = OperatorLinks[Index];
		if (!IsValid(Link) || Link->GetOwner() == Controller)
		{
			OperatorLinks.RemoveAtSwap(Index);
			if (IsValid(Link)) Link->Destroy();
		}
	}
	if (ActiveRadarOperator.Get() == Controller)
	{
		ActiveRadarOperator.Reset();
		for (ARadarOperatorLink* Link : OperatorLinks)
		{
			if (IsValid(Link) && Cast<APlayerController>(Link->GetOwner()))
			{
				SetActiveRadarOperator(Cast<APlayerController>(Link->GetOwner()));
				break;
			}
		}
	}
}

bool UAircraftRadarComponent::SetActiveRadarOperator(APlayerController* Controller)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Controller) return false;
	bool bFound = false;
	for (ARadarOperatorLink* Link : OperatorLinks)
	{
		if (IsValid(Link) && Link->GetOwner() == Controller) bFound = true;
	}
	if (!bFound) return false;
	ActiveRadarOperator = Controller;
	for (ARadarOperatorLink* Link : OperatorLinks)
	{
		if (IsValid(Link)) Link->SetCanControl(Link->GetOwner() == Controller);
	}
	return true;
}

bool UAircraftRadarComponent::IsAuthorizedOperatorLink(const ARadarOperatorLink* Link) const
{
	return GetOwner() && GetOwner()->HasAuthority() && IsValid(Link) &&
		OperatorLinks.ContainsByPredicate([Link](const TObjectPtr<ARadarOperatorLink>& Entry)
		{
			return Entry.Get() == Link;
		}) && Link->GetOwner() == ActiveRadarOperator.Get();
}

void UAircraftRadarComponent::RecordOperatorEvent(ERadarOperatorEventType Type, int32 TrackID,
	bool bSuccess, const FRadarTrack* Track, const FVector& Location)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	FRadarOperatorEvent Event;
	Event.Sequence = ++OperatorEventSequence;
	Event.Type = Type;
	Event.TrackID = TrackID;
	Event.bSuccess = bSuccess;
	if (Track) Event.Track = *Track;
	Event.Location = Location;
	const int32 MinimumSnapshotRevision = OperatorSnapshotRevision + 1;
	for (ARadarOperatorLink* Link : OperatorLinks)
	{
		if (IsValid(Link)) Link->PushEvent(Event, MinimumSnapshotRevision);
	}
}

void UAircraftRadarComponent::RefreshAutomaticOperator()
{
	APlayerController* PossessingController = nullptr;
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		PossessingController = Cast<APlayerController>(Pawn->GetController());
	}
	else if (GetOwner())
	{
		PossessingController = Cast<APlayerController>(GetOwner()->GetOwner());
	}
	if (AutoAuthorizedController.Get() == PossessingController)
	{
		if (!ActiveRadarOperator.IsValid())
		{
			if (PossessingController) SetActiveRadarOperator(PossessingController);
			if (!ActiveRadarOperator.IsValid())
			{
				for (ARadarOperatorLink* Link : OperatorLinks)
				{
					if (IsValid(Link) && IsValid(Link->GetOwner()) &&
						SetActiveRadarOperator(Cast<APlayerController>(Link->GetOwner()))) break;
				}
			}
		}
		return;
	}
	APlayerController* Previous = AutoAuthorizedController.Get();
	const bool bWasActive = Previous && ActiveRadarOperator.Get() == Previous;
	AutoAuthorizedController = PossessingController;
	if (Previous) RevokeRadarAccess(Previous);
	if (PossessingController)
	{
		GrantRadarAccess(PossessingController);
		if (bWasActive || !ActiveRadarOperator.IsValid()) SetActiveRadarOperator(PossessingController);
	}
}

void UAircraftRadarComponent::PublishOperatorSnapshot()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || OperatorLinks.IsEmpty()) return;
	FRadarOperatorSnapshot Snapshot;
	Snapshot.Revision = ++OperatorSnapshotRevision;
	Snapshot.Mode = RadarMode;
	Snapshot.ACMMode = ACMSubMode;
	Snapshot.DisplayRangeCm = CurrentDisplayRange;
	Snapshot.DisplayView = DisplayView;
	Snapshot.DisplayViewRevision = DisplayViewRevision;
	Snapshot.ScanAzimuth = CurrentScanAzimuth;
	Snapshot.ScanElevation = CurrentScanElevation;
	Snapshot.bVirtuallySweepBeam = bVirtuallySweepBeam;
	Snapshot.SampleBeams = ActiveSampleBeams;
	Snapshot.ScanCenterAzimuth = ScanCenterAzimuth;
	Snapshot.ScanCenterElevation = ScanCenterElevation;
	Snapshot.AzimuthScanWidth = AzimuthScanWidth;
	Snapshot.ElevationScanHeight = ElevationScanHeight;
	Snapshot.ElevationBars = ElevationBars;
	Snapshot.ScanDrive = ScanDrive;
	Snapshot.ScanSizePreset = ScanSizePreset;
	Snapshot.ScanBar = CurrentScanBar;
	Snapshot.ScanProgressRevision = ScanProgressRevision;
	Snapshot.ScanBarLevel = ScanBarLevel;
	Snapshot.ScanSweepLevel = ScanSweepLevel;
	Snapshot.bScanningRight = bScanningRight;
	Snapshot.SweepCounter = ScanSweepCounter;
	Snapshot.SARImageRevision = SARImageRevision;
	Snapshot.LockedTrackID = STTLockedTrackID;
	Snapshot.BuggedTrackID = BuggedTrackID;
	Snapshot.Cursor = GetTDCCursorState();
	Snapshot.bTargetCursorEnabled = bEnableTargetCursor;
	Snapshot.Tracks = Tracks;
	// Send raw donated reports even while a local return is driving the symbol.
	// The owner recomputes display correlation from both independent track sets.
	Snapshot.DisplayTracks = LinkedTracks;
	BuildLaunchedRadarMissileStatuses(Snapshot.LaunchedRadarMissiles);
	for (FRadarTrack& DisplayTrack : Snapshot.DisplayTracks)
	{
		if (DisplayTrack.Source != ERadarTrackSource::Local) DisplayTrack.TrackedActor.Reset();
	}
	Snapshot.SelectedLinkedTrackID = SelectedLinkedTrackID;
	Snapshot.SelectedContactID = SelectedContactID;
	Snapshot.bDataLinkConnected = IsDataLinkConnected();
	Snapshot.DataLinkNetworkID = DataLinkNetworkID;
	Snapshot.DataLinkParticipantID = DataLinkParticipantID;
	Snapshot.SpotlightPoint = SpotlightTargetLocation;
	Snapshot.bHasSpotlightPoint = bHasSpotlightPoint;
	Snapshot.RadarAltitudeCm = CachedRadarAltitude;
	Snapshot.bRadarAltitudeValid = bIsRadarAltitudeValid;
	for (int32 Index = OperatorLinks.Num() - 1; Index >= 0; --Index)
	{
		if (IsValid(OperatorLinks[Index])) OperatorLinks[Index]->PushSnapshot(Snapshot);
		else OperatorLinks.RemoveAtSwap(Index);
	}
	OnRadarDisplayStateUpdated.Broadcast(Snapshot.Revision);
}

void UAircraftRadarComponent::RegisterLocalOperatorLink(ARadarOperatorLink* Link)
{
	if (Link && Link->GetRadar() == this) LocalOperatorLink = Link;
}

void UAircraftRadarComponent::UnregisterLocalOperatorLink(ARadarOperatorLink* Link)
{
	if (LocalOperatorLink.Get() != Link) return;
	LocalOperatorLink.Reset();
	ClientLaunchedRadarMissiles.Reset();
}

void UAircraftRadarComponent::ApplyOperatorSnapshot(const FRadarOperatorSnapshot& Snapshot)
{
	if (GetOwner() && GetOwner()->HasAuthority()) return;
	if (bHasAppliedOperatorSnapshot && Snapshot.Revision <= OperatorSnapshotRevision) return;
	const TArray<FRadarTrack> OldTracks = MoveTemp(Tracks);
	const TArray<FRadarTrack> OldLinkedTracks = MoveTemp(LinkedTracks);
	const ERadarOperatingMode OldMode = LastAppliedOperatorMode;
	const float OldRange = CurrentDisplayRange;
	const int32 OldLock = STTLockedTrackID;
	const int32 OldBug = BuggedTrackID;
	const int32 OldSweeps = ScanSweepCounter;
	const uint32 OldScanProgressRevision = ScanProgressRevision;
	RadarMode = Snapshot.Mode;
	ACMSubMode = Snapshot.ACMMode;
	CurrentDisplayRange = Snapshot.DisplayRangeCm;
	DisplayView = Snapshot.DisplayView;
	DisplayViewRevision = Snapshot.DisplayViewRevision;
	CurrentScanAzimuth = Snapshot.ScanAzimuth;
	CurrentScanElevation = Snapshot.ScanElevation;
	bVirtuallySweepBeam = Snapshot.bVirtuallySweepBeam;
	ActiveSampleBeams = Snapshot.SampleBeams;
	ScanCenterAzimuth = Snapshot.ScanCenterAzimuth;
	ScanCenterElevation = Snapshot.ScanCenterElevation;
	AzimuthScanWidth = Snapshot.AzimuthScanWidth;
	ElevationScanHeight = Snapshot.ElevationScanHeight;
	ElevationBars = Snapshot.ElevationBars;
	ScanDrive = Snapshot.ScanDrive;
	ScanSizePreset = Snapshot.ScanSizePreset;
	CurrentScanBar = Snapshot.ScanBar;
	ScanProgressRevision = Snapshot.ScanProgressRevision;
	ScanBarLevel = Snapshot.ScanBarLevel;
	ScanSweepLevel = Snapshot.ScanSweepLevel;
	bScanningRight = Snapshot.bScanningRight;
	ScanSweepCounter = Snapshot.SweepCounter;
	SARImageRevision = Snapshot.SARImageRevision;
	STTLockedTrackID = Snapshot.LockedTrackID;
	BuggedTrackID = Snapshot.BuggedTrackID;
	bEnableTargetCursor = Snapshot.bTargetCursorEnabled;
	TDCCursorAzimuth = Snapshot.Cursor.AzimuthDegrees;
	TDCCursorElevation = Snapshot.Cursor.ElevationDegrees;
	TDCCursorRange = Snapshot.Cursor.SlantRangeCm;
	SpotlightTargetLocation = Snapshot.SpotlightPoint;
	bHasSpotlightPoint = Snapshot.bHasSpotlightPoint;
	CachedRadarAltitude = Snapshot.RadarAltitudeCm;
	bIsRadarAltitudeValid = Snapshot.bRadarAltitudeValid;
	Tracks = Snapshot.Tracks;
	ClientLaunchedRadarMissiles = Snapshot.LaunchedRadarMissiles;
	LinkedTracks.Reset();
	for (const FRadarTrack& DisplayTrack : Snapshot.DisplayTracks)
	{
		if (DisplayTrack.Source != ERadarTrackSource::Local) LinkedTracks.Add(DisplayTrack);
	}
	SelectedLinkedTrackID = Snapshot.SelectedLinkedTrackID;
	SelectedContactID = Snapshot.SelectedContactID;
	bClientDataLinkConnected = Snapshot.bDataLinkConnected;
	DataLinkNetworkID = Snapshot.DataLinkNetworkID;
	DataLinkParticipantID = Snapshot.DataLinkParticipantID;
	STTLockedActor = nullptr;
	if (const FRadarTrack* Locked = Tracks.FindByPredicate([this](const FRadarTrack& Track)
		{ return Track.TrackID == STTLockedTrackID; })) STTLockedActor = Locked->TrackedActor.Get();
	OperatorSnapshotRevision = Snapshot.Revision;
	LastAppliedOperatorMode = Snapshot.Mode;
	const bool bHasVolumeScanProgress = RadarMode == ERadarOperatingMode::Search ||
		RadarMode == ERadarOperatingMode::TrackWhileScan ||
		RadarMode == ERadarOperatingMode::GroundMapping ||
		RadarMode == ERadarOperatingMode::SeaSearch;

	if (!bHasAppliedOperatorSnapshot)
	{
		bHasAppliedOperatorSnapshot = true;
		OnRadarSnapshotReady.Broadcast();
		if (bEnableTargetCursor) OnRadarCursorMoved.Broadcast(GetTDCCursorState());
		if (bHasVolumeScanProgress)
			OnScanProgressUpdated.Broadcast(CurrentScanBar, ScanBarLevel, ScanSweepLevel, bScanningRight);
		OnRadarDisplayStateUpdated.Broadcast(Snapshot.Revision);
		return;
	}

	if (OldLock >= 0 && OldLock != STTLockedTrackID)
	{
		if (OldLock != BuggedTrackID) OnRadarTrackDeselected.Broadcast(OldLock);
	}
	if (OldBug >= 0 && OldBug != BuggedTrackID && OldBug != STTLockedTrackID)
		OnRadarTrackDeselected.Broadcast(OldBug);
	for (const FRadarTrack& OldTrack : OldTracks)
	{
		if (!Tracks.ContainsByPredicate([&OldTrack](const FRadarTrack& Track) { return Track.TrackID == OldTrack.TrackID; }))
		{
			OnRadarContactLost.Broadcast(OldTrack.TrackID);
		}
	}
	if (OldTracks.Num() > 0 && Tracks.IsEmpty()) OnRadarAllContactsCleared.Broadcast();
	for (const FRadarTrack& OldLinked : OldLinkedTracks)
	{
		if (!LinkedTracks.ContainsByPredicate([&OldLinked](const FRadarTrack& Current)
			{ return Current.TrackID == OldLinked.TrackID; })) OnRadarContactLost.Broadcast(OldLinked.TrackID);
	}
	for (const FRadarTrack& Linked : LinkedTracks)
	{
		const FRadarTrack* Previous = OldLinkedTracks.FindByPredicate([&Linked](const FRadarTrack& Old)
			{ return Old.TrackID == Linked.TrackID; });
		if (!Previous) OnRadarContactNew.Broadcast(Linked);
		else if (Previous->LastKnownPosition != Linked.LastKnownPosition ||
			Previous->TrackAge != Linked.TrackAge || Previous->Status != Linked.Status)
			OnRadarContactUpdated.Broadcast(Linked);
	}
	for (const FRadarTrack& Track : Tracks)
	{
		const FRadarTrack* Previous = OldTracks.FindByPredicate([&Track](const FRadarTrack& Old)
		{
			return Old.TrackID == Track.TrackID;
		});
		if (!Previous) OnRadarContactNew.Broadcast(Track);
		else if (Previous->LastKnownPosition != Track.LastKnownPosition ||
			Previous->EstimatedVelocity != Track.EstimatedVelocity ||
			Previous->SignalStrength != Track.SignalStrength ||
			Previous->bIsNotching != Track.bIsNotching ||
			Previous->Status != Track.Status) OnRadarContactUpdated.Broadcast(Track);
		if (Track.bIsNotching && (!Previous || !Previous->bIsNotching)) OnRadarTargetNotching.Broadcast(Track);
	}
	if (OldMode != RadarMode) OnRadarModeChanged.Broadcast(RadarMode);
	if (!FMath::IsNearlyEqual(OldRange, CurrentDisplayRange)) OnRadarDisplayRangeChanged.Broadcast(CurrentDisplayRange);
	if (OldLock != STTLockedTrackID && STTLockedTrackID >= 0)
	{
		if (const FRadarTrack* Locked = Tracks.FindByPredicate([this](const FRadarTrack& Track)
			{ return Track.TrackID == STTLockedTrackID; }))
		{
			OnRadarTrackSelected.Broadcast(*Locked);
		}
	}
	else if (OldBug != BuggedTrackID && BuggedTrackID >= 0)
	{
		if (const FRadarTrack* Bugged = Tracks.FindByPredicate([this](const FRadarTrack& Track)
			{ return Track.TrackID == BuggedTrackID; })) OnRadarTrackSelected.Broadcast(*Bugged);
	}
	if (bEnableTargetCursor) OnRadarCursorMoved.Broadcast(GetTDCCursorState());
	if (ScanSweepCounter > OldSweeps) OnScanSweepComplete.Broadcast();
	if (bHasVolumeScanProgress && OldScanProgressRevision != ScanProgressRevision)
		OnScanProgressUpdated.Broadcast(CurrentScanBar, ScanBarLevel, ScanSweepLevel, bScanningRight);
	OnRadarDisplayStateUpdated.Broadcast(Snapshot.Revision);
}

void UAircraftRadarComponent::ApplyOperatorEvent(const FRadarOperatorEvent& Event)
{
	if (GetOwner() && GetOwner()->HasAuthority()) return;
	if (Event.Sequence <= LastAppliedOperatorEventSequence) return;
	LastAppliedOperatorEventSequence = Event.Sequence;
	switch (Event.Type)
	{
	case ERadarOperatorEventType::LockAcquired: OnRadarLockAcquired.Broadcast(Event.Track); break;
	case ERadarOperatorEventType::LockLost: OnRadarLockLost.Broadcast(Event.TrackID); break;
	case ERadarOperatorEventType::CursorDesignated:
		if (bEnableTargetCursor) OnRadarCursorDesignated.Broadcast(Event.bSuccess, Event.TrackID);
		break;
	case ERadarOperatorEventType::SARReady: OnSARImageReady.Broadcast(Event.Location); break;
	default: break;
	}
}

bool UAircraftRadarComponent::ExecuteOperatorCommand(ERadarCommandType Command, int32 IntValue,
	float ValueA, float ValueB, const FVector& WorldValue, const FRadarCursorState& Cursor,
	ERadarDisplayGeometry Geometry, int32& OutTrackID, int32 ViewRevision)
{
	OutTrackID = -1;
	if (!GetOwner() || !GetOwner()->HasAuthority() || !FMath::IsFinite(ValueA) ||
		!FMath::IsFinite(ValueB) || !FMath::IsFinite(WorldValue.X) ||
		!FMath::IsFinite(WorldValue.Y) || !FMath::IsFinite(WorldValue.Z)) return false;
	if ((Command == ERadarCommandType::DesignateCursor || Command == ERadarCommandType::SetCursor) &&
		ViewRevision >= 0 && ViewRevision != DisplayViewRevision) return false;
	switch (Command)
	{

	case ERadarCommandType::SetDisplayGeometry:
		if (IntValue < 0 || IntValue > static_cast<int32>(ERadarDisplayGeometry::PPI)) return false;
		if (DisplayView.ActiveGeometry != static_cast<ERadarDisplayGeometry>(IntValue))
		{
			DisplayView.ActiveGeometry = static_cast<ERadarDisplayGeometry>(IntValue);
			++DisplayViewRevision;
		}
		return true;
	case ERadarCommandType::SetDisplayHeadingUp:
		if (IntValue != 0 && IntValue != 1) return false;
		if (DisplayView.bHeadingUp != (IntValue != 0))
		{
			DisplayView.bHeadingUp = IntValue != 0;
			++DisplayViewRevision;
		}
		return true;
	case ERadarCommandType::SetMode:
		if (IntValue < 0 || IntValue > static_cast<int32>(ERadarOperatingMode::Spotlight)) return false;
		return ExecuteAuthoritativeSetRadarMode(static_cast<ERadarOperatingMode>(IntValue));
	case ERadarCommandType::SetRange:
		if (ValueA < 1000.0f || ValueA > 100000000.0f) return false;
		return ExecuteAuthoritativeSetDisplayRange(ValueA);
	case ERadarCommandType::SetACMMode:
		if (IntValue < 0 || IntValue > static_cast<int32>(ERadarACMSubMode::SlewAcquisition)) return false;
		return ExecuteAuthoritativeSetACMSubMode(static_cast<ERadarACMSubMode>(IntValue));
	case ERadarCommandType::LockTrack:
		OutTrackID = IntValue; return ExecuteAuthoritativeLockTrack(IntValue);
	case ERadarCommandType::BugTrack:
		OutTrackID = IntValue; return ExecuteAuthoritativeBugTrack(IntValue);
	case ERadarCommandType::DesignateLinkedTrack:
		OutTrackID = IntValue; return ExecuteAuthoritativeDesignateLinkedTrack(IntValue);
	case ERadarCommandType::BreakLock:
		if (!IsSTTLocked()) return false;
		return ExecuteAuthoritativeBreakLock();
	case ERadarCommandType::ClearBug:
		if (BuggedTrackID < 0) return false;
		return ExecuteAuthoritativeClearBugTrack();
	case ERadarCommandType::DesignateCursor:
		if (!bEnableTargetCursor) return false;
		if (Geometry != DisplayView.ActiveGeometry) return false;
		if (!ExecuteAuthoritativeDesignateUnderCursor()) return false;
		OutTrackID = SelectedLinkedTrackID < -1 ? SelectedLinkedTrackID :
			(STTLockedTrackID >= 0 ? STTLockedTrackID : BuggedTrackID);
		return true;
	case ERadarCommandType::SetCursor:
	{
		if (!bEnableTargetCursor) return false;
		if (Geometry != DisplayView.ActiveGeometry) return false;
		if (!IsCursorWithinLimits(Cursor)) return false;
		TDCCursorAzimuth = FRotator::NormalizeAxis(Cursor.AzimuthDegrees);
		TDCCursorElevation = Cursor.ElevationDegrees;
		TDCCursorRange = Cursor.SlantRangeCm;
		OnRadarCursorMoved.Broadcast(GetTDCCursorState()); return true;
	}
	case ERadarCommandType::SetScanVolume:
		if (ValueA < 5.0f || ValueA > 360.0f || ValueB < 2.0f || ValueB > 120.0f || IntValue < 1 || IntValue > 8) return false;
		return ExecuteAuthoritativeSetScanVolume(ValueA, ValueB, IntValue);
	case ERadarCommandType::OffsetScanCenter:
		if (FMath::Abs(ValueA) > 30.0f || FMath::Abs(ValueB) > 30.0f) return false;
		return ExecuteAuthoritativeOffsetScanCenter(ValueA, ValueB);
	case ERadarCommandType::DesignateSpotlight:
		if (FVector::DistSquared(GetRadarLocation(), WorldValue) > FMath::Square(MaxDetectionRange)) return false;
		return ExecuteAuthoritativeDesignateSpotlightPoint(WorldValue);
	case ERadarCommandType::ClearSpotlight:
		return ExecuteAuthoritativeClearSpotlightTarget();
	case ERadarCommandType::HelmetCue:
		if (WorldValue.SizeSquared() > 1.1f) return false;
		return ExecuteAuthoritativeSetHelmetLookDirection(WorldValue);
	case ERadarCommandType::ApplyScanPreset:
		if (IntValue < 0 || IntValue > static_cast<int32>(ERadarScanSize::Custom)) return false;
		return ExecuteAuthoritativeApplyScanSizePreset(static_cast<ERadarScanSize>(IntValue));
	default: return false;
	}
}
