#include "Bridge.h"
#include "Controls.h"
#include "Support.h"
#include <openxr.h>
#include <openxr_loader_negotiation.h>
#include <cstring>
#include <mutex>

namespace seated {
namespace {
struct State {
    std::mutex mutex;
    bool active = false;
    bool enabled = true;
    unsigned holdMs = 550;
    float chestMultiplier = 1.75f;
    ComfortConfig comfort;
    bool crouchEnabled = true, hapticsEnabled = true;
    XrInstance instance = XR_NULL_HANDLE;
    XrActionSet gameplay = XR_NULL_HANDLE;
    XrAction secondary = XR_NULL_HANDLE;
    XrAction primary = XR_NULL_HANDLE, stick = XR_NULL_HANDLE, haptic = XR_NULL_HANDLE;
    XrPath left = XR_NULL_PATH;
    XrPath right = XR_NULL_PATH;
    PFN_xrGetInstanceProcAddr getProc = nullptr;
    PFN_xrDestroyInstance destroy = nullptr;
    PFN_xrCreateActionSet createSet = nullptr;
    PFN_xrCreateAction createAction = nullptr;
    PFN_xrSyncActions sync = nullptr;
    PFN_xrGetActionStateBoolean getBoolean = nullptr;
    PFN_xrGetActionStateVector2f getVector = nullptr;
    PFN_xrApplyHapticFeedback applyHaptic = nullptr;
    PFN_xrStopHapticFeedback stopHaptic = nullptr;
    TapHold button;
    ToggleCrouch crouch;
    bool movementValid = false, jumpOutput = false, jumpChanged = false, vectorChanged = false;
    XrVector2f stickOutput{};
    ULONGLONG cueUntil[2] = {};
    bool valid = false, output = false, changed = false;
    XrTime lastChangeTime = 0;
    GameVar ui{"tf2vrsc_ui_ready"};
    GameVar player{"tf2vrsc_player_ready"};
    GameVar heartbeat{"tf2vrsc_heartbeat"};
    int previousHeartbeat = -1;
    ULONGLONG heartbeatAt = 0;
};
State s;

bool IsTargetProcess() {
    wchar_t path[32768] = {};
    if (!GetModuleFileNameW(nullptr, path, 32768)) return false;
    std::wstring name(path);
    name = name.substr(name.find_last_of(L"\\/") + 1);
    return _wcsicmp(name.c_str(), L"Titanfall2VRLauncher.exe") == 0 || _wcsicmp(name.c_str(), L"Titanfall2.exe") == 0;
}

void Configure() {
    const auto path = ModuleDirectory(reinterpret_cast<const void*>(&Configure)) + L"\\TF2VRSeatedControls.ini";
    s.enabled = GetPrivateProfileIntW(L"controls", L"enabled", 1, path.c_str()) != 0;
    s.holdMs = static_cast<unsigned>(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"controls", L"menu_hold_ms", 550, path.c_str())), 250, 1500));
    const int percent = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"grenade", L"pickup_radius_percent", 175, path.c_str())), 100, 250);
    s.chestMultiplier = static_cast<float>(percent) / 100.0f;
    s.crouchEnabled = GetPrivateProfileIntW(L"controls", L"toggle_crouch", 1, path.c_str()) != 0;
    s.hapticsEnabled = GetPrivateProfileIntW(L"controls", L"pickup_haptics", 1, path.c_str()) != 0;
    s.comfort.rightSecondaryHolster = GetPrivateProfileIntW(L"holsters", L"secondary_right", 1, path.c_str()) != 0;
    s.comfort.frontPouch = GetPrivateProfileIntW(L"reload", L"front_pouch", 1, path.c_str()) != 0;
    auto setting = [&](const wchar_t* key, int fallback, int low, int high) {
        return std::clamp(static_cast<int>(GetPrivateProfileIntW(L"reload", key, fallback, path.c_str())), low, high);
    };
    s.comfort.pouchForward = setting(L"pouch_forward_cm", 30, 15, 50) / 2.54f;
    s.comfort.pouchDrop = setting(L"pouch_drop_cm", 30, 15, 50) / 2.54f;
    s.comfort.pouchRadius = setting(L"pouch_radius_cm", 12, 8, 18) / 2.54f;
    s.comfort.reloadDistance = setting(L"distance_percent", 150, 100, 200) / 100.0f;
    s.comfort.reloadAngle = setting(L"angle_percent", 125, 100, 150) / 100.0f;
}

