// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftRadarComponent.h"
#include "RadarOperatorLink.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

int32 UAircraftRadarComponent::SubmitControlRequest(ERadarCommandType Command, int32 IntValue,
	float ValueA, float ValueB, const FVector& WorldValue, const FRadarCursorState& Cursor,
	ERadarDisplayGeometry Geometry, APlayerController* RequestingController)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		int32 TrackID = -1;
		const bool bAuthorized = IsValid(RequestingController) &&
			ActiveRadarOperator.Get() == RequestingController;
		const bool bSuccess = bAuthorized && ExecuteOperatorCommand(Command, IntValue, ValueA, ValueB,
			WorldValue, Cursor, Geometry, TrackID, DisplayViewRevision);
		OnRadarCommandResult.Broadcast(0, Command, bSuccess, TrackID);
		return 0;
	}
	if (ARadarOperatorLink* Link = LocalOperatorLink.Get())
	{
		if (RequestingController && Link->GetOwner() != RequestingController) return INDEX_NONE;
		return Link->SubmitCommand(Command, IntValue, ValueA, ValueB, WorldValue, Cursor,
			Geometry, DisplayViewRevision);
	}
	return INDEX_NONE;
}

int32 UAircraftRadarComponent::RequestSetRadarMode(ERadarOperatingMode NewMode, APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::SetMode, static_cast<int32>(NewMode), 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope, RequestingController);
}

int32 UAircraftRadarComponent::RequestSetDisplayRange(float NewRangeCm, APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::SetRange, 0, NewRangeCm, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope, RequestingController);
}

int32 UAircraftRadarComponent::RequestLockTrack(int32 TrackID, APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::LockTrack, TrackID, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope, RequestingController);
}

int32 UAircraftRadarComponent::RequestBugTrack(int32 TrackID, APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::BugTrack, TrackID, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope, RequestingController);
}

int32 UAircraftRadarComponent::RequestBreakLock(APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::BreakLock, 0, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope, RequestingController);
}

int32 UAircraftRadarComponent::RequestDesignateCursor(APlayerController* RequestingController)
{
	if (!bEnableTargetCursor) return INDEX_NONE;
	return SubmitControlRequest(ERadarCommandType::DesignateCursor, 0, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry, RequestingController);
}

int32 UAircraftRadarComponent::RequestDesignateUnderDisplayCursor(APlayerController* RequestingController)
{
	return RequestDesignateCursor(RequestingController);
}

int32 UAircraftRadarComponent::RequestSetDisplayWindow(float ZoomFactor,
	FVector2D ViewCenterOffset, APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::SetDisplayWindow, 0,
		ZoomFactor, ViewCenterOffset.X, FVector(ViewCenterOffset.Y, 0.0f, 0.0f),
		FRadarCursorState(), DisplayView.ActiveGeometry, RequestingController);
}

bool UAircraftRadarComponent::SetDisplayWindowAuthoritative(ERadarDisplayGeometry Geometry,
	const FRadarDisplayWindow& Window)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() ||
		(Geometry != ERadarDisplayGeometry::BScope && Geometry != ERadarDisplayGeometry::PPI) ||
		!FMath::IsFinite(Window.ZoomFactor) || !FMath::IsFinite(Window.ViewCenterOffset.X) ||
		!FMath::IsFinite(Window.ViewCenterOffset.Y)) return false;
	FRadarDisplayWindow& Current = Geometry == ERadarDisplayGeometry::BScope
		? DisplayView.BScope : DisplayView.PPI;
	FRadarDisplayWindow Clamped = FRadarDisplayGeometryMath::ClampWindow(Window);
	const float XLimit = Geometry == ERadarDisplayGeometry::PPI
		? CurrentDisplayRange / 100000.0f : AzimuthScanWidth * 0.5f;
	const float YLimit = Geometry == ERadarDisplayGeometry::PPI
		? CurrentDisplayRange / 100000.0f : CurrentDisplayRange / 200000.0f;
	Clamped.ViewCenterOffset.X = FMath::Clamp(Clamped.ViewCenterOffset.X, -XLimit, XLimit);
	Clamped.ViewCenterOffset.Y = FMath::Clamp(Clamped.ViewCenterOffset.Y, -YLimit, YLimit);
	if (!FMath::IsNearlyEqual(Current.ZoomFactor, Clamped.ZoomFactor) ||
		!Current.ViewCenterOffset.Equals(Clamped.ViewCenterOffset))
	{
		Current = Clamped;
		++DisplayViewRevision;
	}
	return true;
}

