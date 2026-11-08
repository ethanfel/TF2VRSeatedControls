#pragma once
#include <cstdint>

namespace seated {
struct Profile {
    const char* name;
    const char* sha256;
    std::uint32_t classify, update;
    std::uint32_t leftReturn, rightReturn;
    std::uint32_t gun, grenade, radius, dialogue;
    std::uint32_t pouch, pouchReturn, reload;
    std::uint32_t reloadDistance, reloadAngle, guideDistance, guideAngle, seatDistance;
};
inline constexpr Profile Profiles[] = {
    {"stable 1.0.25", "9a555fe9f6fc19bd3ab3f81459643aaaa2c9f55350510d4db3917c3085818450",
     0x3fc40, 0x3fde0, 0x425a2, 0x425d6, 0x35b230, 0x35b26c, 0x1ae7b8, 0x35b5fd,
     0x3fc10, 0x5c17e, 0x52a10, 0x53e86, 0x53ea5, 0x54185, 0x54237, 0x54318},
    {"multiplayer-beta 1.1.32", "55e4440e8ed6a6c7c2f5d6b499d34d7aae7cd04634d70803d61c4de7cffce101",
     0x50490, 0x50630, 0x5313f, 0x5316d, 0x4ad390, 0x4ad3cc, 0x24ee70, 0x4ad83d,
     0x50460, 0x6d4c5, 0x63bd0, 0x65046, 0x65065, 0x65345, 0x653f7, 0x654d8}
};
}
