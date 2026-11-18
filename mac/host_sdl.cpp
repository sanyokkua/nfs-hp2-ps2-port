// SpeedBreaker HP2. GPL-3.0-or-later.
//
// The macOS host: the process's main thread owns an SDL3 window (AppKit wants its windows and events there) with a Metal view for
// MoltenVK, and plays the part nfshp2-web's page plays in the browser: it writes the runtime's input block (host_input.h: keys by
// Windows virtual-key code, the mouse in 0..1 of the view, two gamepads, the view's pixels) and reads back the game's wishes (the
// pointer, the motors). The game itself runs on a thread of its own: ps2.cpp's main, built as hp2_game_main (HP2_HOST_MAIN).
#include "host_input.h"
#include "host_mac.h"
#include "installer.h"
#include "pcconfig.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>

#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int hp2_game_main();
void host_input_publish() {}
void pc_save_on_exit();
void host_log_flush();

namespace {
SDL_Window *g_window;
std::atomic<void *> g_layer{nullptr};
std::atomic<int> g_px_w{0}, g_px_h{0}, g_hz{0};

// ---- keys: SDL's scancodes (the key's place, as Windows' virtual keys mostly are) as virtual-key codes ----
int vk_of(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return 0x70 + (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return 0x61 + (sc - SDL_SCANCODE_KP_1);
    switch (sc) {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_KP_0: return 0x60;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x0D;
    case SDL_SCANCODE_ESCAPE: return 0x1B;
    case SDL_SCANCODE_BACKSPACE: return 0x08;
    case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_SPACE: return 0x20;
    case SDL_SCANCODE_LEFT: return 0x25;
    case SDL_SCANCODE_UP: return 0x26;
    case SDL_SCANCODE_RIGHT: return 0x27;
    case SDL_SCANCODE_DOWN: return 0x28;
    case SDL_SCANCODE_PAGEUP: return 0x21;
    case SDL_SCANCODE_PAGEDOWN: return 0x22;
    case SDL_SCANCODE_END: return 0x23;
    case SDL_SCANCODE_HOME: return 0x24;
    case SDL_SCANCODE_INSERT: return 0x2D;
    case SDL_SCANCODE_DELETE: return 0x2E;
    case SDL_SCANCODE_CAPSLOCK: return 0x14;
    case SDL_SCANCODE_LSHIFT: return 0xA0;
    case SDL_SCANCODE_RSHIFT: return 0xA1;
    case SDL_SCANCODE_LCTRL: return 0xA2;
    case SDL_SCANCODE_RCTRL: return 0xA3;
    case SDL_SCANCODE_LALT: return 0xA4;
    case SDL_SCANCODE_RALT: return 0xA5;
    case SDL_SCANCODE_LGUI: return 0x5B;
    case SDL_SCANCODE_RGUI: return 0x5C;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x6A;
    case SDL_SCANCODE_KP_PLUS: return 0x6B;
    case SDL_SCANCODE_KP_MINUS: return 0x6D;
    case SDL_SCANCODE_KP_PERIOD: return 0x6E;
    case SDL_SCANCODE_KP_DIVIDE: return 0x6F;
    case SDL_SCANCODE_SEMICOLON: return 0xBA;
    case SDL_SCANCODE_EQUALS: return 0xBB;
    case SDL_SCANCODE_COMMA: return 0xBC;
    case SDL_SCANCODE_MINUS: return 0xBD;
    case SDL_SCANCODE_PERIOD: return 0xBE;
    case SDL_SCANCODE_SLASH: return 0xBF;
    case SDL_SCANCODE_GRAVE: return 0xC0;
    case SDL_SCANCODE_LEFTBRACKET: return 0xDB;
    case SDL_SCANCODE_BACKSLASH: return 0xDC;
    case SDL_SCANCODE_RIGHTBRACKET: return 0xDD;
    case SDL_SCANCODE_APOSTROPHE: return 0xDE;
    default: return 0;
    }
}

void store(int32_t &to, int32_t v) { __atomic_store_n(&to, v, __ATOMIC_RELEASE); }

void key_event(HostInput &in, const SDL_KeyboardEvent &k)
{
    const int vk = vk_of(k.scancode);
    if (!vk) return;
    __atomic_store_n(&in.keys[vk], (uint8_t)(k.down ? 1 : 0), __ATOMIC_RELEASE);
    store(in.caps_lock, (SDL_GetModState() & SDL_KMOD_CAPS) ? 1 : 0);
    const uint32_t head = __atomic_load_n(&in.ev_head, __ATOMIC_RELAXED);
    in.ev[head % 64] = {vk, k.down ? 1 : 0, k.repeat ? 1 : 0};
    __atomic_store_n(&in.ev_head, head + 1, __ATOMIC_RELEASE);
}

void view_changed(HostInput &in)
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(g_window, &w, &h);
    if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) w = h = 0;
    g_px_w = w, g_px_h = h;
    store(in.view_w, w > 0 ? w : 1), store(in.view_h, h > 0 ? h : 1);
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(g_window));
    g_hz = mode && mode->refresh_rate > 0 ? (int)(mode->refresh_rate + 0.5f) : 0;
}