int32 UAircraftRadarComponent::RequestSetDisplayGeometry(ERadarDisplayGeometry Geometry,
	APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::SetDisplayGeometry, static_cast<int32>(Geometry),
		0.0f, 0.0f, FVector::ZeroVector, FRadarCursorState(), Geometry, RequestingController);
}

int32 UAircraftRadarComponent::RequestSetDisplayHeadingUp(bool bHeadingUp,
	APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::SetDisplayHeadingUp, bHeadingUp ? 1 : 0,
		0.0f, 0.0f, FVector::ZeroVector, FRadarCursorState(), DisplayView.ActiveGeometry,
		RequestingController);
}

int32 UAircraftRadarComponent::RequestSetCursorFromDisplayPosition(FVector2D WidgetPosition,
	FVector2D WidgetSize, APlayerController* RequestingController)
{
	if (!bEnableTargetCursor) return INDEX_NONE;
	const FRadarDisplayProjection Projection = MakeDisplayProjection(FVector2D::ZeroVector, WidgetSize,
		DisplayView.ActiveGeometry, DisplayView.bHeadingUp);
	FRadarCursorState Cursor;
	if (!ResolveDisplayPointToCursor(WidgetPosition + WidgetSize * 0.5f, Projection, Cursor)) return INDEX_NONE;
	return SubmitControlRequest(ERadarCommandType::SetCursor, 0, 0.0f, 0.0f,
		FVector::ZeroVector, Cursor, DisplayView.ActiveGeometry, RequestingController);
}

int32 UAircraftRadarComponent::RequestDesignateLinkedTrack(int32 TrackID, APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::DesignateLinkedTrack, TrackID, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope, RequestingController);
}

int32 UAircraftRadarComponent::RequestSetCursorFromWidgetPosition(const FVector2D& WidgetPosition,
	const FRadarDisplayProjection& Projection, APlayerController* RequestingController)
{
	if (!bEnableTargetCursor) return INDEX_NONE;
	FRadarCursorState Cursor;
	if (!ResolveDisplayPointToCursor(WidgetPosition, Projection, Cursor)) return INDEX_NONE;
	return SubmitControlRequest(ERadarCommandType::SetCursor, 0, 0.0f, 0.0f,
		FVector::ZeroVector, Cursor, Projection.Geometry, RequestingController);
}

int32 UAircraftRadarComponent::RequestSetACMSubMode(ERadarACMSubMode NewSubMode,
	APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::SetACMMode, static_cast<int32>(NewSubMode),
		0.0f, 0.0f, FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope,
		RequestingController);
}

int32 UAircraftRadarComponent::RequestSetScanVolume(float AzimuthWidth, float ElevationHeight,
	int32 Bars, APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::SetScanVolume, Bars, AzimuthWidth, ElevationHeight,
		FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope, RequestingController);
}

int32 UAircraftRadarComponent::RequestOffsetScanCenter(float AzimuthDelta, float ElevationDelta,
	APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::OffsetScanCenter, 0, AzimuthDelta, ElevationDelta,
		FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope, RequestingController);
}

int32 UAircraftRadarComponent::RequestApplyScanSizePreset(ERadarScanSize Preset,
	APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::ApplyScanPreset, static_cast<int32>(Preset),
		0.0f, 0.0f, FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope,
		RequestingController);
}

int32 UAircraftRadarComponent::RequestClearBugTrack(APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::ClearBug, 0, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope,
		RequestingController);
}

int32 UAircraftRadarComponent::RequestDesignateSpotlightPoint(const FVector& WorldLocation,
	APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::DesignateSpotlight, 0, 0.0f, 0.0f,
		WorldLocation, FRadarCursorState(), ERadarDisplayGeometry::BScope, RequestingController);
}

int32 UAircraftRadarComponent::RequestClearSpotlightTarget(APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::ClearSpotlight, 0, 0.0f, 0.0f,
		FVector::ZeroVector, FRadarCursorState(), ERadarDisplayGeometry::BScope,
		RequestingController);
}

