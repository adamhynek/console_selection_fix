#include "common/IDebugLog.h"  // IDebugLog
#include "skse64_common/SafeWrite.h"
#include "skse64_common/skse_version.h"  // RUNTIME_VERSION
#include "skse64/PluginAPI.h"  // SKSEInterface, PluginInfo
#include "skse64_common/Relocation.h"
#include "skse64_common/BranchTrampoline.h"

#include <ShlObj.h>  // CSIDL_MYDOCUMENTS

#include "version.h"
#include "config.h"


// SKSE globals
static PluginHandle	g_pluginHandle = kPluginHandle_Invalid;
static SKSEMessagingInterface *g_messaging = nullptr;
SKSETrampolineInterface *g_trampoline = nullptr;


namespace RE {
    struct NiCamera : NiAVObject
    {
        float			m_aafWorldToCam[4][4];	// 110
        NiFrustum *ViewFrustumPtr_178;
        tArray<NiFrustum> eyeFrustums; // 180
        tArray<NiPoint3> eyePositions; // 198
        tArray<NiMatrix33> eyeRotations; // 1B0
        UInt32 numEyes;            // 1C8
        NiFrustum viewFrustum;       // 1CC
        float minNearPlaneDist;  // 1E8
        float maxFarNearRatio;   // 1EC
        NiRect<float> port;              // 1F0
        float lodAdjust;         // 200
    };
}


typedef void (*_NiCamera_ConsolePick_GetStartAndDir)(RE::NiCamera *camera, float a_rightAmt, float a_upAmt, NiPoint3 &a_camPosOut, NiPoint3 &a_dirOut);
_NiCamera_ConsolePick_GetStartAndDir NiCamera_ConsolePick_GetStartAndDir_Original = nullptr;
auto NiCamera_ConsolePick_GetStartAndDir_HookLoc = RelocPtr<uintptr_t>(0xCAB37F);
void NiCamera_ConsolePick_GetStartAndDir_Hook(RE::NiCamera *camera, float a_rightAmt, float a_upAmt, NiPoint3 &a_camPosOut, NiPoint3 &a_dirOut)
{
    //_MESSAGE("%.2f %2f", a_rightAmt, a_upAmt);

    a_upAmt *= Config::options.upMultiplier;
    a_rightAmt *= Config::options.rightMultiplier;

    a_upAmt += Config::options.upOffset * Config::options.upMultiplier;
    a_rightAmt += Config::options.rightOffset * Config::options.rightMultiplier;

    NiCamera_ConsolePick_GetStartAndDir_Original(camera, a_rightAmt, a_upAmt, a_camPosOut, a_dirOut);

    a_camPosOut += camera->eyePositions.entries[0] - camera->m_worldTransform.pos;
}

typedef bool(*_CollectPickResults_NiNode)(NiPoint3 *a_start, NiPoint3 *a_dir, void *a3, NiNode *a4);
_CollectPickResults_NiNode CollectPickResults_NiNode_Original = nullptr;
auto CollectPickResults_NiNode_HookLoc = RelocPtr<uintptr_t>(0xCD9131);
bool CollectPickResults_NiNode_Hook(NiPoint3 *a_start, NiPoint3 *a_dir, void *a3, NiNode *a4)
{
    if (a4 == (*g_thePlayer)->unk3F0[PlayerCharacter::Node::kNode_PlayerWorldNode]) {
        return false;
    }

    return CollectPickResults_NiNode_Original(a_start, a_dir, a3, a4);
}



std::uintptr_t Write5Call(std::uintptr_t a_src, std::uintptr_t a_dst)
{
    const auto disp = reinterpret_cast<std::int32_t *>(a_src + 1);
    const auto nextOp = a_src + 5;
    const auto func = nextOp + *disp;

    g_branchTrampoline.Write5Call(a_src, a_dst);

    return func;
}

