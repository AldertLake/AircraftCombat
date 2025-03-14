// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftRadarComponent.h"
#include "AircraftDataLinkSubsystem.h"
#include "ModularMissionManagement.h"
#include "CombatTeamUtility.h"
#include "IFFTransponderComponent.h"
#include "Engine/World.h"

int32 UAircraftRadarComponent::AssignContactID(AActor* Actor)
{
	if (ActorContactIDs.Num() > 256)
	{
		for (auto It = ActorContactIDs.CreateIterator(); It; ++It)
			if (!It.Key().IsValid()) It.RemoveCurrent();
	}
	if (bEnableTrackCorrelation && IsValid(Actor))
	{
		const TWeakObjectPtr<AActor> Key(Actor);
		if (const int32* Existing = ActorContactIDs.Find(Key)) return *Existing;
		const int32 ID = NextContactID++;
		ActorContactIDs.Add(Key, ID);
		return ID;
	}
	return NextContactID++;
}

bool UAircraftRadarComponent::GetBestTrackForContact(int32 ContactID, FRadarTrack& OutTrack) const
{
	if (ContactID <= 0) return false;
	TArray<FRadarTrack> Display;
	GetDisplayTracks(Display);
	for (const FRadarTrack& Track : Display)
	{
		if (Track.ContactID != ContactID || Track.Status == ERadarTrackStatus::Lost) continue;
		const float MaxAge = Track.Source == ERadarTrackSource::Local ?
			LocalCorrelationFreshnessSeconds : DataLinkTrackExpirySeconds;
		if (Track.TrackAge <= MaxAge)
		{
			OutTrack = Track;
			return true;
		}
	}
	return false;
}

void UAircraftRadarComponent::RefreshCorrelatedSelection()
{
	if (SelectedContactID <= 0) return;
	FRadarTrack Best;
	if (!GetBestTrackForContact(SelectedContactID, Best))
	{
		SelectedContactID = 0;
		SelectedLinkedTrackID = -1;
		if (UModularMissionManagement* Mission = GetOwner()->FindComponentByClass<UModularMissionManagement>())
			Mission->ClearDesignatedTarget();
		return;
	}
	if (Best.Source != ERadarTrackSource::Local)
	{
		if (STTLockedTrackID >= 0)
		{
			FRadarTrack Locked;
			if (GetTrackByID(STTLockedTrackID, Locked) && Locked.ContactID == SelectedContactID &&
				Locked.TrackAge > LocalCorrelationFreshnessSeconds) BreakLock();
		}
		if (BuggedTrackID >= 0)
		{
			FRadarTrack Bugged;
			if (GetTrackByID(BuggedTrackID, Bugged) && Bugged.ContactID == SelectedContactID &&
				Bugged.TrackAge > LocalCorrelationFreshnessSeconds) ClearBugTrack();
		}
	}
	const int32 NewLinkedID = Best.Source == ERadarTrackSource::Local ? -1 : Best.TrackID;
	if (SelectedLinkedTrackID == NewLinkedID) return;
	SelectedLinkedTrackID = NewLinkedID;
	OnRadarTrackSelected.Broadcast(Best);
}

bool UAircraftRadarComponent::GetBestWeaponSupportTrackForActor(const AActor* Actor,
	FRadarTrack& OutTrack) const
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Actor)) return false;
	if (GetTrackByActor(Actor, OutTrack) && OutTrack.Status != ERadarTrackStatus::Lost &&
		OutTrack.TrackAge <= FMath::Min(1.5f, LocalCorrelationFreshnessSeconds)) return true;
	if (!bAllowRemoteWeaponSupport) return false;
	return GetFreshLinkedTrackForActor(Actor, 0, OutTrack);
}

void UAircraftRadarComponent::SetDataLinkNetworkID(FName NewNetworkID)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (DataLinkNetworkID == NewNetworkID) return;
	DataLinkNetworkID = NewNetworkID;
	const bool bHadLinkedSelection = SelectedLinkedTrackID != -1;
	LinkedTracks.Reset();
	LinkedTrackSources.Reset();
	SelectedLinkedTrackID = -1;
	FRadarTrack LocalFallback;
	if (!GetBestTrackForContact(SelectedContactID, LocalFallback))
	{
		SelectedContactID = 0;
		if (bHadLinkedSelection)
			if (UModularMissionManagement* Mission = GetOwner()->FindComponentByClass<UModularMissionManagement>())
				Mission->ClearDesignatedTarget();
	}
	LastDataLinkReceptionTime = -1000000.0f;
}