template<class T> bool Resolve(const char* name, T& fn) {
    PFN_xrVoidFunction result = nullptr;
    if (XR_FAILED(s.getProc(s.instance, name, &result)) || !result) return false;
    fn = reinterpret_cast<T>(result);
    return true;
}

XrResult XRAPI_CALL Destroy(XrInstance instance) {
    BridgeShutdown();
    const auto result = s.destroy(instance);
    std::lock_guard lock(s.mutex);
    s.button.Cancel();
    s.valid = false;
    s.secondary = XR_NULL_HANDLE;
    s.primary = s.stick = s.haptic = XR_NULL_HANDLE;
    s.crouch.Reset(); s.movementValid = false;
    s.cueUntil[0] = s.cueUntil[1] = 0;
    s.gameplay = XR_NULL_HANDLE;
    s.active = false;
    s.previousHeartbeat = -1;
    return result;
}

XrResult XRAPI_CALL CreateSet(XrInstance instance, const XrActionSetCreateInfo* info, XrActionSet* actionSet) {
    const auto result = s.createSet(instance, info, actionSet);
    if (XR_SUCCEEDED(result) && info && actionSet && std::strcmp(info->actionSetName, "gameplay") == 0) {
        std::lock_guard lock(s.mutex);
        s.gameplay = *actionSet;
    }
    return result;
}

XrResult XRAPI_CALL CreateAction(XrActionSet actionSet, const XrActionCreateInfo* info, XrAction* action) {
    const auto result = s.createAction(actionSet, info, action);
    if (XR_SUCCEEDED(result) && info && action && actionSet == s.gameplay) {
        std::lock_guard lock(s.mutex);
        if (info->actionType == XR_ACTION_TYPE_BOOLEAN_INPUT) {
            if (std::strcmp(info->actionName, "secondary") == 0) s.secondary = *action;
            if (std::strcmp(info->actionName, "primary") == 0) s.primary = *action;
        }
        if (info->actionType == XR_ACTION_TYPE_VECTOR2F_INPUT && std::strcmp(info->actionName, "stick") == 0) s.stick = *action;
        if (info->actionType == XR_ACTION_TYPE_VIBRATION_OUTPUT && std::strcmp(info->actionName, "haptic") == 0) s.haptic = *action;
    }
    return result;
}

XrResult XRAPI_CALL Sync(XrSession session, const XrActionsSyncInfo* info) {
    const auto result = s.sync(session, info);
    if (result != XR_SUCCESS) {
        std::lock_guard lock(s.mutex);
        s.button.Cancel(); s.valid = false;
        s.crouch.Reset(); s.movementValid = false;
        s.cueUntil[0] = s.cueUntil[1] = 0;
        BridgeGameplay(false);
        BridgeCancel();
        return result;
    }
    if (!s.enabled || !BridgeInitialize(s.chestMultiplier, s.comfort)) return result;
    std::lock_guard lock(s.mutex);
    if (!s.secondary) return result;
    XrActionStateGetInfo get{};
    get.type = XR_TYPE_ACTION_STATE_GET_INFO;
    get.action = s.secondary;
    get.subactionPath = s.left;
    XrActionStateBoolean raw{};
    raw.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    if (XR_FAILED(s.getBoolean(session, &get, &raw)) || !raw.isActive) {
        s.valid = false; s.button.Cancel(); BridgeCancel();
        s.crouch.Reset(); s.movementValid = false; BridgeGameplay(false);
        s.cueUntil[0] = s.cueUntil[1] = 0;
        return result;
    }
    const auto now = GetTickCount64();
    int ui = 0, player = 0, heartbeat = 0;
    const bool known = s.ui.Read(ui) && s.player.Read(player) && s.heartbeat.Read(heartbeat);
    if (known && heartbeat != s.previousHeartbeat) {
        s.previousHeartbeat = heartbeat;
        s.heartbeatAt = now;
    }
    const bool gameplay = BridgeGameplay(known && ui == 1 && player == 1 && now - s.heartbeatAt < 250);
    const bool swapAllowed = gameplay && BridgeCanSwap();
    if (!swapAllowed) BridgeCancel();
    const auto output = s.button.Update(raw.currentState != XR_FALSE, swapAllowed, now, s.holdMs);
    s.changed = output.original != s.output;
    if (s.changed) s.lastChangeTime = raw.lastChangeTime;
    s.output = output.original;
    s.valid = true;
    if (output.swap) BridgeRequestSwap();
    s.movementValid = false;
    if (s.primary && s.stick && s.getVector && s.crouchEnabled) {
        get.subactionPath = s.right; get.action = s.primary;
        XrActionStateBoolean jump{}; jump.type = XR_TYPE_ACTION_STATE_BOOLEAN;
        XrActionStateVector2f stick{}; stick.type = XR_TYPE_ACTION_STATE_VECTOR2F;
        const bool jumpKnown = XR_SUCCEEDED(s.getBoolean(session, &get, &jump)) && jump.isActive;
        get.action = s.stick;
        const bool stickKnown = XR_SUCCEEDED(s.getVector(session, &get, &stick)) && stick.isActive;
        const bool enabled = gameplay && jumpKnown && stickKnown;
        const auto movement = s.crouch.Update(stick.currentState.y, jump.currentState != XR_FALSE, enabled, now);
        s.jumpChanged = s.jumpOutput != movement.jump;
        s.vectorChanged = s.stickOutput.x != stick.currentState.x || s.stickOutput.y != movement.y;
        s.jumpOutput = movement.jump;
        s.stickOutput = {stick.currentState.x, movement.y};
        s.movementValid = enabled;
    } else s.crouch.Reset();
    const unsigned cues = BridgeCues();
    if (gameplay && s.hapticsEnabled && s.haptic && s.applyHaptic) {
        for (int hand = 0; hand < 2; ++hand) if (cues & (1u << hand)) {
            s.cueUntil[hand] = now + 45;
            XrHapticActionInfo action{}; action.type = XR_TYPE_HAPTIC_ACTION_INFO;
            action.action = s.haptic; action.subactionPath = hand ? s.right : s.left;
            XrHapticVibration pulse{}; pulse.type = XR_TYPE_HAPTIC_VIBRATION;
            pulse.amplitude = .3f; pulse.frequency = XR_FREQUENCY_UNSPECIFIED; pulse.duration = 45000000;
            s.applyHaptic(session, &action, reinterpret_cast<const XrHapticBaseHeader*>(&pulse));
        }
    } else s.cueUntil[0] = s.cueUntil[1] = 0;
    return result;
}