void mouse_moved(HostInput &in, float x, float y)
{
    int w = 0, h = 0;
    SDL_GetWindowSize(g_window, &w, &h);
    const bool inside = w > 0 && h > 0 && x >= 0 && y >= 0 && x < w && y < h;
    float mx = inside ? x / w : -1.0f, my = inside ? y / h : -1.0f;
    __atomic_store(&in.mouse_x, &mx, __ATOMIC_RELEASE);
    __atomic_store(&in.mouse_y, &my, __ATOMIC_RELEASE);
    __atomic_add_fetch(&in.mouse_moves, 1, __ATOMIC_RELEASE);
}

// ---- gamepads: the first two SDL has, as pad.cpp's XInput-style mask ----
SDL_Gamepad *g_pads[2];
int32_t g_rumble_sent[2] = {-1, -1};
Uint64 g_rumble_at[2];

void pads_open()
{
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    for (int slot = 0; slot < 2; slot++) {
        if (g_pads[slot] && SDL_GamepadConnected(g_pads[slot])) continue;
        if (g_pads[slot]) SDL_CloseGamepad(g_pads[slot]), g_pads[slot] = nullptr;
        for (int i = 0; ids && i < count; i++) {
            SDL_Gamepad *taken = SDL_GetGamepadFromID(ids[i]);
            if (taken && (taken == g_pads[0] || taken == g_pads[1])) continue;
            if ((g_pads[slot] = SDL_OpenGamepad(ids[i]))) {
                fprintf(stderr, "host: pad %d is %s\n", slot + 1, SDL_GetGamepadName(g_pads[slot]));
                break;
            }
        }
    }
    SDL_free(ids);
}

void pads_frame(HostInput &in)
{
    static const struct { SDL_GamepadButton b; int32_t bit; } kButtons[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, 0x1}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0x2}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0x4},
        {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0x8}, {SDL_GAMEPAD_BUTTON_START, 0x10}, {SDL_GAMEPAD_BUTTON_BACK, 0x20},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, 0x40}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0x80}, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0x100},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0x200}, {SDL_GAMEPAD_BUTTON_SOUTH, 0x1000}, {SDL_GAMEPAD_BUTTON_EAST, 0x2000},
        {SDL_GAMEPAD_BUTTON_WEST, 0x4000}, {SDL_GAMEPAD_BUTTON_NORTH, 0x8000}};
    for (int slot = 0; slot < 2; slot++) {
        HostPad &p = in.pads[slot];
        SDL_Gamepad *g = g_pads[slot];
        if (!g) {
            store(p.ok, 0);
            continue;
        }
        int32_t mask = 0;
        for (const auto &k : kButtons)
            if (SDL_GetGamepadButton(g, k.b)) mask |= k.bit;
        store(p.mask, mask);
        store(p.lt, SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        store(p.rt, SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        store(p.axis[0], SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX));
        store(p.axis[1], SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY));
        store(p.axis[2], SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX));
        store(p.axis[3], SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY));
        const SDL_GamepadType type = SDL_GetGamepadType(g);
        store(p.playstation, type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5);
        store(p.ok, 1);
        // the motors: the game's wish, told again while it lasts (SDL's rumble has a duration)
        const int32_t small = __atomic_load_n(&p.rumble_small, __ATOMIC_RELAXED), large = __atomic_load_n(&p.rumble_large, __ATOMIC_RELAXED);
        const int32_t want = small << 8 | large;
        const Uint64 now = SDL_GetTicks();
        if (want != g_rumble_sent[slot] || (want && now - g_rumble_at[slot] > 900)) {
            g_rumble_sent[slot] = want, g_rumble_at[slot] = now;
            SDL_RumbleGamepad(g, (Uint16)(large * 257), small ? 0xFFFF : 0, want ? 1000 : 0);
        }
    }
}

// the game's thread: a big stack, as the EE's code nests deep (the Windows build's main thread)
void *game_thread(void *)
{
    const int code = hp2_game_main();
    fprintf(stderr, "host: the game ended (%d)\n", code);
    host_log_flush();
    fflush(nullptr);
    _exit(code);
}
}   // namespace

