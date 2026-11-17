module;

#include <common.hxx>

export module registration;

import episodic;
import fixes;
import graphics;
import loader;
import misc;
import multiplayer;
import performance;
import stability;
import vehicles;

// The ordered registration entry point.
//
// The feature modules own their arms, bodies, published state and tables. This
// module owns only *when* each arm subscribes: the historical publish/install
// sequence interleaves keys from different INI sections, and it is behavioural --
// a site written before the publish that reads it hands a body a destroyed
// resume, and the two vehicle-shadow arms only agree because
// ForceCarHeadlightShadows runs after CarDynamicShadowFix. So the two tiers are
// listed here in the original order rather than by section.
//
// Adding an arm: add its function to its section module and add its subscription
// below at the position the original handler runs it -- never by module order.
//
// The old `Register*` blocks also filled unconditional data before subscribing.
// Those fills live in each section's `Prepare()`, called first, and they write
// module words or register bodies only; nothing reads them before `Loader::Run`.
export namespace Registration
{
    void RegisterAll();
}

void Registration::RegisterAll()
{
    // The unconditional preamble, in the order the old Register* blocks ran it.
    // None of this reads an option or writes an image byte.
    Stability::Prepare();
    Vehicles::Prepare();
    Graphics::Prepare();
    Performance::Prepare();
    Episodic::Prepare();
    Multiplayer::Prepare();
    Misc::Prepare();
    Fixes::Prepare();

    // Publish tier: every publish runs before every install.
    Loader::PublishEvent() += []
    {
        Stability::PublishFreezeGunFix();
        Multiplayer::PublishGroupHackFix();
        Multiplayer::PublishPathNodeCrashFix();
        Vehicles::PublishPlaneCrashFix();
        Multiplayer::PublishBlackscreenFix();
        Multiplayer::PublishCrashMsgFix();
        Stability::PublishWantedLevelCrashFix();
        Stability::PublishEpisodeSwitchFreezeFix();
        Stability::PublishSMPA60Fix();
        Misc::PublishPlayerAsPedComponentsFix();
        Multiplayer::PublishBadPlayerModelCrashFix();
        Multiplayer::PublishCollisionDeadlockFix();
        Graphics::PublishCarPartsShadowFix();
        Graphics::PublishCarDynamicShadowFix();
        Graphics::PublishForceCarHeadlightShadows();
        Fixes::PublishHighFPSBikePhysicsFix();
        Graphics::PublishDualVehicleHeadlights();
        Vehicles::PublishMovingSteeringWheelSupport();
        Vehicles::PublishShakingEngineBlock();
        Vehicles::PublishMovingPlaneParts();
        Vehicles::PublishGarageModelFix();
        Vehicles::PublishNoLiveryLimit();
        Multiplayer::PublishFreezeCarFix();
        Graphics::PublishBuildingDynamicShadows();
        Fixes::PublishCutsceneFixes();
        Graphics::PublishEmissiveLerpFix();
        Multiplayer::PublishSuperLODFix();
        Misc::CopyOverrideDatPath();
        Performance::PublishNewMemorySystem();
        Episodic::PublishLoadDLCs();
        Misc::PublishRestoreDeathMusic();
        Fixes::PublishMouseFix();
        Fixes::PublishDrivebyFOVFix();
        Fixes::PublishSpeedupBugFix();
        Multiplayer::PublishPatch4RemoveModChecks();
        Vehicles::PublishCopSirensFlag();
        Vehicles::PublishPopupHeadlightsFlag();
        Fixes::PublishOutOfCommissionFix();
    };

    // Install tier: the original sequence, key by key. The six arms the old
    // `InstallSimpleBranchArms` table drove keep the table's order (Blackscreen,
    // FreezeGun, GroupHack, PathNode, Plane, WantedLevel).
    Loader::InstallEvent() += []
    {
        Multiplayer::InstallMPNikoCrashFix();
        Multiplayer::InstallBlackscreenFix();
        Stability::InstallFreezeGunFix();
        Multiplayer::InstallGroupHackFix();
        Multiplayer::InstallPathNodeCrashFix();
        Vehicles::InstallPlaneCrashFix();
        Stability::InstallWantedLevelCrashFix();
        Multiplayer::InstallCrashMsgFix();
        Misc::InstallPlayerAsPedComponentsFix();
        Stability::InstallEpisodeSwitchFreezeFix();
        Stability::InstallSMPA60Fix();
        Episodic::InstallSCOSignatureFix();
        Stability::InstallTryToSkipAllErrors();
        Multiplayer::InstallRemoveAllWeaponsProtection();
        Fixes::InstallHighFPSSpeedupFix();
        Multiplayer::InstallAimProjectileCrashFix();
        Multiplayer::InstallBadPlayerModelCrashFix();
        Vehicles::InstallCarDecalsFix();
        Multiplayer::InstallCollisionDeadlockFix();
        Graphics::InstallCarPartsShadowFix();
        Graphics::InstallCarDynamicShadowFix();
        Graphics::InstallForceCarHeadlightShadows();
        Graphics::InstallInfiniteCarBottomDistance();
        Graphics::InstallReversingLightFix();
        Fixes::InstallBikeFeetFix();
        Fixes::InstallBikePhoneAnimsFix();
        Episodic::InstallEpisodicVehicleSupport();
        Fixes::InstallHighFPSBikePhysicsFix();
        Episodic::InstallPipeBombDropIconSupport();
        Multiplayer::InstallTeleportProtection();
        Graphics::InstallDualVehicleHeadlights();
        Vehicles::InstallIncreaseVehicleModelLimit();
        Vehicles::InstallIncreaseVehicleStructLimit();
        Vehicles::InstallMovingSteeringWheelSupport();
        Vehicles::InstallShakingEngineBlock();
        Vehicles::InstallMovingPlaneParts();
        Vehicles::InstallGarageModelFix();
        Vehicles::InstallNoLiveryLimit();
        Multiplayer::InstallFreezeCarFix();
        Graphics::InstallBuildingAlphaFix();
        Graphics::InstallBuildingDynamicShadows();
        Fixes::InstallCutsceneFixes();
        Graphics::InstallEmissiveLerpFix();
        Graphics::InstallForceShadowsOnObjects();
        Performance::InstallImprovedShaderStreaming();
        Graphics::InstallNoImpostorHeightCutoff();
        Graphics::InstallNoLODLightHeightCutoff();
        Graphics::InstallRemoveStaticCarShadows();
        Graphics::InstallRemoveStaticCutsceneCarShadows();
        Multiplayer::InstallSuperLODFix();
        Performance::InstallFastLoading();
        Performance::InstallNoLoadingSleep();
        Multiplayer::InstallRemoveGFWLUpdatePopup();
        Multiplayer::InstallRemoveRGSCLoginRequirement();
        Performance::InstallStartupTimeFix();
        Fixes::InstallVSyncFix();
        Stability::InstallBenchmarkFix();
        Misc::InstallOverrideGTADatPath();
        Performance::InstallNewMemorySystem();
        Episodic::InstallLoadDLCs();
        Misc::InstallAllowHashesForIDE();
        Stability::InstallCombinedRadioFreezeFix();
        Multiplayer::InstallBetterMPSync();
        Multiplayer::InstallIncreasePtrNodes();
        Episodic::InstallEpisodicWeaponSupport();
        Episodic::InstallPoliceEpisodicWeaponSupport();
        Episodic::InstallTBoGTHeliHeightLimit();
        Misc::InstallPedModelsInCutscenesFix();
        Misc::InstallRestoreDeathMusic();
        Fixes::InstallMouseFix();
        Multiplayer::InstallPatch6and7RemoveModChecks();
        Fixes::InstallDrivebyFOVFix();
        Fixes::InstallSpeedupBugFix();
        Multiplayer::InstallPatch4RemoveModChecks();
        Vehicles::InstallCopSirensFlag();
        Vehicles::InstallPopupHeadlightsFlag();
        Fixes::InstallOutOfCommissionFix();
    };
}
