// NWN:EE Japanese hook (Windows x64).
//
// The legacy GUI font bakes a fixed 256 glyph atlas. AurGetTTFTexture is reached
// through a global function pointer, so this DLL swaps that pointer and asks the
// original bake for the full Japanese code point list instead. Everything else is
// passed straight through.
//
// The hook writes nwnjp.log next to itself.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <initializer_list>
#include <intrin.h>

#include "legacy_codepoints.h"
#include "codepoint_slot_table.h"
#include "nui_range_table.h"

namespace {

/// Global holding the resolved AurGetTTFTexture pointer.
constexpr uintptr_t kAurGetTTFTexturePtrRva = 0x140B278u;
/// The bake the engine issues for its 256 glyph atlas.
constexpr int kEngineGlyphCount = 256;

using BakeFn = void (*)(const char *path, float size, int *chars, int count,
                        float p5, float p6, float p7, void *out);

BakeFn g_original = nullptr;
CRITICAL_SECTION g_logLock;
bool g_logReady = false;

bool BuildLogPath(char *path, size_t size) {
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&BuildLogPath), &self)) {
        return false;
    }
    if (GetModuleFileNameA(self, path, static_cast<DWORD>(size)) == 0) return false;
    char *slash = strrchr(path, '\\');
    if (slash == nullptr) return false;
    strcpy_s(slash + 1, size - static_cast<size_t>(slash + 1 - path), "nwnjp.log");
    return true;
}

void WriteLog(const char *format, va_list args) {
    char path[MAX_PATH]{};
    if (!BuildLogPath(path, sizeof(path))) return;
    if (FILE *file = fopen(path, "a")) {
        vfprintf(file, format, args);
        fputc('\n', file);
        fclose(file);
    }
}

/// Each process starts a fresh log; appending forever made the file hard to read.
void ResetLog() {
    char path[MAX_PATH]{};
    if (!BuildLogPath(path, sizeof(path))) return;
    if (FILE *file = fopen(path, "w")) fclose(file);
}

void Log(const char *format, ...) {
    if (!g_logReady) return;
    EnterCriticalSection(&g_logLock);
    va_list args;
    va_start(args, format);
    WriteLog(format, args);
    va_end(args);
    LeaveCriticalSection(&g_logLock);
}

/// Same as Log, but gives up when another thread holds the lock so a crash that
/// interrupts a write cannot deadlock the handler.
void LogTry(const char *format, ...) {
    if (!g_logReady) return;
    if (!TryEnterCriticalSection(&g_logLock)) return;
    va_list args;
    va_start(args, format);
    WriteLog(format, args);
    va_end(args);
    LeaveCriticalSection(&g_logLock);
}

/// The engine derives each glyph's vertical UV window as `size` pixels starting just
/// inside the ink, so the top row of every glyph falls outside the window and the glyph
/// looks clipped along its top edge. Shifting the window up by one texel lines it up with
/// the ink without changing its height.
void ShiftUvWindows(void *data) {
    auto *base = static_cast<uint8_t *>(data);
    const uint32_t height = *reinterpret_cast<uint32_t *>(base + 8);
    const int32_t count = *reinterpret_cast<int32_t *>(base + 0x20);
    auto *from = *reinterpret_cast<float **>(base + 0x10);
    auto *to = *reinterpret_cast<float **>(base + 0x18);
    if (height == 0 || count <= 0 || from == nullptr || to == nullptr) return;
    const float step = 0.5f / static_cast<float>(height);
    for (int32_t i = 0; i < count; ++i) {
        from[i * 3 + 1] -= step;
        to[i * 3 + 1] -= step;
    }
}

void HookedBake(const char *path, float size, int *chars, int count, float p5,
                float p6, float p7, void *out) {
    Log("[bake] path=%s size=%.2f count=%d chars=%p out=%p",
        path ? path : "(null)", size, count, static_cast<void *>(chars), out);
    if (!g_original) {
        Log("[bake] original pointer missing, cannot bake");
        return;
    }
    if (count == kEngineGlyphCount && chars != nullptr) {
        Log("[bake] substituting %zu code points",
            sizeof(kNwnjpLegacyCodepoints) / sizeof(kNwnjpLegacyCodepoints[0]));
        g_original(path, size, const_cast<int *>(reinterpret_cast<const int *>(kNwnjpLegacyCodepoints)),
                   static_cast<int>(sizeof(kNwnjpLegacyCodepoints) / sizeof(kNwnjpLegacyCodepoints[0])),
                   p5, p6, p7, out);
        ShiftUvWindows(out);
        Log("[bake] substitute finished");
        return;
    }
    g_original(path, size, chars, count, p5, p6, p7, out);
    Log("[bake] pass-through finished");
}

/// Site pointer for a runtime patch, or nullptr when the bytes are not inside the loaded
/// image. Reading them unconditionally faults whenever this DLL ends up in a host whose
/// image is smaller than the game's (the smoke test in `work/out/probe/smoke` found this), and
/// such a host has nothing to patch anyway.
uint8_t *PatchSite(HMODULE module, uintptr_t rva, size_t size) {
    if (module == nullptr) return nullptr;
    auto *base = reinterpret_cast<uint8_t *>(module);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const size_t imageSize = nt->OptionalHeader.SizeOfImage;
    if (rva + size > imageSize) {
        Log("[patch] site 0x%llX+%llu is outside the image (%llu bytes)",
            static_cast<unsigned long long>(rva), static_cast<unsigned long long>(size),
            static_cast<unsigned long long>(imageSize));
        return nullptr;
    }
    return base + rva;
}

DWORD WINAPI InstallHook(LPVOID) {
    HMODULE base = GetModuleHandleA(nullptr);
    if (!base) {
        Log("[hook] no module handle");
        return 1;
    }
    auto *slot = reinterpret_cast<void **>(PatchSite(base, kAurGetTTFTexturePtrRva, sizeof(void *)));
    if (slot == nullptr) return 1;
    for (int attempt = 0; attempt < 1200; ++attempt) {
        void *current = *slot;
        if (current != nullptr && reinterpret_cast<uintptr_t>(current) > 0x10000) {
            g_original = reinterpret_cast<BakeFn>(current);
            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &oldProtect)) {
                Log("[hook] VirtualProtect failed: %lu", GetLastError());
                return 1;
            }
            *slot = reinterpret_cast<void *>(&HookedBake);
            VirtualProtect(slot, sizeof(void *), oldProtect, &oldProtect);
            Log("[hook] installed: slot=%p original=%p hook=%p", static_cast<void *>(slot),
                reinterpret_cast<void *>(g_original), reinterpret_cast<void *>(&HookedBake));
            return 0;
        }
        Sleep(50);
    }
    Log("[hook] timed out waiting for the function pointer");
    return 1;
}