int32 UAircraftRadarComponent::RequestSetHelmetLookDirection(const FVector& WorldDirection,
	APlayerController* RequestingController)
{
	return SubmitControlRequest(ERadarCommandType::HelmetCue, 0, 0.0f, 0.0f,
		WorldDirection, FRadarCursorState(), ERadarDisplayGeometry::BScope, RequestingController);
}

bool UAircraftRadarComponent::RequestMoveTDCCursor(float XAxis, float YAxis,
	APlayerController* RequestingController)
{
	if (!bEnableTargetCursor) return false;
	if (!FMath::IsFinite(XAxis) || !FMath::IsFinite(YAxis)) return false;
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (!IsValid(RequestingController) || ActiveRadarOperator.Get() != RequestingController)
			return false;
		MoveTDCCursor(XAxis, YAxis);
		return true;
	}
	if (ARadarOperatorLink* Link = LocalOperatorLink.Get())
	{
		if (!Link->CanControl() || (RequestingController && Link->GetOwner() != RequestingController))
			return false;
		Link->SubmitCursorInput(XAxis, YAxis);
		return true;
	}
	return false;
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
	case ERadarCommandType::SetDisplayWindow:
		if (ValueA < 1.0f || ValueA > 16.0f) return false;
		{
			FRadarDisplayWindow Window;
			Window.ZoomFactor = ValueA;
			Window.ViewCenterOffset = FVector2D(ValueB, WorldValue.X);
			return SetDisplayWindowAuthoritative(DisplayView.ActiveGeometry, Window);
		}
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
		SetRadarMode(static_cast<ERadarOperatingMode>(IntValue)); return true;
	case ERadarCommandType::SetRange:
		if (ValueA < 1000.0f || ValueA > 100000000.0f) return false;
		SetRangeScale(ValueA); return true;
	case ERadarCommandType::SetACMMode:
		if (IntValue < 0 || IntValue > static_cast<int32>(ERadarACMSubMode::SlewAcquisition)) return false;
		SetACMSubMode(static_cast<ERadarACMSubMode>(IntValue)); return true;
	case ERadarCommandType::LockTrack:
		OutTrackID = IntValue; return CommandLock(IntValue);
	case ERadarCommandType::BugTrack:
		OutTrackID = IntValue; return CommandBugTrack(IntValue);
	case ERadarCommandType::DesignateLinkedTrack:
		OutTrackID = IntValue; return DesignateLinkedTrack(IntValue);
	case ERadarCommandType::BreakLock:
		if (!IsSTTLocked()) return false;
		BreakLock(); return true;
	case ERadarCommandType::ClearBug:
		if (BuggedTrackID < 0) return false;
		ClearBugTrack(); return true;
	case ERadarCommandType::DesignateCursor:
		if (!bEnableTargetCursor) return false;
		if (Geometry != DisplayView.ActiveGeometry) return false;
		if (!DesignateTrackUnderCursorInDisplay(Geometry, CursorSelectionRadiusFraction)) return false;
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
		SetScanVolume(ValueA, ValueB, IntValue); return true;
	case ERadarCommandType::OffsetScanCenter:
		if (FMath::Abs(ValueA) > 30.0f || FMath::Abs(ValueB) > 30.0f) return false;
		OffsetScanCenter(ValueA, ValueB); return true;
	case ERadarCommandType::DesignateSpotlight:
		if (FVector::DistSquared(GetRadarLocation(), WorldValue) > FMath::Square(MaxDetectionRange)) return false;
		DesignateSpotlightPoint(WorldValue); return true;
	case ERadarCommandType::ClearSpotlight:
		ClearSpotlightTarget(); return true;
	case ERadarCommandType::HelmetCue:
		if (WorldValue.SizeSquared() > 1.1f) return false;
		SetHelmetLookDirection(WorldValue); return true;
	case ERadarCommandType::ApplyScanPreset:
		if (IntValue < 0 || IntValue > static_cast<int32>(ERadarScanSize::Custom)) return false;
		ApplyScanSizePreset(static_cast<ERadarScanSize>(IntValue)); return true;
	default: return false;
	}
}
