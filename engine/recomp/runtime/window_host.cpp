// The keyboard, the mouse and the gamepads outside Windows: window.cpp's bindings and menus as they are, with the window's messages
// replaced by the host's input block (host_input.h), which the page writes (web) or nobody does (a headless run, driven by
// HP2_KEYSCRIPT / HP2_AUTOPRESS as on Windows). The picture is the page's canvas: its size and fullscreen are the page's.
#include "hw.h"
#include "pcconfig.h"
#include "phase.h"
#include "host.h"
#include "pcmenu.h"
#include "pad.h"
#include "native.h"
#include <array>

#include "win32_lite.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

// ---- the input block ----------------------------------------------------------------------
static HostInput g_host_input = [] {
    HostInput in = {};
    in.magic = HOST_INPUT_MAGIC;
    in.version = HOST_INPUT_VERSION;
    in.size = sizeof(HostInput);
    in.mouse_x = in.mouse_y = -1;
    return in;
}();
HostInput &host_input() { return g_host_input; }

// What the Windows build asks of its window: "there is one" (non-null) and "it has the keyboard" (the page is focused). A headless run
// (HP2_NOWINDOW) has none, as on Windows.
static HWND g_hwnd;
static bool foreground() { return g_hwnd && __atomic_load_n(&g_host_input.focused, __ATOMIC_RELAXED) != 0; }

// set when a setting changed from the keyboard (Alt+Enter): the menu shows the new value
std::atomic<bool> g_pc_ui_dirty;

// The page owns fullscreen and the canvas: the settings are kept for the menu to show, the page asked through the block's size
void window_set_fullscreen(bool fs)
{
    g_cfg.fullscreen = fs;
    pc_config_save();
    g_pc_ui_dirty = true;
}

void window_set_size(int w, int h)
{
    g_cfg.win_w = w;
    g_cfg.win_h = h;
    pc_config_save();
}

void *window_handle() { return g_hwnd; }

void window_screen_size(int &w, int &h)
{
    w = g_host_input.view_w > 0 ? g_host_input.view_w : 1280;
    h = g_host_input.view_h > 0 ? g_host_input.view_h : 720;
}

void window_view_size(int &w, int &h) { window_screen_size(w, h); }

// One screen: the one the host gives (Options > Graphics > Monitor lists it alone)
int window_monitor_count() { return 1; }
void window_monitor_size(int index, int &w, int &h)
{
    w = h = 0;
    if (index == 1) window_screen_size(w, h);
}
void window_set_monitor(int index)
{
    g_cfg.monitor = index;
    pc_config_save();
    g_pc_ui_dirty = true;
}

// Changing the profile restarts the game on Windows; here the page reloads it (the profile is named in the block's answer)
void window_relaunch(const std::string &profile)
{
    fprintf(stderr, "window: relaunch with profile %s asked (not supported here)\n", profile.c_str());
}

void pc_save_on_exit();

void window_quit()
{
    window_rumble(0, 0);            // a pad's motors outlive the program
    pc_save_on_exit();              // the profile's save, as the Windows window's WM_DESTROY does after WM_CLOSE
    host_log_flush();
    fflush(nullptr);
    _exit(0);
}

// the pointer: the page shows its own while g_cursor_on (host_cursor_wanted)
static std::atomic<bool> g_cursor_on{false};    // not till a screen asks for it and the mouse moves (window_pad)
bool host_cursor_wanted() { return g_cursor_on; }

// ---- typing a name ------------------------------------------------------------------------
static std::atomic<bool> g_text_on, g_text_script_fresh;
// Enter and Esc count once let go since the typing began. The game sees a key by its state, the window by its message, which can come after:
// the Enter that opened the typing would otherwise end it at once, with the name as it was.
static std::atomic<bool> g_text_enter_free, g_text_esc_free;
static std::atomic<unsigned> g_text_in, g_text_out;
static char g_text_ring[64];

static void text_push(int ch)
{
    const unsigned at = g_text_in;
    if (at - g_text_out >= sizeof g_text_ring) return;
    g_text_ring[at % sizeof g_text_ring] = (char)ch;
    g_text_in = at + 1;
}

// a key as a character of a name: the letters, digits, space and what tKeyboard has of the rest, ! . #
static int text_char(int vk, bool shift)
{
    if (vk >= 'A' && vk <= 'Z') return shift != ((GetKeyState(VK_CAPITAL) & 1) != 0) ? vk : vk + 32;
    if (vk == '1' && shift) return '!';
    if (vk == '3' && shift) return '#';
    if (vk >= '0' && vk <= '9') return shift ? 0 : vk;
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return '0' + vk - VK_NUMPAD0;
    if (vk == VK_SPACE) return ' ';
    if (vk == VK_OEM_PERIOD || vk == VK_DECIMAL) return shift ? 0 : '.';
    if (vk == VK_BACK) return 8;
    if (vk == VK_DELETE) return 127;
    if (vk == VK_LEFT) return 1;                    // the cursor in the name
    if (vk == VK_RIGHT) return 2;
    if (vk == VK_HOME) return 3;
    if (vk == VK_END) return 4;
    if (vk == VK_RETURN) return 13;
    if (vk == VK_ESCAPE) return 27;
    return 0;
}