void DoHooks()
{
    {
        std::uintptr_t originalFunc = Write5Call(NiCamera_ConsolePick_GetStartAndDir_HookLoc.GetUIntPtr(), uintptr_t(NiCamera_ConsolePick_GetStartAndDir_Hook));
        NiCamera_ConsolePick_GetStartAndDir_Original = (_NiCamera_ConsolePick_GetStartAndDir)originalFunc;
    }

    {
        std::uintptr_t originalFunc = Write5Call(CollectPickResults_NiNode_HookLoc.GetUIntPtr(), uintptr_t(CollectPickResults_NiNode_Hook));
        CollectPickResults_NiNode_Original = (_CollectPickResults_NiNode)originalFunc;
    }
}

bool TryHook()
{
    // This should be sized to the actual amount used by your trampoline
    static const size_t TRAMPOLINE_SIZE = 32;

    if (g_trampoline) {
        void *branch = g_trampoline->AllocateFromBranchPool(g_pluginHandle, TRAMPOLINE_SIZE);
        if (!branch) {
            _ERROR("couldn't acquire branch trampoline from SKSE. this is fatal. skipping remainder of init process.");
            return false;
        }

        g_branchTrampoline.SetBase(TRAMPOLINE_SIZE, branch);

        void *local = g_trampoline->AllocateFromLocalPool(g_pluginHandle, TRAMPOLINE_SIZE);
        if (!local) {
            _ERROR("couldn't acquire codegen buffer from SKSE. this is fatal. skipping remainder of init process.");
            return false;
        }

        g_localTrampoline.SetBase(TRAMPOLINE_SIZE, local);
    }
    else {
        if (!g_branchTrampoline.Create(TRAMPOLINE_SIZE)) {
            _ERROR("couldn't create branch trampoline. this is fatal. skipping remainder of init process.");
            return false;
        }
        if (!g_localTrampoline.Create(TRAMPOLINE_SIZE, nullptr))
        {
            _ERROR("couldn't create codegen buffer. this is fatal. skipping remainder of init process.");
            return false;
        }
    }

    DoHooks();
    return true;
}


extern "C" {
    // Listener for SKSE Messages
    void OnSKSEMessage(SKSEMessagingInterface::Message* msg)
    {
        if (msg) {
            if (msg->type == SKSEMessagingInterface::kMessage_PostLoad) {
            }
        }
    }

    bool SKSEPlugin_Query(const SKSEInterface* skse, PluginInfo* info)
    {
        gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim VR\\SKSE\\console_selection_fix.log");
        gLog.SetPrintLevel(IDebugLog::kLevel_Message);
        gLog.SetLogLevel(IDebugLog::kLevel_Message);

        _MESSAGE("Console Selection Fix v%s", VERSION_VERSTRING);

        info->infoVersion = PluginInfo::kInfoVersion;
        info->name = "Console Selection Fix";
        info->version = VERSION_MAJOR;

        g_pluginHandle = skse->GetPluginHandle();

        if (skse->isEditor) {
            _FATALERROR("[FATAL ERROR] Loaded in editor, marking as incompatible!\n");
            return false;
        }
        else if (skse->runtimeVersion != RUNTIME_VR_VERSION_1_4_15) {
            _FATALERROR("[FATAL ERROR] Unsupported runtime version %08X!\n", skse->runtimeVersion);
            return false;
        }

        return true;
    }

    bool SKSEPlugin_Load(const SKSEInterface * skse)
    {	// Called by SKSE to load this plugin
        _MESSAGE("Console Selection Fix loaded");

        if (Config::ReadConfigOptions()) {
            _MESSAGE("Successfully read config parameters");
        }
        else {
            _WARNING("[WARNING] Failed to read config options. Using defaults instead.");
        }

        _MESSAGE("Registering for SKSE messages");
        g_messaging = (SKSEMessagingInterface*)skse->QueryInterface(kInterface_Messaging);
        g_messaging->RegisterListener(g_pluginHandle, "SKSE", OnSKSEMessage);

        g_trampoline = (SKSETrampolineInterface *)skse->QueryInterface(kInterface_Trampoline);
        if (!g_trampoline) {
            _WARNING("Couldn't get trampoline interface");
        }
        if (!TryHook()) {
            _ERROR("[CRITICAL] Failed to perform hooks");
            return false;
        }

        return true;
    }
};
