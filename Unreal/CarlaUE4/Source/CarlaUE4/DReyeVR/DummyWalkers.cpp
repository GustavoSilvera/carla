#include "DummyWalkers.h"
#include "Carla/Actor/ActorDefinition.h" // FActorDefinition
#include "Carla/Actor/CarlaActor.h"      // FCarlaActor
#include "Carla/Game/CarlaStatics.h"     // GetRecorder, GetEpisode
#include "Carla/Walker/WalkerBase.h"     // AWalkerBase
#include "Carla/Walker/WalkerControl.h"  // FWalkerControl
#include "DReyeVRUtils.h"                // bIsWalkable

DummyWalkers::DummyWalkers()
{
    /// TODO: use params file
    NumWalkers = 30;
    Seed = 0;
}
void DummyWalkers::Setup(UWorld *World)
{
    // get carla episode/map
    auto Episode = UCarlaStatics::GetCurrentEpisode(World);
    ensure(Episode != nullptr);
    if (Episode == nullptr)
    {
        return;
    }
    ALargeMapManager *LargeMap = UCarlaStatics::GetLargeMapManager(Episode->GetWorld());

    TArray<FVector> SideWalkLocations;
    { // get sidewalk pieces from the world to spawn walkers on

        // sample points around the vehicle spawn point
        TArray<FTransform> SpawnPoints = Episode->GetRecommendedSpawnPoints();
        constexpr float CM_To_M = 100.f; // cm to m
        const float SearchRadius = 10.f; // meters
        const FVector2D SearchRange{CM_To_M * SearchRadius, CM_To_M * SearchRadius};
        const FVector2D StepSize{CM_To_M * 0.5f, CM_To_M * 0.5}; // resolution for step size (smaller => longer)

        const FBox WorldBounds = ALevelBounds::CalculateLevelBounds(World->GetCurrentLevel());
        const float Height = WorldBounds.Max.Z - WorldBounds.Min.Z;
        for (FTransform &FT : SpawnPoints)
        {
            FVector Center = FT.GetLocation();
            for (float YPos = Center.Y - SearchRange.Y; YPos < Center.Y + SearchRange.Y; YPos += StepSize.Y)
            {
                for (float XPos = Center.X - SearchRange.X; XPos < Center.X + SearchRange.X; XPos += StepSize.X)
                {
                    // compute ground actor
                    FVector TopPosition{XPos, YPos, WorldBounds.Max.Z};
                    FHitResult Hit = DownGroundTrace(World, TopPosition, Height);
                    AActor *GroundActor = Hit.Actor.Get(); // dereference weak ptr
                    if (GroundActor != nullptr && bIsWalkable(GroundActor))
                    {
                        SideWalkLocations.Add(Hit.Location); // absolute location of hit
                    }
                }
            }
        }

        if (SideWalkLocations.Num() == 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("No sidewalks found to spawn walkers on!"));
            return;
        }
    }

    // designate walker definitions
    TArray<FActorDefinition> SpawnableWalkers;
    {
        TArray<FActorDefinition> Spawnable = Episode->GetActorDefinitions();
        for (FActorDefinition &F : Spawnable)
        {
            if (F.Id.ToLower().Contains("walker"))
                SpawnableWalkers.Add(F);
        }
        if (SpawnableWalkers.Num() == 0)
        {
            UE_LOG(LogTemp, Log, TEXT("No available walkers definitions to spawn!"));
            return;
        }
    }

    // begin spawning the walkers
    for (size_t i = 0; i < NumWalkers; i++)
    {
        /// TODO: seed the randomness so it is consistent

        // designate spawn location
        FTransform SpawnTransform;
        {
            size_t RandIdx = FMath::RandRange(0, SideWalkLocations.Num() - 1);
            FVector Location = SideWalkLocations[RandIdx];
            Location.Z += 100; // to account for the actor height (cm)
            FRotator Rotation = FRotator::ZeroRotator;
            Rotation.Yaw = FMath::RandRange(-180, 180); // random heading
            SpawnTransform = FTransform{Rotation, Location, FVector::OneVector};
        }

        // spawn Walker
        FCarlaActor *Walker = nullptr;
        {
            // track the spawnable walkers
            FActorDescription Description;
            {
                // designate walker type
                size_t RandIdx = FMath::RandRange(0, SpawnableWalkers.Num() - 1);
                FActorDefinition Definition = SpawnableWalkers[RandIdx];
                Description.UId = Definition.UId;
                Description.Id = Definition.Id;
                Description.Class = Definition.Class;
                // Description.Variations = Definition.Variations;
            }

            // spawn the actor
            auto Result = Episode->SpawnActorWithInfo(SpawnTransform, std::move(Description));

            if (Result.Key != EActorSpawnResultStatus::Success)
            {
                UE_LOG(LogTemp, Error, TEXT("Actor not Spawned"));
                continue;
            }

            if (LargeMap)
            {
                LargeMap->OnActorSpawned(*Result.Value);
            }

            Episode->SerializeActor(Result.Value);

            Walker = Result.Value; // assign FCarlaActor
        }
        ensure(Walker != nullptr);
        Walkers.push_back(Walker);
    }
}