static void host_events_poll();

int window_text_next()
{
    host_events_poll();
    const unsigned at = g_text_out;
    if (at == g_text_in) return 0;
    const int ch = (unsigned char)g_text_ring[at % sizeof g_text_ring];
    g_text_out = at + 1;
    return ch;
}

// While a name is picked on the letter grid the keyboard's letters are kept: the first one says the player is back at the keyboard, and
// is the first typed (window_text_begin(true)). Enter, Esc and the keys that move are not: they only bring the keyboard back.
static std::atomic<bool> g_text_armed;

void window_text_arm(bool on)
{
    if (on && !g_text_armed) g_text_out = g_text_in.load();
    g_text_armed = on;
}

void window_text_begin(bool keep)
{
    if (!keep) g_text_out = g_text_in.load();
    g_text_script_fresh = true;
    // only a key that is down now is the one that opened the typing: any other counts at its first press
    g_text_enter_free = !(GetAsyncKeyState(VK_RETURN) & 0x8000);
    g_text_esc_free = !(GetAsyncKeyState(VK_ESCAPE) & 0x8000);
    g_text_on = true;
}

static bool g_cap_release;              // after an answer: the game sees nothing until it is let go, or the key just bound also moves the menu

void window_text_end()
{
    g_text_on = false;
    g_cap_release = true;
}

static std::atomic<int> g_wheel;            // notches not yet taken, up positive

// one notch of the wheel at a time, for the menus
int window_wheel()
{
    int w = g_wheel;
    if (w > 0) g_wheel--;
    else if (w < 0) g_wheel++;
    return w;
}

static std::atomic<int> g_input_kind;       // 0 the keyboard and mouse, 1 a gamepad
int window_input_kind() { return g_input_kind; }

// When the mouse last moved or clicked, by GetTickCount64; 0: not yet, or a key or a pad button since. The pointer shows for 3 seconds after
// it (window_pad), so it is there for whoever uses the mouse and gone for whoever does not.
static std::atomic<unsigned long long> g_mouse_at;
static void mouse_used() { g_mouse_at = GetTickCount64() | 1; }

// What wndproc did with the window's messages, from the block: the key presses in order (typing, the device in use), the wheel's
// notches, the mouse's moves and clicks, the page's focus. Called on every read of the pad and of the typed text, from the game's thread.
static void host_events_poll()
{
    HostInput &in = g_host_input;
    static std::mutex mx;
    std::lock_guard<std::mutex> lock(mx);
    const uint32_t head = __atomic_load_n(&in.ev_head, __ATOMIC_ACQUIRE);
    uint32_t tail = in.ev_tail;
    if (head - tail > 64) tail = head - 64;         // the page ran ahead: the oldest are gone
    for (; tail != head; tail++) {
        const HostKeyEvent e = in.ev[tail % 64];
        const int vk = e.vk & 255;
        if (e.down) {
            // Any new press on the keyboard says the player has moved to it (a repeat says nothing, nor does the system's Win)
            if (!e.repeat && vk != VK_LWIN && vk != VK_RWIN) g_input_kind = 0, g_mouse_at = 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (g_text_on)
                if (const int ch = text_char(vk, shift))
                    if ((ch != 13 || g_text_enter_free) && (ch != 27 || g_text_esc_free)) text_push(ch);
            if (!g_text_on && g_text_armed && !e.repeat)
                if (const int ch = text_char(vk, shift))
                    if (ch >= 32 && ch < 127) text_push(ch);
        } else {
            if (vk == VK_RETURN) g_text_enter_free = true;
            if (vk == VK_ESCAPE) g_text_esc_free = true;
        }
    }
    __atomic_store_n(&in.ev_tail, tail, __ATOMIC_RELEASE);
    // the mouse: a move or a click says it is in use
    static int32_t moves = -1, wheel = 0, buttons = 0;
    const int32_t m = __atomic_load_n(&in.mouse_moves, __ATOMIC_RELAXED);
    if (m != moves) {
        if (moves != -1) mouse_used();
        moves = m;
    }
    const int32_t b = __atomic_load_n(&in.mouse_buttons, __ATOMIC_RELAXED);
    if (b & ~buttons) g_input_kind = 0, mouse_used();
    buttons = b;
    const int32_t w = __atomic_load_n(&in.wheel, __ATOMIC_RELAXED);
    if (w != wheel) {
        g_input_kind = 0;
        mouse_used();
        int notches = g_wheel + (w - wheel);
        g_wheel = notches > 4 ? 4 : notches < -4 ? -4 : notches;
        wheel = w;
    }
    // a pad does not shake for a page behind another window
    pad_rumble_quiet(!foreground());
}

