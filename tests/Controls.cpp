#include "Controls.h"
#include "Comfort.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static int checks = 0;
static void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    using namespace seated;
    TapHold button;
    auto v = button.Update(true, true, 100, 550);
    Check(!v.original && !v.swap, "Y down waits for tap or hold");
    v = button.Update(false, true, 180, 550);
    Check(v.swap && !v.original, "tap swaps on release without menu");
    Check(!button.Update(false, true, 190, 550).swap, "tap cannot repeat");
    button.Update(true, true, 1000, 550);
    Check(!button.Update(true, true, 1549, 550).original, "no early menu");
    v = button.Update(true, true, 1550, 550);
    Check(v.original && !v.swap, "hold opens original menu at threshold");
    Check(button.Update(true, false, 1600, 550).original, "menu transition preserves long press");
    v = button.Update(false, false, 1650, 550);
    Check(!v.original && !v.swap, "hold release cannot swap");
    Check(button.Update(true, false, 1700, 550).original, "menus use original Y immediately");
    Check(button.Update(true, true, 1750, 550).original, "press begun in menu stays original");
    Check(!button.Update(false, true, 1800, 550).swap, "menu release cannot swap");
    button.Update(true, true, 2000, 550);
    Check(!button.Update(true, false, 2020, 550).original, "context loss cancels pending tap");
    Check(!button.Update(false, true, 2050, 550).swap, "cancelled tap cannot reappear");
    button.Update(true, true, 2100, 550); button.Cancel();
    Check(!button.Update(true, true, 2800, 550).original, "focus return requires release");
    Check(!button.Update(false, true, 2900, 550).swap, "focus-loss release cannot swap");
    button.Update(true, true, 3000, 550);
    v = button.Update(false, true, 3600, 550);
    Check(v.original && !v.swap, "long press across a frame stall produces menu, never swap");
    Check(!button.Update(false, true, 3610, 550).original, "stalled long press releases next frame");

    const float chest[4] = {0, 0, 0, 0};
    float hand[4] = {7, 0, 0, 0};
    constexpr auto empty = std::uint64_t{0xffffffffu};
    Check(Grenade(ExpandChest(empty, hand, chest, 16, 8.2f)), "near chest gets grenade");
    Check(!Grenade(ExpandChest(empty, hand, chest, 0, 8.2f)), "no ammo/availability cannot become grenade");
    Check(Slot(ExpandChest(1, hand, chest, 18, 8.2f)) == 1, "existing secondary target keeps priority");
    hand[0] = 20;
    Check(!Grenade(ExpandChest(empty, hand, chest, 16, 8.2f)), "hands away from chest unchanged");
    hand[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!Grenade(ExpandChest(empty, hand, chest, 16, 8.2f)), "invalid pose cannot grab");
    ToggleCrouch crouch;
    crouch.Update(0, false, true, 100);
    Check(crouch.Update(-1, false, true, 110).y == -1, "stick down toggles crouch on");
    Check(crouch.Update(0, false, true, 120).y == -1, "crouch persists after stick release");
    Check(crouch.Update(-1, false, true, 130).y == 0, "second down toggles crouch off");
    Check(crouch.Update(-1, false, true, 140).y == 0, "held stick cannot toggle repeatedly");
    crouch.Update(0, false, true, 150); crouch.Update(-1, false, true, 160);
    auto movement = crouch.Update(0, true, true, 170);
    Check(movement.y == 0 && !movement.jump, "jump first releases latched crouch");
    Check(!crouch.Update(0, false, true, 200).jump, "jump waits briefly for standing");
    Check(crouch.Update(0, false, true, 320).jump, "short jump tap survives the stand delay");
    crouch.Update(0, false, true, 381);
    Check(!crouch.Update(0, false, true, 382).jump, "buffered jump releases");
    crouch.Update(-1, false, true, 400);
    Check(crouch.Update(.5f, false, false, 410).y == .5f, "menu cancels crouch and passes stick");
    Check(crouch.Update(-1, false, true, 420).y == 0, "context return requires stick neutral");
    EntryCue cue;
    Check(cue.Update(true, 1), "cue fires on zone entry");
    Check(!cue.Update(true, 2), "cue does not buzz continuously");
    cue.Update(false, 10);
    Check(!cue.Update(true, 20), "boundary jitter does not retrigger");
    cue.Update(false, 30); cue.Update(false, 180);
    Check(cue.Update(true, 181), "leaving and reentering rearms cue");
    ComfortConfig comfort;
    float holsters[15] = {0,7,0, 0,-7,0, 0,0,0, 0,0,0, 1,0,0};
    float front[4] = {30/2.54f,18/2.54f,-30/2.54f,0};
    Check(FrontPouch(holsters, front, comfort), "front right pouch reachable");
    front[1] *= -1;
    Check(FrontPouch(holsters, front, comfort), "front left pouch reachable");
    front[0] = -30;
    Check(!FrontPouch(holsters, front, comfort), "front pouch excludes behind-chair hand");
    front[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!FrontPouch(holsters, front, comfort), "invalid tracking cannot grab magazine");
    std::printf("PASS: %d control checks\n", checks);
}
