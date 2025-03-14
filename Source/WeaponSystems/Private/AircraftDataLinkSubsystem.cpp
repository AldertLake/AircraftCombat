// -----------------------------------------------------
// Copyright   (c) 2024 AldertLake. All Rights Reserved.
// GitHub:     https://github.com/AldertLake/
// Discord:    https://discord.gg/QpPPfh6WVn
// -----------------------------------------------------

#include "AircraftDataLinkSubsystem.h"
#include "AircraftRadarComponent.h"
#include "CombatTeamUtility.h"
#include "IFFTransponderComponent.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Stats/Stats.h"

void UAircraftDataLinkSubsystem::RegisterRadar(UAircraftRadarComponent* Radar)
{
	if (!IsValid(Radar) || !Radar->GetOwner() || !Radar->GetOwner()->HasAuthority()) return;
	for (const TWeakObjectPtr<UAircraftRadarComponent>& Entry : Participants)
	{
		if (Entry.Get() == Radar) return;
	}
	Radar->SetDataLinkParticipantID(NextParticipantID++);
	Participants.Add(Radar);
}

void UAircraftDataLinkSubsystem::UnregisterRadar(UAircraftRadarComponent* Radar)
{
	if (Radar) NextReportIndex.Remove(Radar->GetDataLinkParticipantID());
	Participants.RemoveAll([Radar](const TWeakObjectPtr<UAircraftRadarComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Radar;
	});
}

void UAircraftDataLinkSubsystem::Deinitialize()
{
	Participants.Reset();
	NextReportIndex.Reset();
	Super::Deinitialize();
}

TStatId UAircraftDataLinkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UAircraftDataLinkSubsystem, STATGROUP_Tickables);
}

void UAircraftDataLinkSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) return;
	Participants.RemoveAll([](const TWeakObjectPtr<UAircraftRadarComponent>& Entry) { return !Entry.IsValid(); });
	TransmissionAccumulator += DeltaTime;
	if (TransmissionAccumulator < FMath::Max(0.1f, TransmissionInterval)) return;
	TransmissionAccumulator = FMath::Fmod(TransmissionAccumulator, FMath::Max(0.1f, TransmissionInterval));
	TransmitWindow(World->GetTimeSeconds());
}

bool UAircraftDataLinkSubsystem::HasPhysicalPath(const UAircraftRadarComponent* First,
	const UAircraftRadarComponent* Second, const FVector& From, const FVector& To) const
{
	if (!First || !Second || First == Second || First->DataLinkNetworkID != Second->DataLinkNetworkID) return false;
	const float Range = FMath::Min(First->DataLinkRangeCm, Second->DataLinkRangeCm);
	if (FVector::DistSquared(From, To) > FMath::Square(FMath::Max(0.0f, Range))) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(AircraftDataLinkLOS), false);
	Params.AddIgnoredActor(First->GetOwner());
	Params.AddIgnoredActor(Second->GetOwner());
	return !GetWorld()->LineTraceTestByChannel(From, To, ECC_WorldStatic, Params);
}

