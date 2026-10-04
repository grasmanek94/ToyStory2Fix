#pragma once

#include <cstdint>

struct MouseButtonActions
{
    // Native raw actions: default Left Ctrl (fire) and Tab (visor).
    static constexpr uint16_t Fire = 0x8000;
    static constexpr uint16_t Visor = 0x0400;

    bool fireArmed = false;
    bool visorArmed = false;

    void Reset()
    {
        *this = {};
    }

    uint16_t GetActions(bool focused, bool fireDown, bool visorDown)
    {
        if (!focused)
        {
            Reset();
            return 0;
        }

        // Ignore buttons held when focus returns, including the click used to refocus.
        if (!fireDown)
            fireArmed = true;
        if (!visorDown)
            visorArmed = true;

        uint16_t actions = 0;
        if (fireDown && fireArmed)
            actions |= Fire;
        if (visorDown && visorArmed)
            actions |= Visor;
        return actions;
    }
};
