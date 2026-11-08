// Exercise the real OpenXR entry points with a fake runtime and game context.
// No headset, registry changes or game process is used by this test.
#include "../src/layer/Layer.cpp"
#include <cstdio>
#include <cstdlib>

namespace {
bool pressed = false, active = true, focused = true, available = true;
bool uiReady = true, playerReady = true, canSwap = true, varsAvailable = true;
int heartbeat = 0, swaps = 0, checks = 0;
bool jumpPressed = false, cueGameplay = false;
unsigned cueBits = 0;
XrVector2f rawStick{};
int hapticCalls = 0, stopCalls = 0;
float hapticAmplitude = 0;
XrPath hapticHand = XR_NULL_PATH;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
XrResult XRAPI_CALL FakeDestroy(XrInstance) { return XR_SUCCESS; }
XrResult XRAPI_CALL FakeSet(XrInstance, const XrActionSetCreateInfo*, XrActionSet* set) {
    *set = reinterpret_cast<XrActionSet>(1); return XR_SUCCESS;
}
XrResult XRAPI_CALL FakeAction(XrActionSet, const XrActionCreateInfo* info, XrAction* action) {
    const int id = !std::strcmp(info->actionName,"primary") ? 3 : !std::strcmp(info->actionName,"stick") ? 4 : !std::strcmp(info->actionName,"haptic") ? 5 : 2;
    *action = reinterpret_cast<XrAction>(static_cast<std::uintptr_t>(id)); return XR_SUCCESS;
}
XrResult XRAPI_CALL FakeSync(XrSession, const XrActionsSyncInfo*) {
    ++heartbeat; return focused ? XR_SUCCESS : XR_SESSION_NOT_FOCUSED;
}
XrResult XRAPI_CALL FakeBoolean(XrSession, const XrActionStateGetInfo* info, XrActionStateBoolean* state) {
    state->isActive = active;
    state->currentState = info->action == reinterpret_cast<XrAction>(3) ? jumpPressed : pressed;
    state->changedSinceLastSync = XR_TRUE;
    state->lastChangeTime = heartbeat * 100;
    return XR_SUCCESS;
}
XrResult XRAPI_CALL FakeVector(XrSession, const XrActionStateGetInfo*, XrActionStateVector2f* state) {
    state->isActive = active; state->currentState = rawStick; return XR_SUCCESS;
}
XrResult XRAPI_CALL FakeHaptic(XrSession, const XrHapticActionInfo* info, const XrHapticBaseHeader* pulse) {
    ++hapticCalls; hapticHand = info->subactionPath;
    hapticAmplitude = reinterpret_cast<const XrHapticVibration*>(pulse)->amplitude; return XR_SUCCESS;
}
XrResult XRAPI_CALL FakeStop(XrSession, const XrHapticActionInfo*) { ++stopCalls; return XR_SUCCESS; }
XrResult XRAPI_CALL FakePath(XrInstance, const char* name, XrPath* path) { *path = std::strstr(name,"right") ? 2 : 1; return XR_SUCCESS; }
XrResult XRAPI_CALL FakeProc(XrInstance, const char* name, PFN_xrVoidFunction* fn) {
#define FAKE(api, function) if (std::strcmp(name, api) == 0) { *fn = reinterpret_cast<PFN_xrVoidFunction>(&function); return XR_SUCCESS; }
    FAKE("xrDestroyInstance", FakeDestroy)
    FAKE("xrCreateActionSet", FakeSet)
    FAKE("xrCreateAction", FakeAction)
    FAKE("xrSyncActions", FakeSync)
    FAKE("xrGetActionStateBoolean", FakeBoolean)
    FAKE("xrStringToPath", FakePath)
    FAKE("xrGetActionStateVector2f", FakeVector)
    FAKE("xrApplyHapticFeedback", FakeHaptic)
    FAKE("xrStopHapticFeedback", FakeStop)
#undef FAKE
    *fn = nullptr; return XR_ERROR_FUNCTION_UNSUPPORTED;
}
XrResult XRAPI_CALL FakeInstance(const XrInstanceCreateInfo*, const XrApiLayerCreateInfo*, XrInstance* instance) {
    *instance = reinterpret_cast<XrInstance>(3); return XR_SUCCESS;
}
}
namespace seated {
std::wstring ModuleDirectory(const void*) { return L"C:\\nonexistent-test-config"; }
void Log(const char*, ...) {}
bool BridgeInitialize(float, const ComfortConfig&) { return available; }
bool BridgeGameplay(bool active) { cueGameplay = active; return active; }
unsigned BridgeCues() { const auto bits = cueBits; cueBits = 0; return bits; }
bool BridgeReady() { return available; }
bool BridgeCanSwap() { return canSwap; }
void BridgeRequestSwap() { ++swaps; }
void BridgeCancel() {}
void BridgeShutdown() {}
bool GameVar::Read(int& value) {
    if (!varsAvailable) return false;
    if (std::strcmp(name_, "tf2vrsc_ui_ready") == 0) value = uiReady;
    else if (std::strcmp(name_, "tf2vrsc_player_ready") == 0) value = playerReady;
    else value = heartbeat;
    return true;
}
}

