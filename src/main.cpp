#include "common/IDebugLog.h"  // IDebugLog
#include "skse64_common/SafeWrite.h"
#include "skse64_common/skse_version.h"  // RUNTIME_VERSION
#include "skse64/PluginAPI.h"  // SKSEInterface, PluginInfo
#include "skse64_common/Relocation.h"
#include "skse64_common/BranchTrampoline.h"

#include "skse64/NiNodes.h"
#include "skse64/GameData.h"

#include <DirectXMath.h>

#include <ShlObj.h>  // CSIDL_MYDOCUMENTS

#include "version.h"


// SKSE globals
static PluginHandle	g_pluginHandle = kPluginHandle_Invalid;
static SKSEMessagingInterface *g_messaging = nullptr;
SKSETrampolineInterface *g_trampoline = nullptr;


RelocPtr<float> fWindowCutoutSize_VR(0x1EABDF8);


namespace RE {
    struct ViewData
    {
        DirectX::XMVECTOR m_ViewUp; // 00
        DirectX::XMVECTOR m_ViewRight; // 10
        DirectX::XMVECTOR m_ViewForward; // 20
        DirectX::XMMATRIX m_ViewMat; // 30
        DirectX::XMMATRIX m_ProjMat; // 70
        DirectX::XMMATRIX m_ViewProjMat; // B0
        DirectX::XMMATRIX m_UnknownMat1; // F0 - all 0?
        DirectX::XMMATRIX m_ViewProjMatrixUnjittered; // 130
        DirectX::XMMATRIX m_PreviousViewProjMatrixUnjittered; // 170
        DirectX::XMMATRIX m_ProjMatrixUnjittered; // 1B0
        DirectX::XMMATRIX m_UnknownMat2; // 1F0 - all 0?
        float m_ViewPort[4];// 230 - NiRect<float> { left = 0, right = 1, top = 1, bottom = 0 }
        NiPoint2 m_ViewDepthRange; // 240
        char _pad0[0x8]; // 248
    }; // size == 250

    struct RendererShadowState
    {
        UInt8 unk00[0x3E0 - 0x000];
        ViewData m_CameraData[2]; // 3E0 - size of each is 250
    };

    struct NiCamera : NiAVObject
    {
        float m_aafWorldToCam[4][4];	// 110
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

RelocPtr<RE::RendererShadowState> g_rendererShadowState(0x3180DB0);


NiPoint3 ScreenToWorldDirection(float x_ndc, float y_ndc, const DirectX::XMMATRIX &viewProjectionMatrix) {
    // Step 1: Convert screen coordinates to Normalized Device Coordinates (NDC)
    float z_ndc = 1.0f; // Forward direction

    // Step 2: Define near and far clip space coordinates
    DirectX::XMVECTOR nearPoint = DirectX::XMVectorSet(x_ndc, y_ndc, 0.0f, 1.0f); // z = 0 for near plane
    DirectX::XMVECTOR farPoint = DirectX::XMVectorSet(x_ndc, y_ndc, 1.0f, 1.0f); // z = 1 for far plane

    // Step 3: Invert the View-Projection matrix
    DirectX::XMMATRIX invVP = DirectX::XMMatrixInverse(nullptr, viewProjectionMatrix);

    // Step 4: Unproject to world space
    DirectX::XMVECTOR nearWorld = XMVector4Transform(nearPoint, invVP);
    DirectX::XMVECTOR farWorld = XMVector4Transform(farPoint, invVP);

    // Step 5: Perspective divide to convert from homogeneous coordinates
    nearWorld = DirectX::XMVectorScale(nearWorld, 1.0f / DirectX::XMVectorGetW(nearWorld));
    farWorld = DirectX::XMVectorScale(farWorld, 1.0f / DirectX::XMVectorGetW(farWorld));

    // Step 6: Get the direction vector and normalize it
    DirectX::XMVECTOR direction = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(farWorld, nearWorld));

    return { direction.m128_f32[0], direction.m128_f32[1], direction.m128_f32[2] };
}


typedef bool (*_NiCamera_GetStartAndDirFromScreenRectCoords)(RE::NiCamera *camera, int midpointX, int midpointY, NiPoint3 &a_camPosOut, NiPoint3 &a_dirOut, float uiWidth, float uiHeight);
_NiCamera_GetStartAndDirFromScreenRectCoords NiCamera_GetStartAndDirFromScreenRectCoords_Original = nullptr;
auto NiCamera_GetStartAndDirFromScreenRectCoords_HookLoc = RelocPtr<uintptr_t>(0x885574);
bool NiCamera_GetStartAndDirFromScreenRectCoords_Hook(RE::NiCamera *camera, int mouseX, int mouseY, NiPoint3 &a_camPosOut, NiPoint3 &a_dirOut, float uiWidth, float uiHeight)
{
    if (!NiCamera_GetStartAndDirFromScreenRectCoords_Original(camera, mouseX, mouseY, a_camPosOut, a_dirOut, uiWidth, uiHeight)) {
        // It checks stuff like if the mouse is outside the port, so we don't need to re-check that here
        return false;
    }

    float xPercent = mouseX / uiWidth;
    float yPercent = 1.f - (mouseY / uiHeight);

    _MESSAGE("%.3f %.3f", xPercent, yPercent);

    RE::RendererShadowState *rendererShadowState = g_rendererShadowState;

    float rightAmount = (xPercent - camera->port.m_left) / (camera->port.m_right - camera->port.m_left);
    float upAmount = (yPercent - camera->port.m_bottom) / (camera->port.m_top - camera->port.m_bottom);
    rightAmount = 2.f * rightAmount - 1.f;
    upAmount = 2.f * upAmount - 1.f;
    rightAmount *= (1.f - 4.f * *fWindowCutoutSize_VR);
    upAmount *= (1.f - 2.f * *fWindowCutoutSize_VR);

    _MESSAGE("%.3f %.3f", rightAmount, upAmount);

    a_dirOut = ScreenToWorldDirection(rightAmount, upAmount, *(DirectX::XMMATRIX *)&rendererShadowState->m_CameraData[0].m_ViewProjMatrixUnjittered);

    a_camPosOut = camera->eyePositions.entries[0];

    return true;
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
        std::uintptr_t originalFunc = Write5Call(NiCamera_GetStartAndDirFromScreenRectCoords_HookLoc.GetUIntPtr(), uintptr_t(NiCamera_GetStartAndDirFromScreenRectCoords_Hook));
        NiCamera_GetStartAndDirFromScreenRectCoords_Original = (_NiCamera_GetStartAndDirFromScreenRectCoords)originalFunc;
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