// ---- the mouse ---------------------------------------------------------------------
// The menus' pointer, in the GS's 512 x 448 as the 2D is laid out. HP2_MOUSESCRIPT=<file>: lines "<frame> <x> <y> [1 held]", for runs nobody is at.
bool window_mouse(float &x, float &y, bool &left)
{
    static const std::vector<std::array<int, 4>> script = [] {
        std::vector<std::array<int, 4>> v;
        if (const char *path = getenv("HP2_MOUSESCRIPT"))
            if (FILE *f = fopen(path, "r")) {
                int t, a, b, h;
                char line[64];
                while (fgets(line, sizeof line, f)) {
                    h = 0;
                    if (sscanf(line, "%d %d %d %d", &t, &a, &b, &h) >= 3) v.push_back({t, a, b, h});
                }
                fclose(f);
            }
        return v;
    }();
    if (!script.empty()) {
        static int t;
        t++;
        bool any = false;
        for (auto &s : script)
            if (t >= s[0]) x = (float)s[1], y = (float)s[2], left = s[3] != 0 && t < s[0] + 3, any = true;
        return any;
    }
    left = false;
    host_events_poll();
    if (!foreground()) return false;
    left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const HostInput &in = g_host_input;
    const float cw = (float)(in.view_w > 0 ? in.view_w : 1280), ch = (float)(in.view_h > 0 ? in.view_h : 720);
    if (in.mouse_x < 0 || in.mouse_y < 0) return false;
    const float px = in.mouse_x * cw, py = in.mouse_y * ch;
    // the box the picture is shown in (the page draws it as native_vk.cpp's blit_to_window did)
    float f = pc_racing() ? native_narrow() : 1.0f;
    float aspect = (4.0f / 3.0f) / f;
    float bw = cw, bh = ch;
    if (bw > bh * aspect) bw = bh * aspect;
    else bh = bw / aspect;
    float bx = (cw - bw) / 2, by = (ch - bh) / 2;
    float fx = (px - bx) / bw, fy = (py - by) / bh;
    x = 256 + (fx * 512 - 256) / f;
    y = fy * 444;
    return fx >= 0 && fx <= 1 && fy >= 0 && fy <= 1;
}

// ---- keyboard as a DualShock 2 ---------------------------------------------------
// Active-low button word as scePadRead reports it, sticks (RX RY LX LY) and the
// twelve pressure bytes (right left up down triangle circle cross square L1 R1 L2 R2).
// The bindings are g_cfg's (hp2.ini, the game's Options > PC Settings).
// HP2_KEYSCRIPT=<file>: lines "<frame or +N> <virtual key, hex> [held frames]", frames being the
// game's pad reads: the keyboard for repeatable runs, whatever window has the focus.
static std::atomic<bool> g_key_script_on;
static bool g_script_key[256];

static void key_script_step()
{
    struct Step { long t, hold; int vk; };
    static const std::vector<Step> steps = [] {
        std::vector<Step> v;
        const char *path = getenv("HP2_KEYSCRIPT");
        FILE *f = path ? fopen(path, "r") : nullptr;
        if (!f) return v;
        char line[128], when[32];
        while (fgets(line, sizeof line, f)) {
            Step s = {0, 6, 0};
            if (sscanf(line, "%31s %x %ld", when, &s.vk, &s.hold) < 2 || when[0] == '#') continue;
            s.t = atol(when + (when[0] == '+')) + (when[0] == '+' && !v.empty() ? v.back().t : 0);
            v.push_back(s);
        }
        fclose(f);
        return v;
    }();
    if (steps.empty()) return;
    g_key_script_on = true;
    // HP2_KEYTIME=1: the script's count goes by sixtieths of a second, not by pad reads: in a race at 30 steps a second a read is
    // worth two. Without it the same script presses its keys at other moments of a race at 30 than at 60 (the car starts later
    // and is driven twice as long), and two runs that are meant to differ in the frame rate alone differ in the driving too.
    static const bool by_time = getenv("HP2_KEYTIME") != nullptr;
    static long t = -1;
    t += by_time && pc_racing() && pc_world_rate() == 30 ? 2 : 1;
    memset(g_script_key, 0, sizeof g_script_key);
    for (auto &s : steps)
        if (t >= s.t && t < s.t + s.hold) g_script_key[s.vk & 255] = true;
}