void UAircraftDataLinkSubsystem::TransmitWindow(float WorldTime)
{
	TArray<UAircraftRadarComponent*> Nodes;
	for (const TWeakObjectPtr<UAircraftRadarComponent>& Entry : Participants)
	{
		if (UAircraftRadarComponent* Radar = Entry.Get(); IsValid(Radar) &&
			(Radar->CanDataLinkTransmit() || Radar->CanDataLinkReceive()))
			Nodes.Add(Radar);
	}
	// Evaluate each directed hop once per transmission window, then reuse it for every origin.
	TArray<TArray<int32>> Neighbors;
	Neighbors.SetNum(Nodes.Num());
	TArray<FVector> Locations;
	Locations.Reserve(Nodes.Num());
	for (UAircraftRadarComponent* Node : Nodes) Locations.Add(Node->GetRadarLocation());
	for (int32 FirstIndex = 0; FirstIndex < Nodes.Num(); ++FirstIndex)
	{
		for (int32 SecondIndex = FirstIndex + 1; SecondIndex < Nodes.Num(); ++SecondIndex)
		{
			UAircraftRadarComponent* First = Nodes[FirstIndex];
			UAircraftRadarComponent* Second = Nodes[SecondIndex];
			const bool bForward = First->CanDataLinkTransmit() && Second->CanDataLinkReceive();
			const bool bReverse = Second->CanDataLinkTransmit() && First->CanDataLinkReceive();
			if ((!bForward && !bReverse) ||
				!HasPhysicalPath(First, Second, Locations[FirstIndex], Locations[SecondIndex])) continue;
			if (bForward) Neighbors[FirstIndex].Add(SecondIndex);
			if (bReverse) Neighbors[SecondIndex].Add(FirstIndex);
		}
	}
	for (UAircraftRadarComponent* Origin : Nodes)
	{
		if (!Origin->CanDataLinkTransmit()) continue;
		TArray<int32> Depth;
		Depth.Init(INDEX_NONE, Nodes.Num());
		const int32 OriginIndex = Nodes.IndexOfByKey(Origin);
		Depth[OriginIndex] = 0;
		TArray<int32> Queue;
		Queue.Add(OriginIndex);
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const int32 CurrentIndex = Queue[Head];
			UAircraftRadarComponent* Current = Nodes[CurrentIndex];
			if (CurrentIndex != OriginIndex && !Current->CanDataLinkRelay()) continue;
			if (Depth[CurrentIndex] > MaxRelayHops) continue;
			for (int32 CandidateIndex : Neighbors[CurrentIndex])
			{
				if (Depth[CandidateIndex] != INDEX_NONE || CandidateIndex == OriginIndex) continue;
				UAircraftRadarComponent* Candidate = Nodes[CandidateIndex];
				if (Origin->bRestrictDataLinkToFriendly)
				{
					const UIFFTransponderComponent* Transponder = Candidate->GetOwner()->FindComponentByClass<UIFFTransponderComponent>();
					if ((Transponder && !Transponder->IsTransponderActive()) ||
						FCombatTeamUtility::GetAttitude(Origin->GetOwner(), Candidate->GetOwner(), ETeamAttitude::Neutral)
							!= ETeamAttitude::Friendly) continue;
				}
				Depth[CandidateIndex] = Depth[CurrentIndex] + 1;
				Queue.Add(CandidateIndex);
			}
		}
		const TArray<FRadarTrack>& LocalTracks = Origin->GetAllTracks();
		const int32 TrackCount = LocalTracks.Num();
		int32& Cursor = NextReportIndex.FindOrAdd(Origin->GetDataLinkParticipantID());
		TArray<FRadarTrack> Reports;
		if (Origin->bContributeDataLinkTracks && Origin->IsRadarEmitting() && TrackCount > 0)
		{
			for (int32 Step = 0; Step < TrackCount && Reports.Num() < FMath::Max(1, ReportsPerWindow); ++Step)
			{
				const FRadarTrack& Track = LocalTracks[(Cursor + Step) % TrackCount];
				if (Track.Status != ERadarTrackStatus::Lost && Track.TrackAge <= Origin->DataLinkTrackExpirySeconds)
					Reports.Add(Track);
			}
			Cursor = (Cursor + FMath::Max(1, ReportsPerWindow)) % TrackCount;
		}
		for (int32 RecipientIndex = 0; RecipientIndex < Nodes.Num(); ++RecipientIndex)
		{
			if (RecipientIndex == OriginIndex || Depth[RecipientIndex] == INDEX_NONE) continue;
			UAircraftRadarComponent* Recipient = Nodes[RecipientIndex];
			Recipient->ReceiveDataLinkHeartbeat(WorldTime);
			for (const FRadarTrack& Report : Reports)
			{
				// A donor may report this recipient to everyone else, but not to the
				// aircraft represented by the report itself.
				if (Report.TrackedActor.Get() == Recipient->GetOwner()) continue;
				Recipient->ReceiveDataLinkReport(Report, Origin, WorldTime - Report.TrackAge, WorldTime);
			}
		}
	}
}
