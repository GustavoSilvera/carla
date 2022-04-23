#pragma once

#include "Carla/Sensor/DReyeVRData.h" // DReyeVR namespace
#include "GameFramework/Actor.h"      // AActor

#include <unordered_map> // std::unordered_map
#include <utility>       // std::pair
#include <vector>        // std::vector

#include "DReyeVRCustomActor.generated.h"

UCLASS()
class CARLA_API ADReyeVRCustomActor : public AActor // abstract class
{
    GENERATED_BODY()

  public:
    ADReyeVRCustomActor(const FObjectInitializer &ObjectInitializer);

    virtual void Tick(float DeltaSeconds) override;

    static ADReyeVRCustomActor *RequestNewActor(UWorld *World, const FString &Name);
    void Activate();
    void Deactivate();
    bool IsActive() const
    {
        return bIsActive;
    }

    const static FString OpaqueMaterial, TransparentMaterial;

    void Initialize(const FString &Name);

    void SetInternals(const DReyeVR::CustomActorData &In);

    const DReyeVR::CustomActorData &GetInternals() const;

    static std::unordered_map<std::string, class ADReyeVRCustomActor *> ActiveCustomActors;

    // function to dynamically change the material params of the object at runtime
    void AssignMat(const FString &Path);
    struct DReyeVR::CustomActorData::MaterialParamsStruct MaterialParams;

  protected:
    void BeginPlay() override;
    void BeginDestroy() override;
    bool bIsActive = false; // initially deactivated

    void AssignSM(const FString &Path);

    class DReyeVR::CustomActorData Internals;

    UPROPERTY(EditAnywhere, Category = "Mesh")
    class UStaticMeshComponent *ActorMesh = nullptr;
    static int AllMeshCount;

    UPROPERTY(EditAnywhere, Category = "Materials")
    class UMaterialInstanceDynamic *DynamicMat = nullptr;
    int NumMaterials = 1; // change this to apply the dynamic material to more than 1 materials
};