void UAircraftRadarComponent::SetDataLinkContributionEnabled(bool bEnabled)
{
	if (GetOwner() && GetOwner()->HasAuthority()) bContributeDataLinkTracks = bEnabled;
}

void UAircraftRadarComponent::SetDataLinkReceptionEnabled(bool bEnabled)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	bReceiveDataLinkTracks = bEnabled;
	if (!bEnabled)
	{
		const bool bHadLinkedSelection = SelectedLinkedTrackID != -1;
		LinkedTracks.Reset();
		LinkedTrackSources.Reset();
		SelectedLinkedTrackID = -1;
		FRadarTrack LocalFallback;
		if (!GetBestTrackForContact(SelectedContactID, LocalFallback))
		{
			SelectedContactID = 0;
			if (bHadLinkedSelection)
				if (UModularMissionManagement* Mission = GetOwner()->FindComponentByClass<UModularMissionManagement>())
					Mission->ClearDesignatedTarget();
		}
		LastDataLinkReceptionTime = -1000000.0f;
	}
}

void UAircraftRadarComponent::SetDataLinkRadioInhibited(bool bTransmitInhibited, bool bReceiveInhibited)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	bDataLinkTransmitInhibited = bTransmitInhibited;
	bDataLinkReceiveInhibited = bReceiveInhibited;
	if (bReceiveInhibited) LastDataLinkReceptionTime = -1000000.0f;
}

bool UAircraftRadarComponent::CanDataLinkTransmit() const
{
	return bEnableDataLink && DataLinkNetworkID != NAME_None && !bDataLinkTransmitInhibited &&
		IsValid(GetOwner()) && GetOwner()->HasAuthority();
}

bool UAircraftRadarComponent::CanDataLinkReceive() const
{
	return bEnableDataLink && bReceiveDataLinkTracks && DataLinkNetworkID != NAME_None &&
		!bDataLinkReceiveInhibited && IsValid(GetOwner()) && GetOwner()->HasAuthority();
}

bool UAircraftRadarComponent::CanDataLinkRelay() const
{
	return bRelayDataLinkReports && CanDataLinkTransmit() && CanDataLinkReceive();
}

bool UAircraftRadarComponent::IsDataLinkConnected() const
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return bClientDataLinkConnected;
	return bEnableDataLink && bReceiveDataLinkTracks && !bDataLinkReceiveInhibited && GetWorld() &&
		GetWorld()->GetTimeSeconds() - LastDataLinkReceptionTime <= FMath::Max(0.1f, DataLinkDesyncSeconds);
}

void UAircraftRadarComponent::ReceiveDataLinkHeartbeat(float WorldTime)
{
	if (CanDataLinkReceive()) LastDataLinkReceptionTime = WorldTime;
}