static bool key(int vk)
{
    if (g_key_script_on) return vk && g_script_key[vk & 255];
    return vk && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool window_key_down(int vk)
{
    if (!g_key_script_on && !foreground()) return false;
    return key(vk);
}

// each PcButton: its bit in the button word, and its pressure byte (-1: none)
static const struct { int bit, pressure; } kPadBit[PCB_COUNT] = {
    {4, 2}, {6, 3}, {7, 1}, {5, 0},         // up down left right
    {14, 6}, {15, 7}, {13, 5}, {12, 4},     // cross square circle triangle
    {10, 8}, {11, 9}, {8, 10}, {9, 11},     // L1 R1 L2 R2
    {2, -1}, {3, -1}, {0, -1}};             // R3 start select

// which gamepad a player has
static int pad_slot(int port) { return g_cfg.player2 ? 1 - port : port; }

// Delete on the keyboard, Square (X on an Xbox pad) on the first gamepad: true once for each press, while the game's window has the input.
// Asked once a frame by the screen that deletes with it (the profiles).
bool window_delete_pressed()
{
    static bool was;
    bool now = false;
    if (g_hwnd && (g_key_script_on || foreground())) {
        const PadState a = pad_read(0), b = pad_read(1);      // either gamepad, as the menus take them
        now = key(VK_DELETE) || (a.ok && (a.mask & 0x4000)) || (b.ok && (b.mask & 0x4000));
    }
    const bool pressed = now && !was;
    was = now;
    return pressed;
}

void window_rumble_port(int port, int small, int large) { pad_rumble(pad_slot(port), small, large); }
void window_rumble(int small, int large)
{
    pad_rumble(0, small, large);
    pad_rumble(1, small, large);
}

// The keyboard is shared when there are two players and not a gamepad for each: player 2 has key2 and a player's keys are only their own.
// Elsewhere player 1 has all of it, the same as with no second player in the world. Set once a frame by window_pad, read by window_pad2.
enum { KEYS_ALONE, KEYS_SHARED_DRIVING, KEYS_SHARED_MENU };
static int g_keys_shared;

static int keys_shared(bool driving)
{
    if (g_cfg.player2 || pad_count() >= 2) return KEYS_ALONE;
    if (driving && pc_two_player_race()) return KEYS_SHARED_DRIVING;
    if (!driving && pc_two_player_screen()) return KEYS_SHARED_MENU;
    return KEYS_ALONE;
}

static bool is_key2(int vk)
{
    for (auto &k : g_cfg.key2)
        if (vk && (k[0] == vk || k[1] == vk)) return true;
    return false;
}

// ---- binding a key or a pad button from the menu ------------------------------------
// window_capture_begin(): the game sees no input; once everything is released the next key
// or pad button pressed is the answer (Esc cancels). Polled from the game's frame.
static std::atomic<int> g_cap;          // 0 off, 1 waiting for release, 2 waiting for a press, 3 done, 4 cancelled
static int g_cap_pad, g_cap_code, g_cap_what;

void window_capture_begin(int what)
{
    g_cap_what = what;
    g_cap = 1;
}
// 0: still waiting; 1: a key (code = virtual key); 2: a pad button (code = mask); 3: cancelled
int window_capture_poll(int &code)
{
    int st = g_cap;
    if (st == 3) {
        g_cap = 0;
        code = g_cap_code;
        return g_cap_pad ? 2 : 1;
    }
    if (st == 4) {
        g_cap = 0;
        return 3;
    }
    return 0;
}

static void capture_step()
{
    PadState ps = pad_read(0), ps2 = pad_read(1);
    int pressed_key = 0;
    for (int vk = 8; vk < 256; vk++) {
        // the sided Shift, Ctrl and Alt are bindable, the generic ones are the same key twice
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_SHIFT || vk == VK_CONTROL ||
            vk == VK_MENU)
            continue;
        if (key(vk)) {
            pressed_key = vk;
            break;
        }
    }
    int pressed_pad = (ps.ok ? ps.mask : 0) | (ps2.ok ? ps2.mask : 0);
    // HP2_CAPTEST=<virtual key>: the test press, a few frames after the capture began
    static const char *test = getenv("HP2_CAPTEST");
    static int waited;
    if (test && g_cap == 2 && ++waited > 30) pressed_key = atoi(test);
    if (g_cap == 1) {
        if (!pressed_key && !pressed_pad) g_cap = 2;
        return;
    }
    if (pressed_key == VK_ESCAPE) {
        g_cap = 4;
        g_cap_release = true;
        return;
    }
    if (g_cap_what == 2) pressed_key = 0;
    if (g_cap_what == 1) pressed_pad = 0;
    if (pressed_key) {
        g_cap_pad = 0;
        g_cap_code = pressed_key;
        g_cap = 3;
        g_cap_release = true;
    } else if (pressed_pad) {
        g_cap_pad = 1;
        g_cap_code = pressed_pad & -pressed_pad;      // the lowest button of those down
        g_cap = 3;
        g_cap_release = true;
    }
}