XrResult XRAPI_CALL GetBoolean(XrSession session, const XrActionStateGetInfo* info, XrActionStateBoolean* state) {
    const auto result = s.getBoolean(session, info, state);
    if (XR_FAILED(result) || !info || !state) return result;
    std::lock_guard lock(s.mutex);
    if (s.valid && info->action == s.secondary && info->subactionPath == s.left && state->isActive) {
        state->currentState = s.output ? XR_TRUE : XR_FALSE;
        state->changedSinceLastSync = s.changed ? XR_TRUE : XR_FALSE;
        if (s.changed) state->lastChangeTime = s.lastChangeTime;
    }
    if (s.movementValid && info->action == s.primary && info->subactionPath == s.right && state->isActive) {
        state->currentState = s.jumpOutput;
        state->changedSinceLastSync = s.jumpChanged;
    }
    return result;
}

XrResult XRAPI_CALL GetVector(XrSession session, const XrActionStateGetInfo* info, XrActionStateVector2f* state) {
    const auto result = s.getVector(session, info, state);
    if (XR_FAILED(result) || !info || !state) return result;
    std::lock_guard lock(s.mutex);
    if (s.movementValid && info->action == s.stick && info->subactionPath == s.right && state->isActive) {
        state->currentState = s.stickOutput;
        state->changedSinceLastSync = s.vectorChanged;
    }
    return result;
}

// The core submits its own haptics later in the frame. Merge our short cue so
// that submission cannot erase it; native feedback stronger than ours wins.
XrResult XRAPI_CALL ApplyHaptic(XrSession session, const XrHapticActionInfo* info, const XrHapticBaseHeader* feedback) {
    std::lock_guard lock(s.mutex);
    if (info && feedback && info->action == s.haptic && feedback->type == XR_TYPE_HAPTIC_VIBRATION) {
        const int hand = info->subactionPath == s.left ? 0 : info->subactionPath == s.right ? 1 : -1;
        const auto now = GetTickCount64();
        if (hand >= 0 && s.cueUntil[hand] > now) {
            auto pulse = *reinterpret_cast<const XrHapticVibration*>(feedback);
            pulse.amplitude = std::max(pulse.amplitude, .3f);
            pulse.duration = std::max(pulse.duration, static_cast<XrDuration>((s.cueUntil[hand] - now) * 1000000));
            return s.applyHaptic(session, info, reinterpret_cast<const XrHapticBaseHeader*>(&pulse));
        }
    }
    return s.applyHaptic(session, info, feedback);
}
XrResult XRAPI_CALL StopHaptic(XrSession session, const XrHapticActionInfo* info) {
    std::lock_guard lock(s.mutex);
    const int hand = info && info->action == s.haptic ? (info->subactionPath == s.left ? 0 : info->subactionPath == s.right ? 1 : -1) : -1;
    const auto now = GetTickCount64();
    if (hand >= 0 && s.cueUntil[hand] > now) {
        XrHapticVibration pulse{}; pulse.type = XR_TYPE_HAPTIC_VIBRATION;
        pulse.amplitude = .3f; pulse.frequency = XR_FREQUENCY_UNSPECIFIED;
        pulse.duration = static_cast<XrDuration>((s.cueUntil[hand] - now) * 1000000);
        return s.applyHaptic(session, info, reinterpret_cast<const XrHapticBaseHeader*>(&pulse));
    }
    return s.stopHaptic(session, info);
}

