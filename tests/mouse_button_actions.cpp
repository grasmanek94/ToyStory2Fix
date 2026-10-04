#include "../source/MouseButtonActions.h"
#include <cassert>
#include <cstdio>

int main()
{
    MouseButtonActions input;
    assert(input.GetActions(true, false, false) == 0);
    assert(input.GetActions(true, true, false) == MouseButtonActions::Fire);
    assert(input.GetActions(true, true, false) == MouseButtonActions::Fire);
    assert(input.GetActions(true, false, false) == 0);

    // Mirror the game's previous/current snapshot: a held right click produces one edge.
    uint16_t current = 0;
    unsigned visorPresses = 0;
    const auto frame = [&](uint16_t nativeActions, bool fireDown, bool visorDown)
    {
        const uint16_t previous = current;
        current = nativeActions | input.GetActions(true, fireDown, visorDown);
        if ((current & MouseButtonActions::Visor) != 0 &&
            (previous & MouseButtonActions::Visor) == 0)
        {
            ++visorPresses;
        }
        assert((current & nativeActions) == nativeActions);
    };
    frame(0x10, true, true);
    assert(current == 0x8410 && visorPresses == 1);
    frame(0x10, true, true);
    assert(visorPresses == 1);
    frame(0x10, true, false);
    frame(0x10, true, true);
    assert(visorPresses == 2);

    // Releasing mouse buttons cannot release Ctrl/Tab if the keyboard still holds them.
    frame(0x8400, false, false);
    assert(current == 0x8400 && visorPresses == 2);
    frame(0x8000, false, true);
    assert(current == 0x8400 && visorPresses == 2);
    frame(0, false, false);
    frame(0x400, false, false);
    assert(visorPresses == 3);

    // Focus loss clears only mouse state, and a refocus click must not fire/toggle.
    assert(input.GetActions(false, true, true) == 0);
    assert(input.GetActions(true, true, true) == 0);
    assert(input.GetActions(true, true, true) == 0);
    assert(input.GetActions(true, false, true) == 0);
    assert(input.GetActions(true, true, true) == MouseButtonActions::Fire);
    assert(input.GetActions(true, true, false) == MouseButtonActions::Fire);
    assert(input.GetActions(true, true, true) == 0x8400);

    // Initial/long-gap resets also require held buttons to be released independently.
    input.Reset();
    assert(input.GetActions(true, true, true) == 0);
    assert(input.GetActions(true, true, false) == 0);
    assert(input.GetActions(true, true, true) == MouseButtonActions::Visor);
    assert(input.GetActions(true, false, false) == 0);
    assert(input.GetActions(true, true, true) == 0x8400);

    std::puts("Mouse-button action, hold/edge, keyboard coexistence and focus tests passed.");
}