void DummyWalkers::Tick(UWorld *World, const float DeltaSeconds)
{
    // loop over all relevent (rendered) walkers and give them some simple
    // policy to walk around the sidewalks

    for (FCarlaActor *Walker : Walkers)
    {
        ensure(Walker != nullptr);
        if (Walker == nullptr)
            continue;

        // get AActor from FCarlaActor
        const AActor *WalkerActor = Walker->GetActor();
        ensure(WalkerActor != nullptr);
        if (WalkerActor == nullptr)
            continue;

        // optimization tip! Only compute their walk cycle if they are visible!
        {
            const float Tolerance = 0.0f; // immediate!
            if (!WalkerActor->WasRecentlyRendered(Tolerance))
            {
                continue; // skip computation for this actor
            }
        }

        const FVector &Location = WalkerActor->GetRootComponent()->GetComponentLocation(); // world space
        FRotator Rotation = WalkerActor->GetRootComponent()->GetComponentRotation();       // world space
        // move player forward by Speed meters per second
        const float Speed = 134.f;      // ~3mph (average human walking speed)
        const float AngularStep = 45.f; // granularity of angular checks for walkable
        const size_t NumChecks = static_cast<size_t>(360 / AngularStep);
        const float PersonHeightMax = 180; // cm (on average)
        FVector NewWalkableLocation;       // where the actor will land if they take the step

        bool Walkable = false;
        int RemainingIters = 10; // upper bound so loop won't go forever
        float Lookahead = 1.f;   // grows to cover more ground if no walkable found
        while (Walkable == false && RemainingIters > 0)
        {
            for (int i = 0; i < NumChecks && !Walkable; i++)
            {
                const FVector PossibleNewLocation =
                    Location + Lookahead * Speed * Rotation.RotateVector(FVector::ForwardVector);
                { // compute if ground in front of Walker is walkable
                    FHitResult Hit = DownGroundTrace(World, PossibleNewLocation, PersonHeightMax, {WalkerActor});
                    class AActor *GroundActor = nullptr;
                    GroundActor = Hit.Actor.Get(); // dereference weak ptr
                    Walkable = (GroundActor != nullptr && bIsWalkable(GroundActor));
                    NewWalkableLocation = Hit.Location;
                }
                if (!Walkable)
                { // begin rotation step to see if the next AngularStep is walkable
                    // rotate CW (arbitrary choice)
                    Rotation.Yaw += AngularStep;
                }
            }
            Lookahead += 1.f; // can also make it double (ie. nonlinear increase)
            RemainingIters--;
        }
        ensure(Walkable == true || RemainingIters == 0);
        if (RemainingIters == 0)
        {
            /// TODO: fix linker error
            // Walker->PutActorToSleep(UCarlaStatics::GetCurrentEpisode(World)); // destroys this actor
        }

        FWalkerControl WalkerControl;
        { // create walker control following policy (stay on sidewalk)

            { // set forward speed
                WalkerControl.Speed = Speed;
            }

            { // need to jump?
                if (Walkable)
                {
                    FHitResult RightBelow = DownGroundTrace(World, Location, PersonHeightMax, {WalkerActor});
                    AActor *RightBelowActor = RightBelow.Actor.Get();
                    if (RightBelowActor != nullptr && RightBelowActor->GetName().ToLower().Contains("curb"))
                    {
                        const float JumpThresh = 5.f; // cm threshold to jump
                        // if the walkable location is higher than the current location by a threshold
                        UE_LOG(LogTemp, Log, TEXT("Z diff: %.3f, %.3f"), NewWalkableLocation.Z, RightBelow.Location.Z);
                        if (NewWalkableLocation.Z > RightBelow.Location.Z + JumpThresh) // perhaps on a curb/hill
                        {
                            WalkerControl.Jump = true;
                        }
                    }
                }
            }

            { // set forward direction (turn to Walkable point)
                // Direction stores the direction (not rotation!) of the vector which speed is applied to
                WalkerControl.Direction = Rotation.Vector(); // unit vector
                WalkerControl.Direction.Normalize();         // ensure unit length so speed is unchanged
            }
        }
        Walker->SetActorState(carla::rpc::ActorState::Active); // NOT dormant
        auto Response = Walker->ApplyControlToWalker(WalkerControl);
    }
}