/// Allocates one page within rel32 range of `anchor`.
///
/// The patch sites are 5-byte jumps and calls, so the stubs have to live within
/// +-2 GB of them. A plain VirtualAlloc hands out pages in the low region (around
/// 0x0000_02xx_xxxxxxxx on this machine) while the game is loaded at
/// 0x7ff7_xxxxxxxx, and the truncated displacement then jumps into unmapped memory.
void *AllocNear(const void *anchor, size_t size) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t granularity = info.dwAllocationGranularity;
    const uintptr_t center = reinterpret_cast<uintptr_t>(anchor);
    constexpr uintptr_t kMaxDistance = 0x70000000u;  // keeps a margin below the rel32 limit
    for (uintptr_t distance = granularity; distance < kMaxDistance; distance += granularity) {
        for (uintptr_t candidate : { center + distance, center - distance }) {
            if (candidate < granularity) continue;
            void *memory = VirtualAlloc(reinterpret_cast<void *>(candidate & ~(granularity - 1)),
                                        size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (memory) return memory;
        }
    }
    return nullptr;
}

/// CAuroraTTFTexture::Load lays the atlas out as a single row:
///   width  = next_pow2((int)size * count + (count + 1) * 3)   -> EBX
///   height = next_pow2((int)size + 6)                         -> ESI
/// For the 2,962 glyph set that is 65536x32, which is wider than the driver's
/// GL_MAX_TEXTURE_SIZE (16,384 on this machine), so the upload fails and every glyph
/// samples a texture that was never created. Two instructions after the width and
/// height are both final, this replaces them with a grid that fits; the rest of the
/// function derives the buffer size, the packing bin and the UV divisors from them.
constexpr uintptr_t kAtlasSizeRva = 0xFB7E7u;
constexpr uint8_t kAtlasSizeOriginal[12] = {
    0x44, 0x8B, 0xEB,              // mov r13d, ebx
    0x44, 0x0F, 0xAF, 0xEE,        // imul r13d, esi
    0x4C, 0x89, 0x6C, 0x24, 0x70,  // mov qword ptr [rsp + 0x70], r13
};
constexpr int kAtlasWidth = 4096;
constexpr int kAtlasHeight = 2048;

bool InstallAtlasSizing() {
    HMODULE base = GetModuleHandleA(nullptr);
    auto *site = PatchSite(base, kAtlasSizeRva, sizeof(kAtlasSizeOriginal));
    if (site == nullptr) return false;
    if (memcmp(site, kAtlasSizeOriginal, sizeof(kAtlasSizeOriginal)) != 0) {
        Log("[atlas] unexpected bytes at the sizing site, skipping");
        return false;
    }
    auto *stub = static_cast<uint8_t *>(AllocNear(site, 64));
    if (!stub) {
        Log("[atlas] no free page within rel32 range of the sizing site");
        return false;
    }

    size_t i = 0;
    const auto emit = [&](std::initializer_list<int> bytes) {
        for (int b : bytes) stub[i++] = static_cast<uint8_t>(b);
    };
    // Only the oversized atlases are rewritten; an ordinary 256 glyph font keeps
    // the dimensions the engine computed for it.
    emit({ 0x81, 0xFB, 0x00, 0x40, 0x00, 0x00 });  // cmp ebx, 0x4000
    emit({ 0x76, 0x0A });                          // jbe keep
    emit({ 0xBB });                                // mov ebx, kAtlasWidth
    memcpy(stub + i, &kAtlasWidth, 4);
    i += 4;
    emit({ 0xBE });                                // mov esi, kAtlasHeight
    memcpy(stub + i, &kAtlasHeight, 4);
    i += 4;
    memcpy(stub + i, kAtlasSizeOriginal, 3);       // keep: mov r13d, ebx
    i += 3;
    memcpy(stub + i, kAtlasSizeOriginal + 3, 4);   // imul r13d, esi
    i += 4;
    memcpy(stub + i, kAtlasSizeOriginal + 7, 5);   // mov [rsp + 0x70], r13
    i += 5;
    emit({ 0xE9 });                                // jmp back
    const int32_t back = static_cast<int32_t>(reinterpret_cast<intptr_t>(site) + 12 -
                                              reinterpret_cast<intptr_t>(stub + i + 4));
    memcpy(stub + i, &back, 4);
    i += 4;

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, sizeof(kAtlasSizeOriginal), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("[atlas] VirtualProtect failed: %lu", GetLastError());
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    site[0] = 0xE9;
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(stub) -
                                             reinterpret_cast<intptr_t>(site + 5));
    memcpy(site + 1, &rel, 4);
    memset(site + 5, 0x90, sizeof(kAtlasSizeOriginal) - 5);
    VirtualProtect(site, sizeof(kAtlasSizeOriginal), oldProtect, &oldProtect);
    Log("[atlas] sizing override installed: site=%p stub=%p (%zu bytes) max=%dx%d",
        static_cast<void *>(site), static_cast<void *>(stub), i, kAtlasWidth, kAtlasHeight);
    return true;
}

/// CAurFont::CalculateVisibleStringLengthAndWidth walks the text one byte at a time and
/// stores how many bytes fit on the line at CStringItem+0x14. Two things follow from that
/// byte walk: the split StringGob::WrapLines performs can land inside a character, and a
/// three byte Japanese character is measured as three Latin glyphs, so lines break far
/// too early. This hook re-measures the prefix per character and keeps the longer result.
constexpr uintptr_t kVisibleLengthRva = 0x4C120u;
constexpr uint8_t kVisibleLengthPrologue[5] = { 0x48, 0x89, 0x5C, 0x24, 0x10 };
constexpr uintptr_t kGetSymbolCoordsRva = 0xEBB10u;
constexpr uintptr_t kAlignGuiToPixelRva = 0x74E20u;
constexpr size_t kCStringTextOffset = 0x8;
constexpr size_t kCStringVisibleLengthOffset = 0x14;
constexpr size_t kCStringVisibleWidthOffset = 0x1C;

using CalculateVisibleFn = bool (*)(void *self, void *item, float maxWidth, bool wrap);
CalculateVisibleFn g_calculateVisible = nullptr;

struct SymbolVector {
    float x, y, z;
};
using GetSymbolCoordsFn = void (*)(void *fontInfo, int index, SymbolVector *out1,
                                   SymbolVector *out2);
GetSymbolCoordsFn g_getSymbolCoords = nullptr;
using AlignGuiToPixelFn = void (*)(float *value);
AlignGuiToPixelFn g_alignGuiToPixel = nullptr;