void *host_metal_layer() { return g_layer.load(std::memory_order_acquire); }
void host_view_pixels(int &w, int &h) { w = g_px_w, h = g_px_h; }
int host_refresh_hz() { return g_hz; }

int main(int, char **)
{
    SDL_SetHint(SDL_HINT_MAC_SCROLL_MOMENTUM, "0");
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "host: SDL failed: %s\n", SDL_GetError());
        return 1;
    }
    // the window as hp2.ini will have it is not known yet (the game reads it): 4:3, the PS2's picture at twice its size
    g_window = SDL_CreateWindow("Need for Speed: Hot Pursuit 2", 1280, 960, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_METAL);
    if (!g_window) {
        fprintf(stderr, "host: no window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_MetalView view = SDL_Metal_CreateView(g_window);
    g_layer.store(view ? SDL_Metal_GetLayer(view) : nullptr, std::memory_order_release);
    // the game's files (installed from the player's disc on the first run), and the player's folder as the working directory:
    // hp2.ini, saves/ and the log go there (the runtime opens them by relative paths outside Windows)
    const std::string game_dir = install_ensure(g_window);
    if (game_dir.empty()) return 0;
    setenv("HP2_DATA", game_dir.c_str(), 1);
    if (chdir(user_dir().c_str()) != 0) fprintf(stderr, "host: no folder %s\n", user_dir().c_str());
    if (!isatty(2)) {                           // started from Finder: the log to a file, the last run's kept beside it
        mkdir("logs", 0755);
        rename("logs/hp2.log", "logs/hp2_prev.log");
        if (freopen("logs/hp2.log", "w", stderr)) setvbuf(stderr, nullptr, _IOLBF, 0);
    }
    fprintf(stderr, "host: game %s, player's folder %s\n", game_dir.c_str(), user_dir().c_str());
    HostInput &in = host_input();
    view_changed(in);
    store(in.focused, 1);
    pads_open();

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64u << 20);
    pthread_t game;
    if (pthread_create(&game, &attr, game_thread, nullptr)) {
        fprintf(stderr, "host: no thread for the game\n");
        return 1;
    }

    bool cursor_shown = true, fullscreen = false;
    for (;;) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 4)) do {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                pc_save_on_exit();          // the profile's save, as the Windows window's WM_DESTROY
                host_log_flush();
                fflush(nullptr);
                _exit(0);
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                // Cmd+Enter / Cmd+F: fullscreen (Alt+Enter on Windows); Cmd+Q is the menu's Quit
                if (e.key.down && !e.key.repeat && (e.key.mod & SDL_KMOD_GUI) &&
                    (e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_F)) {
                    g_cfg.fullscreen = !g_cfg.fullscreen;
                    pc_config_save();
                    break;
                }
                key_event(in, e.key);
                break;
            case SDL_EVENT_MOUSE_MOTION: mouse_moved(in, e.motion.x, e.motion.y); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const int bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2 : e.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
                int32_t b = __atomic_load_n(&in.mouse_buttons, __ATOMIC_RELAXED);
                store(in.mouse_buttons, e.button.down ? (b | bit) : (b & ~bit));
                mouse_moved(in, e.button.x, e.button.y);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                const int notches = (int)(e.wheel.y > 0 ? 1 : e.wheel.y < 0 ? -1 : 0) * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
                if (notches) __atomic_add_fetch(&in.wheel, notches, __ATOMIC_RELEASE);
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_GAINED: store(in.focused, 1); break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                store(in.focused, 0);
                for (auto &k : in.keys) __atomic_store_n(&k, (uint8_t)0, __ATOMIC_RELEASE);     // no key stays held behind our back
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            case SDL_EVENT_WINDOW_MINIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                view_changed(in);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_GAMEPAD_REMOVED: pads_open(); break;
            default: break;
            }
        } while (SDL_PollEvent(&e));
        pads_frame(in);
        // what the game asks of the window: its pointer, fullscreen (Options > Graphics, hp2.ini)
        const bool want_cursor = __atomic_load_n(&in.cursor_wanted, __ATOMIC_RELAXED) != 0;
        if (want_cursor != cursor_shown) (cursor_shown = want_cursor) ? SDL_ShowCursor() : SDL_HideCursor();
        const bool want_fs = g_cfg.fullscreen != 0;
        if (want_fs != fullscreen) SDL_SetWindowFullscreen(g_window, fullscreen = want_fs);
    }
}
