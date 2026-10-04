#include "stdafx.h"
#include <ddraw.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace
{
    bool native = false;
    unsigned begins = 0, presents = 0;
    HRESULT result = DD_OK;
    HMODULE TestGetModuleHandleA(const char* name)
    {
        if (std::strcmp(name, "d3dim.dll") == 0)
            return native ? reinterpret_cast<HMODULE>(1) : nullptr;
        return GetModuleHandleA(name);
    }
    HRESULT __cdecl Begin() { ++begins; return result; }
    HRESULT __cdecl Present() { ++presents; return result; }
    unsigned initializations = 0;
    uint8_t* expectedContext = nullptr;
    const GUID* expectedDriver = nullptr;
    uint32_t expectedFlags = 0;
    int initializationResult = 0;
    int __fastcall Initialize(uint8_t* context, void*, const GUID* driver, uint32_t flags)
    {
        ++initializations;
        assert(context == expectedContext && driver == expectedDriver && flags == expectedFlags);
        return initializationResult;
    }
}
#define GetModuleHandleA TestGetModuleHandleA
#include "../source/dllmain.cpp"
#undef GetModuleHandleA

int main(int argc, char** argv)
{
    g_logPath = "build/tests/alt_tab_hooks.log";
    g_beginNativeSceneAddress = reinterpret_cast<uintptr_t>(&Begin);
    g_presentNativeDisplayAddress = reinterpret_cast<uintptr_t>(&Present);
    // A wrapper must pass through even with an unusable recovered context pointer.
    g_ppAltTabDisplayContext = reinterpret_cast<uint8_t**>(1);
    assert(BeginSceneWithAltTabRecovery() == DD_OK && begins == 1);
    result = DDERR_SURFACELOST;
    assert(PresentWithAltTabRecovery() == result && presents == 1 && !g_altTabRecovery.pending);
    native = true;
    g_ppAltTabDisplayContext = nullptr;
    assert(BeginSceneWithAltTabRecovery() == result && begins == 2);
    assert(PresentWithAltTabRecovery() == result && presents == 2 && !g_altTabRecovery.pending);
    g_altTabRecovery.context = reinterpret_cast<uint8_t*>(1);
    assert(PresentWithAltTabRecovery() == result && presents == 3 && g_altTabRecovery.pending);
    auto context = static_cast<uint8_t*>(nullptr);
    g_ppAltTabDisplayContext = &context;
    result = E_INVALIDARG;
    assert(BeginSceneWithAltTabRecovery() == result && begins == 3);
    assert(PresentWithAltTabRecovery() == result && presents == 4 && !g_altTabRecovery.pending);
    std::array<void*, 27> methods{};
    methods[9] = reinterpret_cast<void*>(&Begin);
    methods[26] = reinterpret_cast<void*>(&Present);
    auto methodsPointer = methods.data();
    std::array<uint8_t, 0x50> foreignContext{};
    const auto object = reinterpret_cast<uint8_t*>(&methodsPointer);
    std::memcpy(foreignContext.data() + 0x40, &object, sizeof(object));
    std::memcpy(foreignContext.data() + 0x48, &object, sizeof(object));
    assert(!AltTabRecovery::UsesNativeModules(foreignContext.data(), reinterpret_cast<HMODULE>(1), reinterpret_cast<HMODULE>(2)));
    assert(AltTabRecovery::UsesNativeModules(foreignContext.data(), GetModuleHandleW(nullptr), GetModuleHandleW(nullptr)));
    assert(!AltTabRecovery::UsesNativeModules(nullptr, GetModuleHandleW(nullptr), GetModuleHandleW(nullptr)));
    context = foreignContext.data();
    assert(BeginSceneWithAltTabRecovery() == result && begins == 4 && g_altTabRecovery.context == nullptr);
    assert(!InstallAltTabRecoveryHooks(nullptr));
    // Execute the real thiscall/fastcall bridge with the original argument order
    // and stack cleanup, including both initialization FPU flag variants.
    expectedContext = foreignContext.data();
    g_logPath.clear();
    expectedDriver = reinterpret_cast<const GUID*>(7);
    g_initializeNativeDrawAddress = reinterpret_cast<uintptr_t>(&Initialize);
    const auto initializedWindow = reinterpret_cast<HWND>(1);
    uint32_t fullscreen = 1;
    std::memcpy(foreignContext.data(), &initializedWindow, sizeof(initializedWindow));
    std::memcpy(foreignContext.data() + 4, &fullscreen, sizeof(fullscreen));
    for (unsigned call = 0; call < 512; ++call)
    {
        expectedFlags = (call & 1) != 0 ? 0x17 : 7;
        assert(InitializeNativeDrawWithAltTabSettings(expectedContext, nullptr, expectedDriver, expectedFlags) == 0);
        assert(g_altTabCooperativeSettings.window == initializedWindow);
        assert(g_altTabCooperativeSettings.draw == reinterpret_cast<IDirectDraw4*>(object));
        assert(g_altTabCooperativeSettings.flags == ((call & 1) != 0 ? 0x13u : 0x813u));
    }
    assert(initializations == 512);
    initializationResult = static_cast<int>(0x82000002);
    assert(InitializeNativeDrawWithAltTabSettings(expectedContext, nullptr, expectedDriver, expectedFlags) == initializationResult);
    assert(g_altTabCooperativeSettings.draw == nullptr && g_altTabCooperativeSettings.flags == 0);
    g_logPath = "build/tests/alt_tab_hooks.log";

    if (argc > 1)
    {
        // Scan and patch a private executable image, never launch/modify toy2.exe.
        std::ifstream file(argv[1], std::ios::binary);
        assert(file);
        std::vector<uint8_t> disk{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(disk.data());
        const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(disk.data() + dos->e_lfanew);
        auto image = static_cast<uint8_t*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        assert(image != nullptr);
        std::memcpy(image, disk.data(), nt->OptionalHeader.SizeOfHeaders);
        const auto sections = IMAGE_FIRST_SECTION(nt);
        for (size_t index = 0; index < nt->FileHeader.NumberOfSections; ++index)
            std::memcpy(image + sections[index].VirtualAddress, disk.data() + sections[index].PointerToRawData, sections[index].SizeOfRawData);
        const std::vector<uint8_t> original(image, image + nt->OptionalHeader.SizeOfImage);
        auto module = reinterpret_cast<HMODULE>(image);
        // Missing texture uploader and inconsistent context/cached-handle references
        // must leave ALL three hook calls byte-for-byte unchanged.
        const size_t mismatches[] = {0xB0200, 0xABA81, 0xB1164, 0xC288E, 0xB2D51, 0xAEE1F, 0xAEF4F};
        for (const auto offset : mismatches)
        {
            image[offset] ^= 1;
            const std::vector<uint8_t> before(image, image + original.size());
            assert(!InstallAltTabRecoveryHooks(module));
            assert(std::memcmp(image, before.data(), before.size()) == 0);
            std::memcpy(image, original.data(), original.size());
        }
        assert(InstallAltTabRecoveryHooks(module));
        assert(g_beginNativeSceneAddress == reinterpret_cast<uintptr_t>(image + 0xABA90));
        assert(g_presentNativeDisplayAddress == reinterpret_cast<uintptr_t>(image + 0xABD40));
        assert(g_ppAltTabDisplayContext == reinterpret_cast<uint8_t**>(0x00884008));
        assert(g_altTabBindings.textureHead == reinterpret_cast<uint8_t**>(0x00884444));
        assert(g_altTabBindings.cachedTextureHandle == reinterpret_cast<int32_t*>(0x00AAD778));
        assert(g_initializeNativeDrawAddress == reinterpret_cast<uintptr_t>(image + 0xAEEE0));
        assert(g_altTabBindings.cooperativeSettings == &g_altTabCooperativeSettings);
        assert(reinterpret_cast<uintptr_t>(g_altTabBindings.recreateTexture) == reinterpret_cast<uintptr_t>(image + 0xB0200));
        const size_t sites[] = {0xB2D50, 0xABAB0, 0xAEE1F};
        for (const auto site : sites)
            assert(image[site] == 0xE8 && std::memcmp(image + site, original.data() + site, 5) != 0);
        for (size_t offset = 0; offset < original.size(); ++offset)
            if (!(offset >= sites[0] && offset < sites[0] + 5) && !(offset >= sites[1] && offset < sites[1] + 5) &&
                !(offset >= sites[2] && offset < sites[2] + 5))
                assert(image[offset] == original[offset]);
        VirtualFree(image, 0, MEM_RELEASE);
        std::puts("Native executable signatures, original cooperative flags, relative call targets and fail-closed three-hook installation tests passed.");
    }
    std::puts("Alt-Tab wrapper passthrough, native HRESULT forwarding and null-context tests passed.");
}