void UAircraftRadarComponent::ReceiveDataLinkReport(const FRadarTrack& Report,
	UAircraftRadarComponent* SourceRadar, float MeasurementTime, float WorldTime)
{
	if (!CanDataLinkReceive() || !IsValid(SourceRadar) || SourceRadar == this ||
		SourceRadar->DataLinkNetworkID != DataLinkNetworkID || !FMath::IsFinite(MeasurementTime) ||
		WorldTime - MeasurementTime > DataLinkTrackExpirySeconds || MeasurementTime > WorldTime) return;
	// Enforce this at ingress too: direct callers must not inject a donated
	// track of the receiving aircraft into its display or weapon support.
	if (Report.TrackedActor.Get() == GetOwner()) return;
	const int32 OriginID = SourceRadar->GetDataLinkParticipantID();
	if (OriginID <= 0 || Report.TrackID <= 0) return;
	FRadarTrack Linked = Report;
	Linked.SourceParticipantID = OriginID;
	Linked.SourceTrackID = Report.TrackID;
	switch (SourceRadar->DataLinkPlatformType)
	{
	case EDataLinkPlatformType::AirborneC2: Linked.Source = ERadarTrackSource::AirborneC2; break;
	case EDataLinkPlatformType::Fighter: Linked.Source = ERadarTrackSource::Fighter; break;
	case EDataLinkPlatformType::Surface: Linked.Source = ERadarTrackSource::Surface; break;
	case EDataLinkPlatformType::Ground: Linked.Source = ERadarTrackSource::Ground; break;
	default: Linked.Source = ERadarTrackSource::Unknown; break;
	}
	Linked.bHasActorAssociation = Report.TrackedActor.IsValid();
	Linked.TrackAge = WorldTime - MeasurementTime;
	Linked.Status = ERadarTrackStatus::Tracked;
	Linked.bIsBeamTarget = false;
	Linked.bIsBugged = false;
	const FVector Relative = Linked.LastKnownPosition - GetRadarLocation();
	Linked.Range = Relative.Size();
	ComputeBearingElevation(Linked.LastKnownPosition, Linked.Bearing, Linked.Elevation);
	Linked.ClosureRate = -FVector::DotProduct(Relative.GetSafeNormal(), Linked.EstimatedVelocity - GetOwner()->GetVelocity());
	Linked.IFFResult = Linked.TrackedActor.IsValid() ? ClassifyIFF(Linked.TrackedActor.Get()) : ERadarIFFResult::Unknown;
	for (FRadarTrack& Existing : LinkedTracks)
	{
		if (Existing.SourceParticipantID == OriginID && Existing.SourceTrackID == Report.TrackID)
		{
			if (Existing.TrackAge < Linked.TrackAge) return;
			Linked.TrackID = Existing.TrackID;
			Linked.ContactID = Existing.ContactID;
			if (bEnableTrackCorrelation && Linked.TrackedActor.IsValid())
				Linked.ContactID = AssignContactID(Linked.TrackedActor.Get());
			Existing = Linked;
			LinkedTrackSources.Add(Linked.TrackID, SourceRadar);
			OnRadarContactUpdated.Broadcast(Existing);
			return;
		}
	}
	Linked.TrackID = NextLinkedTrackID--;
	Linked.ContactID = AssignContactID(Linked.TrackedActor.Get());
	if (LinkedTracks.Num() >= FMath::Max(1, MaxLinkedDataLinkTracks))
	{
		int32 Oldest = INDEX_NONE;
		float OldestAge = -1.0f;
		for (int32 Index = 0; Index < LinkedTracks.Num(); ++Index)
		{
			if (LinkedTracks[Index].TrackID != SelectedLinkedTrackID && LinkedTracks[Index].TrackAge > OldestAge)
			{
				OldestAge = LinkedTracks[Index].TrackAge;
				Oldest = Index;
			}
		}
		if (Oldest == INDEX_NONE) return;
		const int32 RemovedID = LinkedTracks[Oldest].TrackID;
		LinkedTracks.RemoveAt(Oldest);
		LinkedTrackSources.Remove(RemovedID);
		OnRadarContactLost.Broadcast(RemovedID);
	}
	LinkedTracks.Add(Linked);
	LinkedTrackSources.Add(Linked.TrackID, SourceRadar);
	OnRadarContactNew.Broadcast(Linked);
}

void UAircraftRadarComponent::PruneLinkedTracks(float WorldTime)
{
	(void)WorldTime;
	const UIFFTransponderComponent* Transponder = GetOwner()
		? GetOwner()->FindComponentByClass<UIFFTransponderComponent>() : nullptr;
	for (int32 Index = LinkedTracks.Num() - 1; Index >= 0; --Index)
	{
		FRadarTrack& Track = LinkedTracks[Index];
		UAircraftRadarComponent* Source = LinkedTrackSources.FindRef(Track.TrackID).Get();
		Track.TrackAge += GetWorld()->GetDeltaSeconds();
		if (Track.TrackedActor.Get() == GetOwner() ||
			!bEnableDataLink || !bReceiveDataLinkTracks || bDataLinkReceiveInhibited ||
			Track.TrackAge > FMath::Max(0.1f, DataLinkTrackExpirySeconds) ||
			!IsValid(Source) || Source->DataLinkNetworkID != DataLinkNetworkID ||
			!Source->bContributeDataLinkTracks ||
			(Source->bRestrictDataLinkToFriendly &&
				((Transponder && !Transponder->IsTransponderActive()) ||
				FCombatTeamUtility::GetAttitude(Source->GetOwner(), GetOwner(), ETeamAttitude::Neutral)
					!= ETeamAttitude::Friendly)))
		{
			const int32 RemovedID = Track.TrackID;
			LinkedTracks.RemoveAt(Index);
			LinkedTrackSources.Remove(RemovedID);
			if (SelectedLinkedTrackID == RemovedID)
			{
				SelectedLinkedTrackID = -1;
				FRadarTrack Fallback;
				if (GetBestTrackForContact(SelectedContactID, Fallback))
				{
					if (Fallback.Source != ERadarTrackSource::Local) SelectedLinkedTrackID = Fallback.TrackID;
				}
				else
				{
					SelectedContactID = 0;
					if (UModularMissionManagement* Mission = GetOwner()->FindComponentByClass<UModularMissionManagement>())
						Mission->ClearDesignatedTarget();
				}
			}
			OnRadarContactLost.Broadcast(RemovedID);
		}
	}
}

