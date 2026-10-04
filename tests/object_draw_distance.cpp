#include "../source/ObjectDrawDistance.h"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace ObjectDrawDistance;

int main()
{
    assert(SanitizeDistance(0) == DefaultDistance);
    assert(SanitizeDistance(-1) == DefaultDistance);
    assert(SanitizeDistance(std::numeric_limits<float>::infinity()) == DefaultDistance);
    assert(SanitizeDistance(std::numeric_limits<float>::quiet_NaN()) == DefaultDistance);
    assert(SanitizeDistance(100) == MinimumDistance);
    assert(SanitizeDistance(1e30f) == MaximumDistance);
    assert(SanitizeDistance(32768) == 32768);

    std::array<Actor, ActorCount> actors{};
    Actor player;
    std::array<const uint8_t*, ModelTypeCount> models{}, metadata{};
    uint8_t model[8]{}, typeMetadata[2]{};
    int32_t bones = 32;
    int16_t renderEnabled = 1;
    std::memcpy(model + 4, &bones, 4);
    std::memcpy(typeMetadata, &renderEnabled, 2);
    models[1] = model;
    metadata[1] = typeMetadata;
    player.Write<int16_t>(0x14, 1);
    assert(HasRenderableModel(player, models.data(), models.size(), metadata.data()));
    renderEnabled = 0;
    std::memcpy(typeMetadata, &renderEnabled, 2);
    assert(!HasRenderableModel(player, models.data(), models.size(), metadata.data()));
    renderEnabled = 1;
    std::memcpy(typeMetadata, &renderEnabled, 2);
    bones = 33;
    std::memcpy(model + 4, &bones, 4);
    assert(!HasRenderableModel(player, models.data(), models.size(), metadata.data()));
    bones = 0;
    std::memcpy(model + 4, &bones, 4);
    assert(!HasRenderableModel(player, models.data(), models.size(), metadata.data()));
    bones = 32;
    std::memcpy(model + 4, &bones, 4);
    assert(!HasRenderableModel(player, models.data(), 1, metadata.data()));
    assert(!HasRenderableModel(player, nullptr, models.size(), metadata.data()));
    assert(!HasRenderableModel(player, models.data(), models.size(), nullptr));
    metadata[1] = nullptr;
    assert(!HasRenderableModel(player, models.data(), models.size(), metadata.data()));
    metadata[1] = typeMetadata;
    models[1] = nullptr;
    assert(!HasRenderableModel(player, models.data(), models.size(), metadata.data()));
    models[1] = model;
    player.Write<int16_t>(0x14, 128);
    assert(!HasRenderableModel(player, models.data(), 1000, metadata.data()));
    player.Write<int16_t>(0x14, -1);
    assert(!HasRenderableModel(player, models.data(), models.size(), metadata.data()));
    player.Write<int16_t>(0x14, 0);
    assert(!HasRenderableModel(player, models.data(), models.size(), metadata.data()));
    const int32_t camera[] = { 0, 0, 0 };
    for (size_t i = 0; i < ActorCount; ++i)
    {
        actors[i].Write<int32_t>(0, static_cast<int32_t>((i + 1) * 32 * 1000));
        actors[i].Write<int16_t>(0x14, 1);
        actors[i].Write<int16_t>(0x7E, 1);
        actors[i].Write<uint16_t>(0x40, 0x82); // Keep native "active" and unrelated flags.
    }
    assert(WithinRange(actors[63], camera, DefaultDistance));
    assert(!WithinRange(actors[63], camera, 32768));
    actors[63].Write<int32_t>(0, std::numeric_limits<int32_t>::max());
    const int32_t distantCamera[] = { std::numeric_limits<int32_t>::min(), 0, 0 };
    assert(!WithinRange(actors[63], distantCamera, DefaultDistance));
    actors[63].Write<int32_t>(0, 64000 * 32);

    std::array<Actor*, ListSlots> list{};
    list[0] = &actors[2]; // Native order must stay intact, not be sorted by pool index.
    list[1] = &actors[0];
    list[2] = &player;
    const auto nativeList = list;
    const auto nativeActors = actors;
    RenderOverlay overlay;
    assert(overlay.Begin(list, actors.data(), &player, camera, DefaultDistance, [](const Actor&) { return true; }));
    assert(list[0] == &actors[2] && list[1] == &actors[0]);
    assert(list[ActorCount] == &player && list[RenderSlots] == nullptr);
    for (size_t i = 0; i < ActorCount; ++i)
    {
        size_t occurrences = 0;
        for (size_t j = 0; j < RenderSlots; ++j)
            occurrences += list[j] == &actors[i];
        assert(occurrences == 1 && actors[i].Read<uint16_t>(0x40) == 0x83);
        assert(actors[i].Read<int32_t>(0) == nativeActors[i].Read<int32_t>(0));
    }
    assert(!overlay.Begin(list, actors.data(), &player, camera, DefaultDistance, [](const Actor&) { return true; }));
    overlay.End(list);
    assert(list == nativeList && !overlay.active);
    for (size_t i = 0; i < ActorCount; ++i)
        assert(actors[i].bytes == nativeActors[i].bytes);
    overlay.End(list); // Repeated cleanup is harmless.

    // Preserve native hides, empty/unready slots, portals/frustum rejection and range.
    list = {};
    list[0] = &player;
    actors[0].Write<uint16_t>(0x40, 0x2082);
    actors[1].Write<int16_t>(0x14, 0);
    actors[2].Write<int16_t>(0x7E, 0);
    actors[3].Write<uint16_t>(0x40, 0x83); // Originally visible; do not clear it on cleanup.
    assert(overlay.Begin(list, actors.data(), &player, camera, 6000,
        [&](const Actor& actor) { return &actor != &actors[4]; }));
    assert(list[0] == &actors[3] && list[1] == &player && list[2] == nullptr);
    actors[3].Write<uint16_t>(0x40, 0x183); // A hypothetical native write must survive.
    overlay.End(list);
    assert(actors[3].Read<uint16_t>(0x40) == 0x183);

    // Also preserve native unrelated flag writes for actors whose visibility we changed.
    list = {};
    list[0] = &player;
    assert(overlay.Begin(list, actors.data(), &player, camera, 7000, [](const Actor&) { return true; }));
    actors[4].Write<uint16_t>(0x40, actors[4].Read<uint16_t>(0x40) | 0x400);
    overlay.End(list);
    assert(actors[4].Read<uint16_t>(0x40) == 0x482);

    // Menus, bad lists, duplicate actors and non-pool pointers remain untouched.
    list = {};
    assert(!overlay.Begin(list, actors.data(), &player, camera, DefaultDistance, [](const Actor&) { return true; }));
    list[0] = &actors[0];
    list[1] = &actors[0];
    list[2] = &player;
    auto invalid = list;
    assert(!overlay.Begin(list, actors.data(), &player, camera, DefaultDistance, [](const Actor&) { return true; }));
    assert(list == invalid);
    Actor foreign;
    list[1] = &foreign;
    invalid = list;
    assert(!overlay.Begin(list, actors.data(), &player, camera, DefaultDistance, [](const Actor&) { return true; }));
    assert(list == invalid);
    list.fill(&foreign);
    assert(!overlay.Begin(list, actors.data(), &player, camera, DefaultDistance, [](const Actor&) { return true; }));
    assert(!overlay.Begin(list, nullptr, &player, camera, DefaultDistance, [](const Actor&) { return true; }));
    std::puts("Finite object ranges, native model eligibility, full 64-actor render capacity, gameplay isolation and overlay restoration tests passed.");
}