XrResult XRAPI_CALL GetProc(XrInstance instance, const char* name, PFN_xrVoidFunction* fn) {
    if (!name || !fn) return XR_ERROR_VALIDATION_FAILURE;
    if (s.active) {
#define HOOK(api, function) if (std::strcmp(name, api) == 0) { *fn = reinterpret_cast<PFN_xrVoidFunction>(&function); return XR_SUCCESS; }
        HOOK("xrGetInstanceProcAddr", GetProc)
        HOOK("xrDestroyInstance", Destroy)
        HOOK("xrCreateActionSet", CreateSet)
        HOOK("xrCreateAction", CreateAction)
        HOOK("xrSyncActions", Sync)
        HOOK("xrGetActionStateBoolean", GetBoolean)
        if (s.getVector) { HOOK("xrGetActionStateVector2f", GetVector) }
        if (s.applyHaptic) { HOOK("xrApplyHapticFeedback", ApplyHaptic) }
        if (s.stopHaptic) { HOOK("xrStopHapticFeedback", StopHaptic) }
#undef HOOK
    }
    if (!s.getProc) { *fn = nullptr; return XR_ERROR_HANDLE_INVALID; }
    return s.getProc(instance, name, fn);
}

XrResult XRAPI_CALL CreateInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layer, XrInstance* instance) {
    if (!layer || !layer->nextInfo || !layer->nextInfo->nextGetInstanceProcAddr ||
        !layer->nextInfo->nextCreateApiLayerInstance || !instance) return XR_ERROR_INITIALIZATION_FAILED;
    XrApiLayerCreateInfo next = *layer;
    next.nextInfo = layer->nextInfo->next;
    const auto result = layer->nextInfo->nextCreateApiLayerInstance(info, &next, instance);
    if (XR_FAILED(result)) return result;
    s.instance = *instance;
    s.getProc = layer->nextInfo->nextGetInstanceProcAddr;
    s.active = false;
    if (!IsTargetProcess()) return result;
    Configure();
    PFN_xrStringToPath stringToPath = nullptr;
    if (!Resolve("xrDestroyInstance", s.destroy) || !Resolve("xrCreateActionSet", s.createSet) ||
        !Resolve("xrCreateAction", s.createAction) || !Resolve("xrSyncActions", s.sync) ||
        !Resolve("xrGetActionStateBoolean", s.getBoolean) || !Resolve("xrStringToPath", stringToPath)) return result;
    if (XR_FAILED(stringToPath(s.instance, "/user/hand/left", &s.left))) return result;
    if (XR_FAILED(stringToPath(s.instance, "/user/hand/right", &s.right))) return result;
    Resolve("xrGetActionStateVector2f", s.getVector);
    Resolve("xrApplyHapticFeedback", s.applyHaptic);
    Resolve("xrStopHapticFeedback", s.stopHaptic);
    s.active = true;
    Log("OpenXR layer loaded; hold Y %u ms for original menu action, grenade radius %g%%.", s.holdMs, s.chestMultiplier * 100);
    return result;
}
}
}

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader, const char*, XrNegotiateApiLayerRequest* request) {
    if (!loader || !request || loader->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loader->structVersion != XR_LOADER_INFO_STRUCT_VERSION || loader->structSize != sizeof(*loader) ||
        request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        request->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION || request->structSize != sizeof(*request) ||
        loader->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loader->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION) return XR_ERROR_INITIALIZATION_FAILED;
    const auto version = std::min<XrVersion>(XR_CURRENT_API_VERSION, loader->maxApiVersion);
    if (version < loader->minApiVersion) return XR_ERROR_INITIALIZATION_FAILED;
    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion = version;
    request->getInstanceProcAddr = seated::GetProc;
    request->createApiLayerInstance = seated::CreateInstance;
    return XR_SUCCESS;
}