void UAircraftRadarComponent::GetDisplayTracks(TArray<FRadarTrack>& OutTracks) const
{
	OutTracks.Reset();
	TMap<int32, int32> ContactIndices;
	auto Consider = [this, &OutTracks, &ContactIndices](const FRadarTrack& Candidate)
	{
		if (Candidate.ContactID <= 0) { OutTracks.Add(Candidate); return; }
		if (const int32* Index = ContactIndices.Find(Candidate.ContactID))
		{
			FRadarTrack& Existing = OutTracks[*Index];
			const bool bCandidateLocalFresh = Candidate.Source == ERadarTrackSource::Local &&
				Candidate.TrackAge <= LocalCorrelationFreshnessSeconds &&
				Candidate.Status != ERadarTrackStatus::Lost;
			const bool bExistingLocalFresh = Existing.Source == ERadarTrackSource::Local &&
				Existing.TrackAge <= LocalCorrelationFreshnessSeconds &&
				Existing.Status != ERadarTrackStatus::Lost;
			const bool bCandidateFresh = Candidate.Status != ERadarTrackStatus::Lost &&
				Candidate.TrackAge <= (Candidate.Source == ERadarTrackSource::Local ?
					LocalCorrelationFreshnessSeconds : DataLinkTrackExpirySeconds);
			const bool bExistingFresh = Existing.Status != ERadarTrackStatus::Lost &&
				Existing.TrackAge <= (Existing.Source == ERadarTrackSource::Local ?
					LocalCorrelationFreshnessSeconds : DataLinkTrackExpirySeconds);
			if ((bCandidateLocalFresh && !bExistingLocalFresh) ||
				(bCandidateLocalFresh == bExistingLocalFresh && bCandidateFresh && !bExistingFresh) ||
				(bCandidateLocalFresh == bExistingLocalFresh && bCandidateFresh == bExistingFresh &&
					(Candidate.TrackAge < Existing.TrackAge ||
						(FMath::IsNearlyEqual(Candidate.TrackAge, Existing.TrackAge) &&
							Candidate.SignalStrength > Existing.SignalStrength)))) Existing = Candidate;
			return;
		}
		ContactIndices.Add(Candidate.ContactID, OutTracks.Add(Candidate));
	};
	for (const FRadarTrack& Track : Tracks) Consider(Track);
	for (const FRadarTrack& Track : LinkedTracks) Consider(Track);
}

bool UAircraftRadarComponent::GetLinkedTrackByID(int32 TrackID, FRadarTrack& OutTrack) const
{
	if (const FRadarTrack* Found = LinkedTracks.FindByPredicate([TrackID](const FRadarTrack& Track)
		{ return Track.TrackID == TrackID; }))
	{
		OutTrack = *Found;
		return true;
	}
	return false;
}

UAircraftRadarComponent* UAircraftRadarComponent::GetLinkedTrackSource(int32 TrackID) const
{
	return LinkedTrackSources.FindRef(TrackID).Get();
}