int main() {
    using namespace seated;
    XrNegotiateLoaderInfo loader{};
    loader.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    loader.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
    loader.structSize = sizeof(loader);
    loader.minInterfaceVersion = loader.maxInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    loader.minApiVersion = XR_MAKE_VERSION(1,0,0);
    loader.maxApiVersion = XR_CURRENT_API_VERSION;
    XrNegotiateApiLayerRequest request{};
    request.structType = XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST;
    request.structVersion = XR_API_LAYER_INFO_STRUCT_VERSION;
    request.structSize = sizeof(request);
    Check(xrNegotiateLoaderApiLayerInterface(&loader, nullptr, &request) == XR_SUCCESS, "loader negotiation");
    XrApiLayerNextInfo next{};
    next.nextGetInstanceProcAddr = FakeProc;
    next.nextCreateApiLayerInstance = FakeInstance;
    XrApiLayerCreateInfo layer{}; layer.nextInfo = &next;
    XrInstance instance;
    Check(request.createApiLayerInstance(nullptr, &layer, &instance) == XR_SUCCESS, "instance chains to runtime");
    Check(s.active, "layer recognizes launcher process");
    XrActionSetCreateInfo setInfo{}; setInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    std::strcpy(setInfo.actionSetName, "gameplay");
    XrActionSet set;
    CreateSet(instance, &setInfo, &set);
    XrActionCreateInfo actionInfo{}; actionInfo.type = XR_TYPE_ACTION_CREATE_INFO;
    actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    std::strcpy(actionInfo.actionName, "secondary");
    XrAction action;
    CreateAction(set, &actionInfo, &action);
    XrActionStateGetInfo get{}; get.type = XR_TYPE_ACTION_STATE_GET_INFO;
    get.action = action; get.subactionPath = 1;
    XrActionStateBoolean state{}; state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    auto frame = [&]() { Sync(XR_NULL_HANDLE, nullptr); GetBoolean(XR_NULL_HANDLE, &get, &state); };

    pressed = true; frame();
    Check(!state.currentState && swaps == 0, "short press does not open menu on down");
    pressed = false; frame();
    Check(!state.currentState && swaps == 1, "release requests exactly one swap");
    frame(); Check(swaps == 1, "no repeated swap");
    pressed = true; frame();
    get.subactionPath = 2;
    GetBoolean(XR_NULL_HANDLE, &get, &state);
    Check(state.currentState, "right secondary button unchanged");
    get.subactionPath = 1;
    focused = false; frame();
    focused = true; frame();
    Check(!state.currentState && swaps == 1, "held button after focus loss stays suppressed");
    pressed = false; frame(); Check(swaps == 1, "focus-loss release cannot swap");
    uiReady = false; pressed = true; frame();
    Check(state.currentState, "menus pass original Y immediately");
    pressed = false; frame(); uiReady = true;
    canSwap = false; pressed = true; frame();
    Check(state.currentState, "dialogue and empty hands pass original Y immediately");
    pressed = false; frame(); canSwap = true;
    varsAvailable = false; pressed = true; frame();
    Check(state.currentState, "missing context script passes original Y");
    pressed = false; frame(); varsAvailable = true;
    s.holdMs = 1;
    pressed = true; frame(); Sleep(20); frame();
    Check(state.currentState && swaps == 1, "long hold forwards original action without swapping");
    uiReady = false; frame();
    Check(state.currentState, "opened menu does not retrigger long press");
    pressed = false; frame();
    Check(!state.currentState && swaps == 1, "long release never swaps");
    for (const char* name : {"primary", "stick", "haptic"}) {
        std::strcpy(actionInfo.actionName, name);
        actionInfo.actionType = !std::strcmp(name,"primary") ? XR_ACTION_TYPE_BOOLEAN_INPUT : !std::strcmp(name,"stick") ? XR_ACTION_TYPE_VECTOR2F_INPUT : XR_ACTION_TYPE_VIBRATION_OUTPUT;
        XrAction extra; CreateAction(set, &actionInfo, &extra);
    }
    uiReady = true; frame();
    XrActionStateGetInfo vectorGet{}; vectorGet.type = XR_TYPE_ACTION_STATE_GET_INFO;
    vectorGet.action = s.stick; vectorGet.subactionPath = s.right;
    XrActionStateVector2f vector{}; vector.type = XR_TYPE_ACTION_STATE_VECTOR2F;
    rawStick = {.6f,-1}; frame(); rawStick.y = 0; frame();
    GetVector(XR_NULL_HANDLE,&vectorGet,&vector);
    Check(vector.currentState.y == -1 && vector.currentState.x == .6f, "crouch latches while turning remains available");
    vectorGet.subactionPath = s.left; GetVector(XR_NULL_HANDLE,&vectorGet,&vector);
    Check(vector.currentState.y == 0, "left locomotion stick is not changed");
    vectorGet.subactionPath = s.right;
    jumpPressed = true; frame();
    XrActionStateGetInfo jumpGet{}; jumpGet.type = XR_TYPE_ACTION_STATE_GET_INFO;
    jumpGet.action = s.primary; jumpGet.subactionPath = s.right;
    GetBoolean(XR_NULL_HANDLE,&jumpGet,&state); GetVector(XR_NULL_HANDLE,&vectorGet,&vector);
    Check(!state.currentState && vector.currentState.y == 0, "jump uncrouches before buffered jump");
    jumpPressed = false; Sleep(170); frame(); GetBoolean(XR_NULL_HANDLE,&jumpGet,&state);
    Check(state.currentState, "brief jump tap is delivered after standing");
    uiReady = false; frame(); GetBoolean(XR_NULL_HANDLE,&jumpGet,&state);
    Check(!state.currentState, "opening menu cancels buffered jump");
    uiReady = true; cueBits = 1; frame();
    Check(cueGameplay && hapticCalls == 1 && hapticHand == s.left, "pickup cue reaches correct controller");
    XrHapticActionInfo hapticInfo{}; hapticInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
    hapticInfo.action = s.haptic; hapticInfo.subactionPath = s.left;
    XrHapticVibration pulse{}; pulse.type = XR_TYPE_HAPTIC_VIBRATION; pulse.amplitude = .1f;
    ApplyHaptic(XR_NULL_HANDLE,&hapticInfo,reinterpret_cast<XrHapticBaseHeader*>(&pulse));
    Check(hapticAmplitude == .3f, "native submission cannot erase active pickup cue");
    pulse.amplitude = .8f; ApplyHaptic(XR_NULL_HANDLE,&hapticInfo,reinterpret_cast<XrHapticBaseHeader*>(&pulse));
    Check(hapticAmplitude == .8f, "stronger native haptics preserved");
    StopHaptic(XR_NULL_HANDLE,&hapticInfo);
    Check(stopCalls == 0 && hapticAmplitude == .3f, "cue survives native stop until expiry");
    focused = false; frame(); StopHaptic(XR_NULL_HANDLE,&hapticInfo);
    Check(stopCalls == 1 && !cueGameplay, "focus loss cancels cue merging");
    focused = true;
    Destroy(instance);
    available = false;
    request.createApiLayerInstance(nullptr, &layer, &instance);
    std::strcpy(actionInfo.actionName,"secondary"); actionInfo.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    CreateSet(instance, &setInfo, &set); CreateAction(set, &actionInfo, &action);
    pressed = true; frame();
    Check(state.currentState, "unsupported core build retains original Y");
    Destroy(instance);
    std::printf("PASS: %d OpenXR routing checks\n", checks);
}