// The pointer: where a screen takes the mouse (pc_cursor_wanted), and only while the mouse is in use. Once for every frame drawn
// (native_end_frame) as well as for every read of the pad: the game does not read the pad all the time (through the logos and the opening
// film it does not), and a pointer left showing by the screen before would stay through them.
// The difference is taken as a signed number: the window's thread may stamp the mouse a millisecond after this one read the clock, and
// unsigned that is a huge time ago; the pointer went out for a frame at a time while the mouse moved.
void window_cursor_frame()
{
    if (!g_hwnd) return;
    const unsigned long long mouse_at = g_mouse_at;
    const bool wanted = pc_cursor_wanted();
    const bool cursor = wanted && mouse_at && (long long)(GetTickCount64() - mouse_at) < 3000;
    __atomic_store_n(&g_host_input.scene, pc_car_input() ? 2 : wanted ? 1 : 0, __ATOMIC_RELAXED);
    __atomic_store_n(&g_host_input.typing, g_text_on ? 1 : 0, __ATOMIC_RELAXED);
    static const bool log = getenv("HP2_NATIVELOG") != nullptr;
    if (log) {                              // the wish itself, whether or not a mouse is there
        static int was = -1, flips;
        const int now = pc_cursor_wanted() ? 1 : 0;
        if (now != was && ++flips < 200) fprintf(stderr, "pointer: the screen %s one, replay %d, car input %d\n", now ? "takes" : "takes no", (int)pc_replay_playing(), (int)pc_car_input());
        was = now;
    }
    if (g_cursor_on != cursor) {
        g_cursor_on = cursor;
        __atomic_store_n(&g_host_input.cursor_wanted, cursor ? 1 : 0, __ATOMIC_RELAXED);
        if (log) fprintf(stderr, "pointer: %s at %.1f s\n", cursor ? "wanted" : "not wanted", GetTickCount64() % 1000000 / 1000.0);
    }
}

// A zone camera's key: on a pad it is Triangle held and L2 (Zone Freeze) or R2 (Zone Zoom) pressed with it, the same in every layout.
// The key holds Triangle and, two reads later, the second button with it, as fingers would.
// For a cop the same two buttons are the calls for the helicopter and for a road block (Player::RacingJoyHandler, events 129 and 128),
// and there the key is a short press, whatever the finger does: the game changes the camera's view when Triangle is let go, and
// keeps that from following a call for one second only (Player +152, the time of the call). A key held longer than that, as a
// single key easily is, changed the view on its way up. Six reads and both buttons are up again, well inside the second.
template <class Press> static void zone_keys(bool down, int which, int &held, Press &press)
{
    if (!down) {
        held = 0;
        return;
    }
    if (pc_cop_mode() && held >= 6) return;
    press(PCB_TRIANGLE, 0xFF);
    if (++held > 2) press(which ? PCB_R2 : PCB_L2, 0xFF);
}

int native_turntest_pad();     // native.cpp