bool UAircraftRadarComponent::GetFreshLinkedTrackForActor(const AActor* Actor,
	int32 OriginID, FRadarTrack& OutTrack) const
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsDataLinkConnected() || !IsValid(Actor)) return false;
	bool bFound = false;
	for (const FRadarTrack& Track : LinkedTracks)
	{
		if (Track.TrackedActor.Get() != Actor || (OriginID > 0 && Track.SourceParticipantID != OriginID) ||
			Track.TrackAge > FMath::Min(DataLinkTrackExpirySeconds, 1.5f)) continue;
		UAircraftRadarComponent* Source = GetLinkedTrackSource(Track.TrackID);
		FRadarTrack SourceTrack;
		if (!IsValid(Source) || !Source->CanDataLinkTransmit() || !Source->bContributeDataLinkTracks ||
			!Source->IsRadarEmitting() || Source->DataLinkNetworkID != DataLinkNetworkID ||
			!Source->GetTrackByID(Track.SourceTrackID, SourceTrack) ||
			SourceTrack.TrackedActor.Get() != Actor || SourceTrack.Status == ERadarTrackStatus::Lost ||
			SourceTrack.TrackAge > Source->DataLinkTrackExpirySeconds) continue;
		if (!bFound || Track.TrackAge < OutTrack.TrackAge ||
			(FMath::IsNearlyEqual(Track.TrackAge, OutTrack.TrackAge) &&
				Track.SignalStrength > OutTrack.SignalStrength))
		{
			OutTrack = Track;
			bFound = true;
		}
	}
	return bFound;
}

bool UAircraftRadarComponent::GetSelectedLinkedTrackForActor(const AActor* Actor, FRadarTrack& OutTrack) const
{
	FRadarTrack Local;
	if (GetTrackByActor(Actor, Local) && Local.Status != ERadarTrackStatus::Lost &&
		Local.TrackAge <= LocalCorrelationFreshnessSeconds) return false;
	if (!GetLinkedTrackByID(SelectedLinkedTrackID, OutTrack) || OutTrack.TrackedActor.Get() != Actor) return false;
	return GetFreshLinkedTrackForActor(Actor, OutTrack.SourceParticipantID, OutTrack);
}

bool UAircraftRadarComponent::DesignateLinkedTrack(int32 TrackID)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return false;
	FRadarTrack Track;
	if (!GetLinkedTrackByID(TrackID, Track) || Track.TrackAge > DataLinkTrackExpirySeconds) return false;
	if (bEnableTrackCorrelation && Track.ContactID > 0)
	{
		for (const FRadarTrack& Local : Tracks)
		{
			if (Local.ContactID != Track.ContactID ||
				Local.Status == ERadarTrackStatus::Lost ||
				Local.TrackAge > LocalCorrelationFreshnessSeconds) continue;
			const bool bSelected = CommandBugTrack(Local.TrackID);
			if (bSelected)
				if (UModularMissionManagement* Mission = GetOwner()->FindComponentByClass<UModularMissionManagement>())
					Mission->SetDesignatedTarget(Local.TrackedActor.Get());
			return bSelected;
		}
	}
	FRadarTrack Fresh;
	if (bAllowRemoteWeaponSupport && Track.TrackedActor.IsValid() &&
		!GetFreshLinkedTrackForActor(Track.TrackedActor.Get(), Track.SourceParticipantID, Fresh)) return false;
	SelectedLinkedTrackID = TrackID;
	SelectedContactID = Track.ContactID;
	OnRadarTrackSelected.Broadcast(Track);
	if (!bAllowRemoteWeaponSupport || !Track.TrackedActor.IsValid()) return true;
	float CueBearing = 0.0f;
	float CueElevation = 0.0f;
	ComputeBearingElevation(Fresh.LastKnownPosition + Fresh.EstimatedVelocity * Fresh.TrackAge,
		CueBearing, CueElevation);
	OffsetScanCenter(FMath::FindDeltaAngleDegrees(ScanCenterAzimuth, CueBearing),
		CueElevation - ScanCenterElevation);
	if (UModularMissionManagement* Mission = GetOwner()->FindComponentByClass<UModularMissionManagement>())
	{
		Mission->SetDesignatedTarget(Track.TrackedActor.Get());
		SelectedLinkedTrackID = TrackID;
		SelectedContactID = Track.ContactID;
		Mission->PrepareLinkedRadarWeapon(Fresh, GetLinkedTrackSource(TrackID));
	}
	return true;
}