/// Byte length and width of the longest character-aligned prefix that fits in maxWidth.
/// Markup tags (<...>) take no width but stay in the byte count, so the prefix can be
/// handed back to the wrapper unchanged; that is what lets a bubble pick a model that
/// actually fits its text instead of jumping to the widest one. Returns -1 when the text
/// carries an explicit newline, so the engine's own result is kept.
int32_t MeasureVisibleLength(void *self, const char *text, float maxWidth, float *widthOut) {
    auto *font = static_cast<uint8_t *>(self);
    auto *info = *reinterpret_cast<uint8_t **>(font + 0x28);
    if (info == nullptr || text == nullptr || g_getSymbolCoords == nullptr) return -1;
    const float scale = *reinterpret_cast<float *>(info + 0xC);
    const float spacing = *reinterpret_cast<float *>(info + 0x10);
    const bool useWidthHint = *reinterpret_cast<char *>(info + 0x68) != 0;
    const float limit = maxWidth == 0.0f ? 1e6f : maxWidth + 1.0000001e-05f;
    float width = 0.0f;
    float breakWidth = 0.0f;
    int32_t breakPos = 0;
    int32_t i = 0;
    while (text[i] != '\0') {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (lead == '\n') {
            // A newline is a line end, not an unmeasurable string: the chat log
            // keeps its messages in one buffer separated by newlines, and bailing
            // here left those lines with the engine's byte measurement.
            if (i == 0) return -1;
            *widthOut = width;
            return i;
        }
        if (lead == '<') {
            // Markup is interpreted by the draw path; skip it without adding width.
            const char *close = static_cast<const char *>(strchr(text + i, '>'));
            if (close == nullptr) return -1;
            i = static_cast<int32_t>(close - text) + 1;
            continue;
        }
        int32_t consumed = 1;
        int32_t index = lead;
        if (lead >= 0xE0) {
            const unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
            const unsigned char b2 = static_cast<unsigned char>(text[i + 2]);
            if (lead < 0xF0 && (b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
                const int32_t cp = ((lead & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
                consumed = 3;
                index = kNwnjpCodepointSlot[cp];
            }
        } else if (lead >= 0xC0) {
            const unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
            if ((b1 & 0xC0) == 0x80) {
                const int32_t cp = ((lead & 0x1F) << 6) | (b1 & 0x3F);
                consumed = 2;
                index = kNwnjpCodepointSlot[cp];
            }
        }
        if (consumed > 1 && index == 0xFFFF) {
            consumed = 1;
            index = lead;
        }
        SymbolVector from{}, to{};
        g_getSymbolCoords(info, index, &from, &to);
        const float extent = useWidthHint ? to.z : (to.x - from.x);
        width += scale * extent;
        if (width > limit) {
            if (breakPos > 0) {
                *widthOut = breakWidth;
                return breakPos;
            }
            *widthOut = width;
            return i;
        }
        const float reported = width;
        width += spacing;
        if (g_alignGuiToPixel != nullptr) g_alignGuiToPixel(&width);
        i += consumed;
        if (lead == ' ') {
            breakPos = i - consumed;
            breakWidth = reported;
        }
    }
    *widthOut = width;
    return i;
}

/// Width of the first `length` bytes with <...> markup skipped. The engine counts markup
/// bytes as glyphs, so a centred line that carries colour codes is positioned by a width
/// larger than it draws; this reports the width of what is actually drawn.
float MeasureMarkupWidth(void *self, const char *text, int32_t length) {
    auto *font = static_cast<uint8_t *>(self);
    auto *info = *reinterpret_cast<uint8_t **>(font + 0x28);
    if (info == nullptr || text == nullptr || g_getSymbolCoords == nullptr) return 0.0f;
    const float scale = *reinterpret_cast<float *>(info + 0xC);
    const float spacing = *reinterpret_cast<float *>(info + 0x10);
    const bool useWidthHint = *reinterpret_cast<char *>(info + 0x68) != 0;
    float width = 0.0f;
    int32_t i = 0;
    while (i < length && text[i] != '\0') {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (lead == '<') {
            const char *close =
                static_cast<const char *>(memchr(text + i, '>', static_cast<size_t>(length - i)));
            if (close == nullptr) break;
            i = static_cast<int32_t>(close - text) + 1;
            continue;
        }
        int32_t consumed = 1;
        int32_t index = lead;
        if (lead >= 0xE0) {
            const unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
            const unsigned char b2 = static_cast<unsigned char>(text[i + 2]);
            if (lead < 0xF0 && (b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
                const int32_t cp = ((lead & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
                consumed = 3;
                index = kNwnjpCodepointSlot[cp];
            }
        } else if (lead >= 0xC0) {
            const unsigned char b1 = static_cast<unsigned char>(text[i + 1]);
            if ((b1 & 0xC0) == 0x80) {
                const int32_t cp = ((lead & 0x1F) << 6) | (b1 & 0x3F);
                consumed = 2;
                index = kNwnjpCodepointSlot[cp];
            }
        }
        if (consumed > 1 && index == 0xFFFF) {
            consumed = 1;
            index = lead;
        }
        SymbolVector from{}, to{};
        g_getSymbolCoords(info, index, &from, &to);
        width += scale * (useWidthHint ? to.z : (to.x - from.x));
        width += spacing;
        if (g_alignGuiToPixel != nullptr) g_alignGuiToPixel(&width);
        i += consumed;
    }
    return width;
}

void SnapVisibleLength(void *item) {
    if (item == nullptr) return;
    auto *base = static_cast<uint8_t *>(item);
    const char *cursor = *reinterpret_cast<const char **>(base + kCStringTextOffset);
    auto *length = reinterpret_cast<int32_t *>(base + kCStringVisibleLengthOffset);
    if (cursor == nullptr) return;
    const int32_t value = *length;
    if (value <= 0) return;
    if ((static_cast<uint8_t>(cursor[value]) & 0xC0) != 0x80) return;
    int32_t start = value;
    while (start > 0 && (static_cast<uint8_t>(cursor[start]) & 0xC0) == 0x80) --start;
    if (start > 0) *length = start;
}

bool CalculateVisibleHook(void *self, void *item, float maxWidth, bool wrap) {
    const bool result = g_calculateVisible(self, item, maxWidth, wrap);
    if (item != nullptr) {
        auto *base = static_cast<uint8_t *>(item);
        const char *text = *reinterpret_cast<const char **>(base + kCStringTextOffset);
        auto *length = reinterpret_cast<int32_t *>(base + kCStringVisibleLengthOffset);
        auto *width = reinterpret_cast<float *>(base + kCStringVisibleWidthOffset);
        if (text != nullptr && *length > 0) {
            float measured = 0.0f;
            const int32_t bytes = MeasureVisibleLength(self, text, maxWidth, &measured);
            // The engine leaves the previous line's length in +0x14 when it asks for the
            // next one, so comparing the two decides the wrong thing: on the second line
            // of a message the stale value is larger than the measured prefix and nothing
            // was replaced. The measured prefix is the split we want either way.
            if (bytes >= 0) {
                *length = bytes;
                *width = measured;
            }
        }
        SnapVisibleLength(item);
    }
    return result;
}

bool InstallVisibleLengthFix() {
    HMODULE base = GetModuleHandleA(nullptr);
    auto *site = PatchSite(base, kVisibleLengthRva, sizeof(kVisibleLengthPrologue));
    if (site == nullptr) return false;
    if (memcmp(site, kVisibleLengthPrologue, sizeof(kVisibleLengthPrologue)) != 0) {
        Log("[wrap] unexpected bytes at the visible length site, skipping");
        return false;
    }
    auto *stub = static_cast<uint8_t *>(AllocNear(site, 128));
    if (!stub) {
        Log("[wrap] no free page within rel32 range of the visible length site");
        return false;
    }
    // The DLL itself sits far above the game, so the entry jump goes to a nearby
    // trampoline that reaches the hook with an absolute address.
    uint8_t *entry = stub;
    entry[0] = 0x48;  // mov rax, imm64
    entry[1] = 0xB8;
    const uint64_t hook = reinterpret_cast<uint64_t>(&CalculateVisibleHook);
    memcpy(entry + 2, &hook, 8);
    entry[10] = 0xFF;  // jmp rax
    entry[11] = 0xE0;

    uint8_t *resume = stub + 16;
    memcpy(resume, kVisibleLengthPrologue, sizeof(kVisibleLengthPrologue));
    resume[5] = 0xE9;  // jmp back to the rest of the original prologue
    const int32_t back = static_cast<int32_t>(reinterpret_cast<intptr_t>(site) + 5 -
                                              reinterpret_cast<intptr_t>(resume + 10));
    memcpy(resume + 6, &back, 4);
    g_calculateVisible = reinterpret_cast<CalculateVisibleFn>(resume);
    g_getSymbolCoords = reinterpret_cast<GetSymbolCoordsFn>(
        reinterpret_cast<uint8_t *>(base) + kGetSymbolCoordsRva);
    g_alignGuiToPixel = reinterpret_cast<AlignGuiToPixelFn>(
        reinterpret_cast<uint8_t *>(base) + kAlignGuiToPixelRva);

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, sizeof(kVisibleLengthPrologue), PAGE_EXECUTE_READWRITE,
                        &oldProtect)) {
        Log("[wrap] VirtualProtect failed: %lu", GetLastError());
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    site[0] = 0xE9;
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(entry) -
                                             reinterpret_cast<intptr_t>(site + 5));
    memcpy(site + 1, &rel, 4);
    VirtualProtect(site, sizeof(kVisibleLengthPrologue), oldProtect, &oldProtect);
    Log("[wrap] visible length snap installed: site=%p entry=%p resume=%p hook=%p",
        static_cast<void *>(site), static_cast<void *>(entry), static_cast<void *>(resume),
        reinterpret_cast<void *>(&CalculateVisibleHook));
    return true;
}

/// stb packs every glyph cell flush against its neighbours, while the atlas code asks
/// each glyph for a texture window `size` pixels tall regardless of how tall the ink is.
/// In the engine's own single row atlas that window only reaches empty space; in a
/// multi row atlas it reaches the glyphs of the next row and their pixels leak into the
/// rendered quad (thin glyphs like 'ー' or '・' are the worst). Growing the pack padding
/// separates the cells so the window stays inside its own glyph.
constexpr uintptr_t kPackRangesRva = 0x106AC0u;
constexpr uint8_t kPackRangesPrologue[5] = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
constexpr size_t kPackPaddingOffset = 0x1C;
constexpr int kPackPadding = 20;

using PackRangesFn = int (*)(void *spc, const void *fontData, int fontIndex, void *ranges,
                             int numRanges);
PackRangesFn g_packRanges = nullptr;
bool g_packLogged = false;

int PackRangesHook(void *spc, const void *fontData, int fontIndex, void *ranges, int numRanges) {
    if (spc != nullptr) {
        auto *words = static_cast<int32_t *>(spc);
        const bool knownLayout = words[4] == kAtlasWidth && words[5] == kAtlasHeight &&
                                 words[6] == kAtlasWidth;
        if (!g_packLogged) {
            g_packLogged = true;
            Log("[pack] ctx=%p words=%d %d %d %d %d %d %d %d layout=%s", spc, words[0], words[1],
                words[2], words[3], words[4], words[5], words[6], words[7],
                knownLayout ? "ok" : "unknown");
        }
        if (knownLayout) {
            auto *padding = reinterpret_cast<int32_t *>(static_cast<uint8_t *>(spc) +
                                                        kPackPaddingOffset);
            if (*padding >= 0 && *padding < kPackPadding) {
                Log("[pack] padding %d -> %d", *padding, kPackPadding);
                *padding = kPackPadding;
            }
        }
    }
    return g_packRanges(spc, fontData, fontIndex, ranges, numRanges);
}

bool InstallPackPadding() {
    HMODULE base = GetModuleHandleA(nullptr);
    auto *site = PatchSite(base, kPackRangesRva, sizeof(kPackRangesPrologue));
    if (site == nullptr) return false;
    if (memcmp(site, kPackRangesPrologue, sizeof(kPackRangesPrologue)) != 0) {
        Log("[pack] unexpected bytes at the pack site, skipping");
        return false;
    }
    auto *stub = static_cast<uint8_t *>(AllocNear(site, 128));
    if (!stub) {
        Log("[pack] no free page within rel32 range of the pack site");
        return false;
    }
    uint8_t *entry = stub;
    entry[0] = 0x48;  // mov rax, imm64
    entry[1] = 0xB8;
    const uint64_t hook = reinterpret_cast<uint64_t>(&PackRangesHook);
    memcpy(entry + 2, &hook, 8);
    entry[10] = 0xFF;  // jmp rax
    entry[11] = 0xE0;

    uint8_t *resume = stub + 16;
    memcpy(resume, kPackRangesPrologue, sizeof(kPackRangesPrologue));
    resume[5] = 0xE9;
    const int32_t back = static_cast<int32_t>(reinterpret_cast<intptr_t>(site) + 5 -
                                              reinterpret_cast<intptr_t>(resume + 10));
    memcpy(resume + 6, &back, 4);
    g_packRanges = reinterpret_cast<PackRangesFn>(resume);

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, sizeof(kPackRangesPrologue), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("[pack] VirtualProtect failed: %lu", GetLastError());
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    site[0] = 0xE9;
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(entry) -
                                             reinterpret_cast<intptr_t>(site + 5));
    memcpy(site + 1, &rel, 4);
    VirtualProtect(site, sizeof(kPackRangesPrologue), oldProtect, &oldProtect);
    Log("[pack] padding hook installed: site=%p entry=%p resume=%p hook=%p padding=%d",
        static_cast<void *>(site), static_cast<void *>(entry), static_cast<void *>(resume),
        reinterpret_cast<void *>(&PackRangesHook), kPackPadding);
    return true;
}

/// CAurFont::TextOut loads one byte per glyph:
///   0x4CA06  movzx ebx, byte ptr [r12]
/// R12 is the current position and EDI the index. Replacing that load with a call to
/// a decoder lets a Shift-JIS pair produce a single atlas slot; the decoder also bumps
/// EDI so the loop advances two bytes for such a pair.
constexpr uintptr_t kTextOutLoadRva = 0x4CA06u;
constexpr uint8_t kTextOutLoadOriginal[5] = { 0x41, 0x0F, 0xB6, 0x1C, 0x24 };

/// Builds and installs the Shift-JIS decoder that TextOut calls instead of its
/// single-byte load. Returns true when the patch is in place.
bool InstallTextOutDecoder() {
    HMODULE base = GetModuleHandleA(nullptr);
    auto *site = PatchSite(base, kTextOutLoadRva, sizeof(kTextOutLoadOriginal));
    if (site == nullptr) return false;
    if (memcmp(site, kTextOutLoadOriginal, sizeof(kTextOutLoadOriginal)) != 0) {
        Log("[textout] unexpected bytes at the load site, skipping");
        return false;
    }

    auto *stub = static_cast<uint8_t *>(AllocNear(site, 64));
    if (!stub) {
        Log("[textout] no free page within rel32 range of the load site");
        return false;
    }
    const intptr_t stubDistance = reinterpret_cast<intptr_t>(stub) -
                                  reinterpret_cast<intptr_t>(site + 5);
    if (stubDistance < INT32_MIN || stubDistance > INT32_MAX) {
        Log("[textout] stub at %p is outside rel32 range", static_cast<void *>(stub));
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }

    size_t i = 0;
    const auto emit = [&](std::initializer_list<int> bytes) {
        for (int b : bytes) stub[i++] = static_cast<uint8_t>(b);
    };
    // Decode one UTF-8 character at [r12]. ASCII stays a single byte so the markup
    // check on BL keeps working; two and three byte forms become an atlas slot and
    // bump EDI so the caller's per-iteration increment finishes the character.
    emit({ 0x41, 0x0F, 0xB6, 0x04, 0x24 });                         // movzx eax, byte [r12]
    emit({ 0x3C, 0x80 });                                           // cmp al, 0x80
    emit({ 0x72, 0x56 });                                           // jb single
    emit({ 0x3C, 0xE0 });                                           // cmp al, 0xE0
    emit({ 0x72, 0x24 });                                           // jb two_byte
    emit({ 0x41, 0x0F, 0xB6, 0x4C, 0x24, 0x01 });                   // movzx ecx, byte [r12+1]
    emit({ 0x41, 0x0F, 0xB6, 0x54, 0x24, 0x02 });                   // movzx edx, byte [r12+2]
    emit({ 0x83, 0xE0, 0x0F });                                     // and eax, 0x0F
    emit({ 0xC1, 0xE0, 0x0C });                                     // shl eax, 12
    emit({ 0x83, 0xE1, 0x3F });                                     // and ecx, 0x3F
    emit({ 0xC1, 0xE1, 0x06 });                                     // shl ecx, 6
    emit({ 0x83, 0xE2, 0x3F });                                     // and edx, 0x3F
    emit({ 0x09, 0xC8 });                                           // or eax, ecx
    emit({ 0x09, 0xD0 });                                           // or eax, edx
    emit({ 0x83, 0xC7, 0x02 });                                     // add edi, 2
    emit({ 0xEB, 0x13 });                                           // jmp lookup
    emit({ 0x41, 0x0F, 0xB6, 0x4C, 0x24, 0x01 });                   // two_byte: movzx ecx, byte [r12+1]
    emit({ 0x83, 0xE0, 0x1F });                                     // and eax, 0x1F
    emit({ 0xC1, 0xE0, 0x06 });                                     // shl eax, 6
    emit({ 0x83, 0xE1, 0x3F });                                     // and ecx, 0x3F
    emit({ 0x09, 0xC8 });                                           // or eax, ecx
    emit({ 0xFF, 0xC7 });                                           // inc edi
    emit({ 0x48, 0xBA });                                           // lookup: mov rdx, imm64
    {
        const uint64_t table = reinterpret_cast<uint64_t>(&kNwnjpCodepointSlot[0]);
        memcpy(stub + i, &table, 8);
        i += 8;
    }
    emit({ 0x0F, 0xB7, 0x1C, 0x42 });                               // movzx ebx, word [rdx+rax*2]
    emit({ 0x66, 0x81, 0xFB, 0xFF, 0xFF });                         // cmp bx, 0xFFFF
    emit({ 0x75, 0x0C });                                           // jne done
    emit({ 0x41, 0x0F, 0xB6, 0x1C, 0x24 });                         // fallback: movzx ebx, byte [r12]
    emit({ 0xC3 });                                                 // ret
    emit({ 0x41, 0x0F, 0xB6, 0x1C, 0x24 });                         // single: movzx ebx, byte [r12]
    emit({ 0xC3 });                                                 // ret
    emit({ 0xC3 });                                                 // done: ret

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, sizeof(kTextOutLoadOriginal), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("[textout] VirtualProtect failed: %lu", GetLastError());
        return false;
    }
    site[0] = 0xE8; // call stub
    const int32_t rel = static_cast<int32_t>(stubDistance);
    memcpy(site + 1, &rel, 4);
    VirtualProtect(site, sizeof(kTextOutLoadOriginal), oldProtect, &oldProtect);
    Log("[textout] decoder installed: site=%p stub=%p rel=0x%X (%zu bytes)",
        static_cast<void *>(site), static_cast<void *>(stub),
        static_cast<unsigned int>(static_cast<uint32_t>(rel)), i);
    return true;
}


/// CAurFontInfo::GetSymbolCoords and SetSymbolCoords reject any glyph index above
/// 0xFF, so the Japanese slots the decoder produces would come back empty. Both
/// comparisons are widened to the baked glyph count.
constexpr uintptr_t kGetSymbolBoundRva = 0xEBB20u;
constexpr uintptr_t kSetSymbolBoundRva = 0xEDF9Fu;

void PatchGlyphBounds() {
    HMODULE base = GetModuleHandleA(nullptr);
    const uint32_t limit = static_cast<uint32_t>(sizeof(kNwnjpLegacyCodepoints) / sizeof(kNwnjpLegacyCodepoints[0]));
    for (uintptr_t rva : { kGetSymbolBoundRva, kSetSymbolBoundRva }) {
        const uint8_t expected[6] = { 0x81, 0xFA, 0xFF, 0x00, 0x00, 0x00 };
        auto *site = PatchSite(base, rva, sizeof(expected));
        if (site == nullptr) continue;
        if (memcmp(site, expected, sizeof(expected)) != 0) {
            Log("[bounds] unexpected bytes at rva 0x%llX", static_cast<unsigned long long>(rva));
            continue;
        }
        DWORD oldProtect = 0;
        if (!VirtualProtect(site, sizeof(expected), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            Log("[bounds] VirtualProtect failed: %lu", GetLastError());
            continue;
        }
        memcpy(site + 2, &limit, 4);
        VirtualProtect(site, sizeof(expected), oldProtect, &oldProtect);
        Log("[bounds] rva 0x%llX -> %u", static_cast<unsigned long long>(rva), limit);
    }
}

/// True when `address` itself is backed by committed, readable memory.
bool IsReadable(const void *address) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) return false;
    if (info.State != MEM_COMMIT) return false;
    return (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) == 0;
}

/// Logs the faulting instruction and registers so a crash can be traced back to the
/// stub or to engine code without relying on the game's own report.
///
/// The game installs its own unhandled exception filter once it is up, which replaces
/// anything set from DllMain, so the handler has to be vectored: those are consulted
/// before the frame-based chain and cannot be displaced.
LONG WINAPI LogCrash(EXCEPTION_POINTERS *info) {
    if (info == nullptr || info->ExceptionRecord == nullptr) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    const bool fatal = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
                       code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW ||
                       code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_PRIV_INSTRUCTION;
    if (!fatal) return EXCEPTION_CONTINUE_SEARCH;

    HMODULE base = GetModuleHandleA(nullptr);
    const CONTEXT *c = info->ContextRecord;
    LogTry("[crash] code=0x%08lX addr=%p base=%p", code,
           info->ExceptionRecord->ExceptionAddress, static_cast<void *>(base));
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2) {
        LogTry("[crash] access=%llu target=%p",
               static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[0]),
               reinterpret_cast<void *>(info->ExceptionRecord->ExceptionInformation[1]));
    }
    if (c != nullptr) {
        LogTry("[crash] rip=%p (+0x%llX) rsp=%p rbp=%p",
               reinterpret_cast<void *>(c->Rip),
               base ? static_cast<unsigned long long>(c->Rip - reinterpret_cast<uintptr_t>(base)) : 0ull,
               reinterpret_cast<void *>(c->Rsp), reinterpret_cast<void *>(c->Rbp));
        LogTry("[crash] rax=%p rbx=%p rcx=%p rdx=%p", reinterpret_cast<void *>(c->Rax),
               reinterpret_cast<void *>(c->Rbx), reinterpret_cast<void *>(c->Rcx),
               reinterpret_cast<void *>(c->Rdx));
        LogTry("[crash] rsi=%p rdi=%p r8=%p r9=%p", reinterpret_cast<void *>(c->Rsi),
               reinterpret_cast<void *>(c->Rdi), reinterpret_cast<void *>(c->R8),
               reinterpret_cast<void *>(c->R9));
        LogTry("[crash] r10=%p r11=%p r12=%p r13=%p", reinterpret_cast<void *>(c->R10),
               reinterpret_cast<void *>(c->R11), reinterpret_cast<void *>(c->R12),
               reinterpret_cast<void *>(c->R13));
        LogTry("[crash] r14=%p r15=%p", reinterpret_cast<void *>(c->R14),
               reinterpret_cast<void *>(c->R15));
        if (IsReadable(reinterpret_cast<const void *>(c->Rsp))) {
            const auto *stack = reinterpret_cast<const uint64_t *>(c->Rsp);
            LogTry("[crash] stack=%p %p %p %p", reinterpret_cast<void *>(stack[0]),
                   reinterpret_cast<void *>(stack[1]), reinterpret_cast<void *>(stack[2]),
                   reinterpret_cast<void *>(stack[3]));
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
constexpr uintptr_t kGuiTextBubbleSetTextRva = 0x1FD690u;
constexpr uint8_t kGuiTextBubbleSetTextPrologue[5] = { 0x48, 0x89, 0x5C, 0x24, 0x18 };
constexpr uintptr_t kStringGobSetMaxWidthRva = 0x4A8C0u;
constexpr size_t kGuiTextBubbleStringOffset = 0xF0;
constexpr size_t kGuiTextBubbleModelWidthOffset = 0xF8;

using GuiTextFn = void (*)(void *self, const char *text);
using SetFloatFn = void (*)(void *self, float value);

GuiTextFn g_bubbleSetText = nullptr;
GuiTextFn g_stringSetText = nullptr;
SetFloatFn g_stringSetMaxWidth = nullptr;

constexpr uintptr_t kStringGobSetTextRva = 0x4AAE0u;
void GuiTextBubbleSetTextHook(void *self, const char *text) {
    if (g_bubbleSetText != nullptr) g_bubbleSetText(self, text);
    if (text == nullptr || g_stringSetMaxWidth == nullptr || g_stringSetText == nullptr) return;
    bool nonAscii = false;
    for (const char *p = text; *p != '\0'; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80) {
            nonAscii = true;
            break;
        }
    }
    if (!nonAscii) return;
    auto *bubble = static_cast<uint8_t *>(self);
    void *string = *reinterpret_cast<void **>(bubble + kGuiTextBubbleStringOffset);
    if (string == nullptr) return;
    const float width = *reinterpret_cast<float *>(bubble + kGuiTextBubbleModelWidthOffset);
    if (width <= 0.0f) return;
    g_stringSetMaxWidth(string, width * 0.01f);
    g_stringSetText(string, text);
}

bool InstallBubbleTextReflow() {
    HMODULE base = GetModuleHandleA(nullptr);
    auto *site = PatchSite(base, kGuiTextBubbleSetTextRva, sizeof(kGuiTextBubbleSetTextPrologue));
    if (site == nullptr) return false;
    if (memcmp(site, kGuiTextBubbleSetTextPrologue, sizeof(kGuiTextBubbleSetTextPrologue)) != 0) {
        Log("[bubble] unexpected bytes at SetText, skipping");
        return false;
    }
    auto *stub = static_cast<uint8_t *>(AllocNear(site, 128));
    if (!stub) {
        Log("[bubble] no free page within rel32 range of SetText");
        return false;
    }
    uint8_t *entry = stub;
    entry[0] = 0x48;
    entry[1] = 0xB8;
    const uint64_t hook = reinterpret_cast<uint64_t>(&GuiTextBubbleSetTextHook);
    memcpy(entry + 2, &hook, 8);
    entry[10] = 0xFF;
    entry[11] = 0xE0;
    uint8_t *resume = stub + 16;
    memcpy(resume, kGuiTextBubbleSetTextPrologue, sizeof(kGuiTextBubbleSetTextPrologue));
    resume[5] = 0xE9;
    const int32_t back = static_cast<int32_t>(reinterpret_cast<intptr_t>(site) + 5 -
                                              reinterpret_cast<intptr_t>(resume + 10));
    memcpy(resume + 6, &back, 4);
    g_bubbleSetText = reinterpret_cast<GuiTextFn>(resume);
    g_stringSetMaxWidth =
        reinterpret_cast<SetFloatFn>(reinterpret_cast<uint8_t *>(base) + kStringGobSetMaxWidthRva);
    g_stringSetText =
        reinterpret_cast<GuiTextFn>(reinterpret_cast<uint8_t *>(base) + kStringGobSetTextRva);
    DWORD oldProtect = 0;
    if (!VirtualProtect(site, sizeof(kGuiTextBubbleSetTextPrologue), PAGE_EXECUTE_READWRITE,
                        &oldProtect)) {
        Log("[bubble] VirtualProtect failed: %lu", GetLastError());
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    site[0] = 0xE9;
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(entry) -
                                             reinterpret_cast<intptr_t>(site + 5));
    memcpy(site + 1, &rel, 4);
    VirtualProtect(site, sizeof(kGuiTextBubbleSetTextPrologue), oldProtect, &oldProtect);
    Log("[bubble] text reflow installed: site=%p entry=%p resume=%p",
        static_cast<void *>(site), static_cast<void *>(entry), static_cast<void *>(resume));
    return true;
}

/// The bubble keeps the first model where SetText reports success, but SetText reports
/// failure for Japanese text even when the glyphs fit (it measures bytes), so short names
/// jump to a wide model. Steer the choice by glyph width: keep reporting failure until the
/// model width class matches the natural text width; the loop then picks the height itself.
constexpr uint8_t kStringGobSetTextPrologue[5] = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
constexpr size_t kCaurStringFontOffset = 0x10;      // CAurString base + 0x4D0
constexpr size_t kCaurStringMaxWidthOffset = 0x74;  // CAurString base + 0x534

using StringSetTextFn = bool (*)(void *self, const char *text);
StringSetTextFn g_stringSetTextOriginal = nullptr;

/// Widest visible line of `text`, markup skipped and lines split on '\n'. The bubble's
/// model loop compares candidate widths against the engine's byte measurement, which
/// counts a Japanese line up to three times too wide and pushes a short name onto a wide
/// panel; this gives the width the text really needs.
float MeasureWidestLine(void *font, const char *text) {
    auto *base = static_cast<uint8_t *>(font);
    auto *info = *reinterpret_cast<uint8_t **>(base + 0x28);
    if (info == nullptr) return 0.0f;
    // MeasureMarkupWidth adds one letter spacing after the last glyph; the engine stores
    // the width before it, and that one spacing is enough to push a Latin name onto the
    // next model class.
    const float spacing = *reinterpret_cast<float *>(info + 0x10);
    float widest = 0.0f;
    int32_t start = 0;
    for (int32_t i = 0;; ++i) {
        const char c = text[i];
        if (c == '\n' || c == '\0') {
            float width = MeasureMarkupWidth(font, text + start, i - start);
            if (width > spacing) width -= spacing;
            if (width > widest) widest = width;
            if (c == '\0') break;
            start = i + 1;
        }
    }
    return widest;
}

bool StringSetTextSteerHook(void *self, const char *text) {
    const bool result =
        g_stringSetTextOriginal != nullptr ? g_stringSetTextOriginal(self, text) : false;
    if (text == nullptr || self == nullptr || g_getSymbolCoords == nullptr) return result;
    bool nonAscii = false;
    for (const char *p = text; *p != '\0'; ++p) {
        if (static_cast<unsigned char>(*p) >= 0x80) {
            nonAscii = true;
            break;
        }
    }
    if (!nonAscii) return result;
    auto *caller = static_cast<uint8_t *>(_ReturnAddress());
    auto *module = reinterpret_cast<uint8_t *>(GetModuleHandleA(nullptr));
    if (caller < module + kGuiTextBubbleSetTextRva || caller >= module + 0x1FE000) return result;
    void *font = *reinterpret_cast<void **>(static_cast<uint8_t *>(self) + kCaurStringFontOffset);
    if (font == nullptr) return result;
    const float natural = MeasureWidestLine(font, text);
    const float current =
        *reinterpret_cast<float *>(static_cast<uint8_t *>(self) + kCaurStringMaxWidthOffset);
    // Reporting failure is what stops the model loop, so keep reporting success until the
    // candidate width can hold the text; the loop then picks that model's height itself.
    return current + 0.001f < natural;
}

bool InstallSetTextSteer() {
    HMODULE base = GetModuleHandleA(nullptr);
    auto *site = PatchSite(base, kStringGobSetTextRva, sizeof(kStringGobSetTextPrologue));
    if (site == nullptr) return false;
    if (memcmp(site, kStringGobSetTextPrologue, sizeof(kStringGobSetTextPrologue)) != 0) {
        Log("[steer] unexpected bytes at StringGob::SetText, skipping");
        return false;
    }
    auto *stub = static_cast<uint8_t *>(AllocNear(site, 128));
    if (!stub) {
        Log("[steer] no free page within rel32 range");
        return false;
    }
    uint8_t *entry = stub;
    entry[0] = 0x48;
    entry[1] = 0xB8;
    const uint64_t hook = reinterpret_cast<uint64_t>(&StringSetTextSteerHook);
    memcpy(entry + 2, &hook, 8);
    entry[10] = 0xFF;
    entry[11] = 0xE0;
    uint8_t *resume = stub + 16;
    memcpy(resume, kStringGobSetTextPrologue, sizeof(kStringGobSetTextPrologue));
    resume[5] = 0xE9;
    const int32_t back = static_cast<int32_t>(reinterpret_cast<intptr_t>(site) + 5 -
                                              reinterpret_cast<intptr_t>(resume + 10));
    memcpy(resume + 6, &back, 4);
    g_stringSetTextOriginal = reinterpret_cast<StringSetTextFn>(resume);
    DWORD oldProtect = 0;
    if (!VirtualProtect(site, sizeof(kStringGobSetTextPrologue), PAGE_EXECUTE_READWRITE,
                        &oldProtect)) {
        Log("[steer] VirtualProtect failed: %lu", GetLastError());
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    site[0] = 0xE9;
    const int32_t rel = static_cast<int32_t>(reinterpret_cast<intptr_t>(entry) -
                                             reinterpret_cast<intptr_t>(site + 5));
    memcpy(site + 1, &rel, 4);
    VirtualProtect(site, sizeof(kStringGobSetTextPrologue), oldProtect, &oldProtect);
    Log("[steer] installed: site=%p entry=%p resume=%p", static_cast<void *>(site),
        static_cast<void *>(entry), static_cast<void *>(resume));
    return true;
}

/// nk_sdl_refresh_config picks a static code point range table per locale with
/// `lea reg,[rip+disp32]`; the shipped tables cover only Latin, so no Japanese glyph ever
/// reaches the NUI atlas. Steer every load at our own table. The replacement table does
/// not fit in the space the engine reserved, so it lives on a page allocated near the
/// sites (the displacement is only 32 bits).
constexpr uintptr_t kRangeLoadSites[] = { 0xA70FE3u, 0xA71136u, 0xA71140u };
constexpr size_t kRangeLoadInstructionBytes = 7;

bool RelocateNuiRangeTable() {
    HMODULE base = GetModuleHandleA(nullptr);
    if (base == nullptr) return false;
    uint8_t *sites[sizeof(kRangeLoadSites) / sizeof(kRangeLoadSites[0])] = {};
    size_t siteCount = 0;
    for (uintptr_t siteRva : kRangeLoadSites) {
        auto *site = PatchSite(base, siteRva, kRangeLoadInstructionBytes);
        if (site == nullptr) return false;
        // 48 8D /r with mod=00 rm=101 is `lea reg,[rip+disp32]`; the register varies.
        if (site[0] != 0x48 || site[1] != 0x8D || (site[2] & 0xC7) != 0x05) {
            Log("[nui] unexpected bytes at range load site 0x%llX, skipping",
                static_cast<unsigned long long>(siteRva));
            return false;
        }
        sites[siteCount++] = site;
    }
    const size_t bytes = sizeof(kNwnjpNuiRangeTable);
    auto *table = static_cast<uint8_t *>(
        AllocNear(reinterpret_cast<uint8_t *>(base) + kRangeLoadSites[0], bytes));
    if (table == nullptr) {
        Log("[nui] no free page within rel32 range of the range load sites");
        return false;
    }
    memcpy(table, kNwnjpNuiRangeTable, bytes);
    for (size_t index = 0; index < siteCount; ++index) {
        auto *site = sites[index];
        DWORD oldProtect = 0;
        if (!VirtualProtect(site, kRangeLoadInstructionBytes, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            Log("[nui] VirtualProtect failed: %lu", GetLastError());
            VirtualFree(table, 0, MEM_RELEASE);
            return false;
        }
        const int32_t displacement = static_cast<int32_t>(
            reinterpret_cast<intptr_t>(table) - reinterpret_cast<intptr_t>(site + kRangeLoadInstructionBytes));
        memcpy(site + 3, &displacement, sizeof(displacement));
        VirtualProtect(site, kRangeLoadInstructionBytes, oldProtect, &oldProtect);
        Log("[nui] range table %p at site 0x%llX (disp %d)", static_cast<void *>(table),
            static_cast<unsigned long long>(kRangeLoadSites[index]), displacement);
    }
    return true;
}

/// Encoding::ToUTF8 re-encodes each byte through a code page table, so dialogue that is
/// already UTF-8 comes out double encoded. The branch at the comparison below chooses the
/// plain byte copy; neutralising it sends every byte that way.
constexpr uintptr_t kToUtf8BranchRva = 0x2F1B26u;
constexpr uint8_t kToUtf8BranchOriginal[2] = { 0x77, 0x38 };  // ja +0x38
constexpr uint8_t kToUtf8BranchNeutral[2] = { 0x90, 0x90 };   // nop nop

bool NeutraliseToUtf8Branch() {
    HMODULE base = GetModuleHandleA(nullptr);
    auto *site = PatchSite(base, kToUtf8BranchRva, sizeof(kToUtf8BranchNeutral));
    if (site == nullptr) return false;
    if (memcmp(site, kToUtf8BranchNeutral, sizeof(kToUtf8BranchNeutral)) == 0) {
        Log("[utf8] branch already neutralised");
        return true;
    }
    if (memcmp(site, kToUtf8BranchOriginal, sizeof(kToUtf8BranchOriginal)) != 0) {
        Log("[utf8] unexpected bytes at the ToUTF8 branch, skipping");
        return false;
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(site, sizeof(kToUtf8BranchNeutral), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("[utf8] VirtualProtect failed: %lu", GetLastError());
        return false;
    }
    memcpy(site, kToUtf8BranchNeutral, sizeof(kToUtf8BranchNeutral));
    VirtualProtect(site, sizeof(kToUtf8BranchNeutral), oldProtect, &oldProtect);
    Log("[utf8] ToUTF8 branch neutralised at 0x%llX",
        static_cast<unsigned long long>(kToUtf8BranchRva));
    return true;
}

/// Encoding::FromUTF8 is the reverse direction of ToUTF8: it decodes every UTF-8 sequence
/// and looks the code point up in the same fixed table, which has no Japanese entries, so
/// each character comes back as '?'. That hits text the game only renders through this path
/// (the PEPS widget's hover label showed "   ??? ??????????" for a henchman name). The
/// process treats UTF-8 as its ANSI code page, so both directions are byte copies, and the
/// char* overload of ToUTF8 already is one once kToUtf8BranchRva is neutralised. Redirecting
/// FromUTF8's entry at ToUTF8 keeps the engine's own std::string construction (same
/// allocator, same layout); the std::string overload of FromUTF8 only forwards src.c_str()
/// to this entry.
constexpr uintptr_t kFromUtf8Rva = 0x2F0EA0u;
constexpr uintptr_t kToUtf8EntryRva = 0x2F19A0u;
constexpr uint8_t kFromUtf8Prologue[5] = { 0x48, 0x8B, 0xC4, 0x55, 0x57 };  // mov rax,rsp; push rbp; push rdi

bool RedirectFromUtf8ToByteCopy() {
    HMODULE base = GetModuleHandleA(nullptr);
    auto *site = PatchSite(base, kFromUtf8Rva, sizeof(kFromUtf8Prologue));
    auto *target = PatchSite(base, kToUtf8EntryRva, 1);
    if (site == nullptr || target == nullptr) return false;
    const int32_t rel = static_cast<int32_t>(target - (site + sizeof(kFromUtf8Prologue)));
    uint8_t jump[sizeof(kFromUtf8Prologue)] = { 0xE9 };
    memcpy(jump + 1, &rel, sizeof(rel));
    if (memcmp(site, jump, sizeof(jump)) == 0) {
        Log("[utf8] FromUTF8 already redirected to ToUTF8");
        return true;
    }
    if (memcmp(site, kFromUtf8Prologue, sizeof(kFromUtf8Prologue)) != 0) {
        Log("[utf8] unexpected bytes at the FromUTF8 entry, skipping");
        return false;
    }
    DWORD oldProtect = 0;
    if (!VirtualProtect(site, sizeof(jump), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log("[utf8] VirtualProtect failed: %lu", GetLastError());
        return false;
    }
    memcpy(site, jump, sizeof(jump));
    VirtualProtect(site, sizeof(jump), oldProtect, &oldProtect);
    Log("[utf8] FromUTF8 redirected to ToUTF8 (rel %d)", rel);
    return true;
}

/// The process code page used to be raised by the executable's manifest
/// (activeCodePage=UTF-8). The loader has already run by the time this DLL is injected,
/// but the code page itself is only a global that GetACP reads:
///   mov eax, dword ptr [rip+disp32]
///   ret
/// so writing it here gives the process the same UTF-8 ANSI code page, which is what
/// every CP_ACP conversion and every GetACP-based decision in the engine then sees.
constexpr uint32_t kUtf8CodePage = 65001;

bool RaiseProcessCodePage() {
    HMODULE kernelbase = GetModuleHandleA("kernelbase.dll");
    if (kernelbase == nullptr) {
        Log("[acp] kernelbase not loaded, leaving the code page alone");
        return false;
    }
    const auto *getAcp = reinterpret_cast<const uint8_t *>(GetProcAddress(kernelbase, "GetACP"));
    if (getAcp == nullptr) {
        Log("[acp] GetACP not found, leaving the code page alone");
        return false;
    }
    if (getAcp[0] != 0x8B || getAcp[1] != 0x05) {
        Log("[acp] GetACP is not a single global load, leaving the code page alone");
        return false;
    }
    int32_t displacement = 0;
    memcpy(&displacement, getAcp + 2, sizeof(displacement));
    auto *global = reinterpret_cast<uint32_t *>(const_cast<uint8_t *>(getAcp) + 6 + displacement);
    DWORD oldProtect = 0;
    if (!VirtualProtect(global, sizeof(*global), PAGE_READWRITE, &oldProtect)) {
        Log("[acp] VirtualProtect failed: %lu", GetLastError());
        return false;
    }
    const uint32_t previous = *global;
    *global = kUtf8CodePage;
    VirtualProtect(global, sizeof(*global), oldProtect, &oldProtect);
    Log("[acp] code page %lu -> %lu (global=%p, GetACP now %lu)", previous, kUtf8CodePage,
        static_cast<void *>(global), GetACP());
    return true;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        InitializeCriticalSection(&g_logLock);
        ResetLog();
        g_logReady = true;
        Log("=== nwnjp hook attached === base=%p pid=%lu",
            static_cast<void *>(GetModuleHandleA(nullptr)), GetCurrentProcessId());
        AddVectoredExceptionHandler(1, LogCrash);
        RaiseProcessCodePage();
        RelocateNuiRangeTable();
        NeutraliseToUtf8Branch();
        RedirectFromUtf8ToByteCopy();
        PatchGlyphBounds();
        InstallAtlasSizing();
        InstallPackPadding();
        InstallVisibleLengthFix();
        InstallBubbleTextReflow();
        InstallSetTextSteer();
        InstallTextOutDecoder();
        if (HANDLE thread = CreateThread(nullptr, 0, InstallHook, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        g_logReady = false;
        DeleteCriticalSection(&g_logLock);
    }
    return TRUE;
}