u16 window_pad(u8 sticks[4], u8 pressure[12])
{
    {   // HP2_NATIVELOG: when the game reads the pad, a line a second (through all of the start it may not)
        static const bool log = getenv("HP2_NATIVELOG") != nullptr;
        static unsigned long long last;
        static unsigned reads;
        reads++;
        if (log && GetTickCount64() / 1000 != last) {
            last = GetTickCount64() / 1000;
            fprintf(stderr, "pad: read %u at %.1f s\n", reads, GetTickCount64() % 1000000 / 1000.0);
        }
    }
    sticks[0] = sticks[1] = sticks[2] = sticks[3] = 0x80;
    memset(pressure, 0, 12);
    // A pad shakes for the player who holds it: with one player, while the gamepad is the device used last (driving on the keyboard
    // leaves a pad on the desk still); with two, each has their own.
    pad_rumble_gate(pad_slot(0), pc_two_player_race() || g_input_kind == 1);
    pad_rumble_gate(pad_slot(1), true);
    // HP2_AUTOPRESS=<start s>,<interval s>: tap cross (and start) on a timer, for
    // walking through the menus with defaults without anyone at the keyboard
    static const char *autop = getenv("HP2_AUTOPRESS");
    // HP2_HOLD=<s>: from that second on, cross is held down (gas, for testing a race)
    static const char *hold = getenv("HP2_HOLD");
    if (hold) {
        static const DWORD t0 = GetTickCount();
        if ((GetTickCount() - t0) / 1000.0 >= atof(hold)) {
            pressure[6] = 0xFF;
            return (u16)~(1u << 14);
        }
    }
    if (autop) {
        static const DWORD t0 = GetTickCount();
        double start = atof(autop), every = strchr(autop, ',') ? atof(strchr(autop, ',') + 1) : 2.0;
        double t = (GetTickCount() - t0) / 1000.0;
        if (t >= start && fmod(t - start, every) < 0.25) {
            pressure[6] = 0xFF;
            // a third number: after that second press cross only (no START, so no pause)
            const char *c2 = strchr(autop, ',') ? strchr(strchr(autop, ',') + 1, ',') : nullptr;
            bool odd = (((int)((t - start) / every)) & 1) || (c2 && t >= atof(c2 + 1));
            return odd ? (u16)~(1u << 14) : (u16)~((1u << 14) | (1u << 3));   // cross, cross+start
        }
        return 0xFFFF;
    }
    if (g_cap && getenv("HP2_CAPTEST")) {
        capture_step();
        return 0xFFFF;
    }
    key_script_step();
    if (!g_hwnd || (!g_key_script_on && !foreground())) return 0xFFFF;
    static bool esc_held;                  // from cancelling a capture: not a press yet
    if (g_cap) {
        esc_held = true;
        capture_step();
        return 0xFFFF;
    }
    if (g_text_on) {
        esc_held = true;
        // a gamepad's button says the player has moved to it: whoever types offers it its own way (the letter grid)
        {
            const PadState a = pad_read(0), b = pad_read(1);
            if ((a.ok && a.mask) || (b.ok && b.mask) || (g_key_script_on && g_script_key[0xFF])) g_input_kind = 1;
        }
        static const bool by_window = getenv("HP2_TYPEBYWINDOW") != nullptr;     // a test that posts the window's own messages
        if (g_key_script_on && !by_window) {    // the same keys from a test's script, by their edges
            static bool was[256];
            if (g_text_script_fresh) {
                for (int vk = 8; vk < 256; vk++) was[vk] = g_script_key[vk] != 0;
                g_text_script_fresh = false;
                g_text_enter_free = g_text_esc_free = true;     // the script's edges are its own
            }
            for (int vk = 8; vk < 256; vk++) {
                const bool now = g_script_key[vk] != 0;
                if (now && !was[vk])
                    if (const int ch = text_char(vk, false)) text_push(ch);
                was[vk] = now;
            }
        }
        return 0xFFFF;
    }
    if (g_cap_release) {
        bool held = false;
        for (int vk = 8; vk < 0xFE && !held; vk++) held = key(vk);     // not the test script's two "as if a gamepad" marks
        PadState a = pad_read(0), b = pad_read(1);
        if (held || (a.ok && a.mask) || (b.ok && b.mask)) return 0xFFFF;
        g_cap_release = false;
    }
    u16 buttons = 0xFFFF;
    auto press = [&](int b, int amount) {
        buttons &= ~(1u << kPadBit[b].bit);
        int p = kPadBit[b].pressure;
        if (p >= 0 && amount > pressure[p]) pressure[p] = (u8)amount;
    };
    // HP2_SPEEDLOG: F9 changes the frame rate where the menu cannot be reached, in a replay. Not saved
    static const bool frame_key = getenv("HP2_SPEEDLOG") != nullptr;
    static bool f9_held;
    if (frame_key) {
        const bool f9 = key(VK_F9);
        if (f9 && !f9_held) {
            g_cfg.frame_rate = g_cfg.frame_rate == 60 ? 30 : 60;
            g_cfg.pictures = 0;
            fprintf(stderr, "speedlog: F9, frame rate %d\n", g_cfg.frame_rate);
        }
        f9_held = f9;
    }
    // and F10 the draw distance, for comparing the same place
    static bool f10_held;
    if (frame_key) {
        const bool f10 = key(VK_F10);
        if (f10 && !f10_held) {
            g_cfg.draw_distance = g_cfg.draw_distance >= 600 ? 100 : g_cfg.draw_distance >= 400 ? 600 : g_cfg.draw_distance * 2;
            fprintf(stderr, "speedlog: F10, draw distance %d\n", g_cfg.draw_distance);
        }
        f10_held = f10;
    }
    // TEMP: HP2_F11DUMP, F11 dumps the next frame's draws and pictures (native_dump_next)
    static const bool f11_key = getenv("HP2_F11DUMP") != nullptr;
    static bool f11_held;
    if (f11_key) {
        const bool f11 = key(VK_F11);
        if (f11 && !f11_held) native_dump_next();
        f11_held = f11;
    }
    bool alt = key(VK_MENU);                // Alt+Enter is the window's
    bool driving = pc_car_input_port(0);
    window_cursor_frame();
    auto key_on = [&](int vk) {
        if (!key(vk) || (alt && vk == VK_RETURN)) return false;
        if (g_key_script_on) g_input_kind = 0;      // the real keyboard says so in wndproc
        return true;
    };
    g_keys_shared = keys_shared(driving);
    pc_keyboard_layout(0, driving && g_input_kind != 1);
    const bool shared_driving = g_keys_shared == KEYS_SHARED_DRIVING, shared_menu = g_keys_shared == KEYS_SHARED_MENU;
    auto drive_on = [&](int vk) { return !(shared_driving && is_key2(vk)) && key_on(vk); };
    auto menu_key = [&](int b) { return shared_menu ? pc_menu_key_split(0, b) : pc_menu_key(b); };
    bool enter = !shared_menu && key_on(VK_RETURN);
    if (driving) {
        // a key is an action, pressed as the button that has it in the keyboard's own layout (pc_keyboard_layout)
        for (int b = 0; b < PCB_COUNT; b++)
            if (drive_on(g_cfg.key[b][0]) || drive_on(g_cfg.key[b][1])) press(b, 0xFF);
        static int zone_held[2];
        for (int z = 0; z < 2; z++) zone_keys(drive_on(g_cfg.key_zone[z][0]) || drive_on(g_cfg.key_zone[z][1]), z, zone_held[z], press);
        if (const int hands = native_turntest_pad()) {      // TEMP HP2_TURNTEST: the port's own hands
            if (hands & 1) press(PCB_CROSS, 0xFF);
            if (hands & 2) press(PCB_LEFT, 0xFF);
        }
        // Steering is the two keys' buttons alone (the D-pad's Left and Right, JOY_EVENT_STEER): the game's own digital steering, which
        // turns the wheel at its own rate. The stick is left in the middle: pushed to its end with the key, as it used to be, it was the
        // analog steering at full lock at once.
        // Enter is no key of the car's: in the race it does nothing (it was Cross, the throttle or the brake by the gamepad's layout).
        // Where the game waits to go on, before the start and after the finish, it is the button waited for.
        if (enter && pc_skip_wanted()) press(PCB_CROSS, 0xFF);
    } else {
        for (int b = 0; b < PCB_COUNT; b++)
            if (b != PCB_TRIANGLE && b != PCB_START && key_on(menu_key(b))) press(b, 0xFF);
        if (key_on(menu_key(PCB_LEFT))) sticks[2] = 0x00;
        if (key_on(menu_key(PCB_RIGHT))) sticks[2] = 0xFF;
        if (key_on(menu_key(PCB_UP))) sticks[3] = 0x00;
        if (key_on(menu_key(PCB_DOWN))) sticks[3] = 0xFF;
    }
    if (!driving || pc_replay_playing()) {      // a replay has the car's controls and a bar of buttons for the pointer
        int mouse = pc_mouse_buttons();
        for (int b = 0; b < PCB_COUNT; b++)
            if (mouse >> b & 1) {
                press(b, 0xFF);
                if (b == PCB_LEFT) sticks[2] = 0x00;
                if (b == PCB_RIGHT) sticks[2] = 0xFF;
                if (b == PCB_UP) sticks[3] = 0x00;
                if (b == PCB_DOWN) sticks[3] = 0xFF;
            }
    }
    // Esc: back in the menus. In a race it opens the pause menu; there Start closes it, and Back
    // says No to its questions
    if (!key(VK_ESCAPE)) esc_held = false;
    if (!esc_held && key_on(VK_ESCAPE)) {
        // (the pause menu can be up with the mode of driving, opened while the zone camera was on: Start closes it there too)
        if (pc_racing() && (!pc_race_menu() || pc_pause_menu())) press(PCB_START, 0xFF);
        if (!driving) press(PCB_TRIANGLE, 0xFF);
    }
    g_pad_steering_now = driving ? g_cfg.pad_steering : 100;
    // HP2_VPADS: 0xC0, 0xC1 in the key script are A and RT on the second test pad, 0xC2, 0xC3 on the first
    if (g_key_script_on) {
        pad_virtual(1, g_script_key[0xC0], g_script_key[0xC1]);
        pad_virtual(0, g_script_key[0xC2], g_script_key[0xC3]);
    }
    PadState ps = pad_read(pad_slot(0));
    g_pad_playstation = g_cfg.pad_icons ? g_cfg.pad_icons == 2 : pad_is_playstation(pad_slot(0));
    g_layout_playstation = g_cfg.pad_icons ? g_cfg.pad_icons == 2 : !ps.ok || pad_is_playstation(pad_slot(0));
    if (ps.ok) {
        // a pad that is there when the game starts is the device, until a key says otherwise
        static bool first = true;
        if (first) g_input_kind = 1;
        first = false;
        // A new press or a stick pushed well over says the player has moved to the pad. One held says nothing: a drifting stick
        // or a pedal kept down must not hold the hints against the keyboard.
        static int pad_held;
        static bool stick_held;
        if (ps.mask & ~pad_held) g_input_kind = 1, g_mouse_at = 0;
        pad_held = ps.mask;
        for (int b = 0; b < PCB_COUNT; b++) {
            int m = (driving ? g_cfg.pad[b] : pc_menu_pad(b)) & ps.mask;
            if (!m) continue;
            // a trigger alone gives its pressure (gas, brake); the other buttons are all or nothing
            int amount = 0xFF;
            if (m == 0x20000) amount = ps.rt;
            else if (m == 0x10000) amount = ps.lt;
            press(b, amount);
        }
        bool pushed = false;
        for (int i = 0; i < 4; i++)
            if (ps.sticks[i] != 0x80) {
                sticks[i] = ps.sticks[i];
                if (ps.sticks[i] < 0x40 || ps.sticks[i] > 0xC0) pushed = true;
            }
        if (pushed && !stick_held) g_input_kind = 1, g_mouse_at = 0;
        stick_held = pushed;
    } else if (g_input_kind == 1) {
        g_input_kind = 0;       // the pad is gone
    }
    if (g_key_script_on && g_script_key[0xFF]) g_input_kind = 1;    // the script's "as if a gamepad were in use"
    if (g_key_script_on && g_script_key[0xFE]) {                     // and "as if that gamepad were a PlayStation's"
        g_input_kind = 1;
        g_pad_playstation = 1;
    }
    return buttons;
}

// Player 2: their gamepad, and the keyboard's other half when it is shared
u16 window_pad2(u8 sticks[4], u8 pressure[12])
{
    sticks[0] = sticks[1] = sticks[2] = sticks[3] = 0x80;
    memset(pressure, 0, 12);
    if (!g_hwnd || g_cap || (!g_key_script_on && !foreground())) return 0xFFFF;
    u16 buttons = 0xFFFF;
    auto press = [&](int b, int amount) {
        buttons &= ~(1u << kPadBit[b].bit);
        int p = kPadBit[b].pressure;
        if (p >= 0 && amount > pressure[p]) pressure[p] = (u8)amount;
    };
    const bool driving = pc_car_input_port(1);
    pc_keyboard_layout(1, driving && g_keys_shared == KEYS_SHARED_DRIVING);
    if (g_keys_shared == KEYS_SHARED_DRIVING) {
        for (int b = 0; b < PCB_COUNT; b++)
            if (key(g_cfg.key2[b][0]) || key(g_cfg.key2[b][1])) press(b, 0xFF);
        static int zone_held[2];
        for (int z = 0; z < 2; z++) zone_keys(key(g_cfg.key2_zone[z][0]) || key(g_cfg.key2_zone[z][1]), z, zone_held[z], press);
        if (key(VK_RETURN) && !key(VK_MENU) && pc_skip_wanted()) press(PCB_CROSS, 0xFF);    // as player 1 has it
    } else if (g_keys_shared == KEYS_SHARED_MENU) {
        for (int b = 0; b < PCB_COUNT; b++)
            if (key(pc_menu_key_split(1, b))) press(b, 0xFF);
        if (key(pc_menu_key_split(1, PCB_LEFT))) sticks[2] = 0x00;
        if (key(pc_menu_key_split(1, PCB_RIGHT))) sticks[2] = 0xFF;
        if (key(pc_menu_key_split(1, PCB_UP))) sticks[3] = 0x00;
        if (key(pc_menu_key_split(1, PCB_DOWN))) sticks[3] = 0xFF;
    }
    PadState ps = pad_read(pad_slot(1));
    if (ps.ok) {
        for (int b = 0; b < PCB_COUNT; b++) {
            int m = (driving ? g_cfg.pad[b] : pc_menu_pad(b)) & ps.mask;
            if (!m) continue;
            int amount = 0xFF;
            if (m == 0x20000) amount = ps.rt;
            else if (m == 0x10000) amount = ps.lt;
            press(b, amount);
        }
        for (int i = 0; i < 4; i++)
            if (ps.sticks[i] != 0x80) sticks[i] = ps.sticks[i];
    }
    return buttons;
}

// The page has its own pixels (devicePixelRatio): nothing to declare
int window_dpi_aware() { return 2; }
void window_dpi_thread() {}

// "The window": the input block made known to the page, and the game told there is one (window_pad reads the keyboard only then)
void window_start()
{
    static int dummy;
    if (getenv("HP2_CURSORSTART")) g_cursor_on = true, mouse_used();    // for tests: as if the mouse had just been used on a screen before
    host_input_publish();
    g_hwnd = &dummy;
}
