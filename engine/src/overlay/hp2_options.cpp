// hp2_options.cpp -- NFS Hot Pursuit 2's rows in the overlay menu (overlay_options.h). With the PS2's own menus (pc_ps2_menus,
// pcmenu.cpp) the port's settings have no page in the game any more: they are all here. Every row is the game's own (key NULL):
// get and set read and write g_cfg, and set saves hp2.ini (pc_config_save). The value sets, their labels and their order are the
// ones nfshp2-port-win's pages had (recomp/runtime/pcsettings.cpp), so a value means here what it meant there.
//
// Threads: get and set run on the render thread (the menu's). They only store ints, which the renderer reads where it makes the
// frame (settings_key) and the game where it needs them. What calls into the game or writes its memory (the language, the cheats)
// is handed to the game's thread (pc_game_job), which runs it at the frontend's next frame.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "hw.h"
#include "pcconfig.h"
#include "pclang.h"
#include "pcmenu.h"
#include "overlay_options.h"
#include "host_mac.h"

namespace
{
// ---- choice lists ------------------------------------------------------------------------------
// A list of labels that lives as long as the program: the table points into it.
struct Labels
{
    std::vector<std::string> text;
    std::vector<const char *> ptr;
    void add(const std::string &s) { text.push_back(s); }
    const char *const *done()
    {
        ptr.clear();
        for (const std::string &s : text) ptr.push_back(s.c_str());
        return ptr.data();
    }
};
// (a deque would do as well: Labels are made once, in the table's order, and never move after done())
std::vector<Labels *> g_labels;

Labels &new_labels()
{
    g_labels.push_back(new Labels);
    return *g_labels.back();
}

// lo..hi by step, each shown by fmt
const char *const *range_labels(int lo, int hi, int step, std::string (*fmt)(int))
{
    Labels &l = new_labels();
    for (int v = lo; v <= hi; v += step) l.add(fmt(v));
    return l.done();
}

int step_index(int v, int lo, int hi, int step) { return (std::min(std::max(v, lo), hi) - lo + step / 2) / step; }

const char *const k_onoff[] = {"Off", "On"};

void save() { pc_config_save(); }

int gpu_limit(int what)     // the most the graphics card has: 0 multisampling, 1 anisotropy (pcsettings.cpp's)
{
    int aa = 8, aniso = 16;
    native_vk_limits(aa, aniso);
    return what ? aniso : aa;
}

// ---- Display -----------------------------------------------------------------------------------
const char *const k_window[] = {"Windowed", "Fullscreen"};
int get_window() { return g_cfg.fullscreen ? 1 : 0; }
void set_window(int c) { window_set_fullscreen(c != 0); }     // the host follows g_cfg.fullscreen

// Aspect Ratio (g_cfg.aspect, pc_aspect): the race's picture. A shape other than the screen's gets black bars; the menus stay 4:3.
const char *const k_aspect[] = {"4:3 (PS2)", "Screen", "16:10", "16:9", "21:9"};
int get_aspect() { return std::min(std::max(g_cfg.aspect, 0), 4); }

// Resolution (fs_w, fs_h): in fullscreen, the size the picture is drawn at before it is scaled to the screen. The renderer takes the
// smaller of it and the screen (native_vk.cpp, lines_now), so only sizes that fit the screen are offered, and only those of the
// chosen shape. 0, 0 is the screen's own size: the sharpest without Render Scale, which is what draws above the screen's size.
struct Size { int w, h; };
const Size k43[] = {{800, 600}, {960, 720}, {1024, 768}, {1280, 960}, {1440, 1080}, {1600, 1200}, {1920, 1440}, {2048, 1536}, {2880, 2160}};
const Size k1610[] = {{1280, 800}, {1440, 900}, {1680, 1050}, {1920, 1200}, {2304, 1440}, {2560, 1600}, {2880, 1800}, {3024, 1890}, {3456, 2160}, {3840, 2400}};
const Size k169[] = {{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}, {5120, 2880}};
const Size k219[] = {{1720, 720}, {2560, 1080}, {3440, 1440}, {3840, 1600}, {5120, 2160}};

struct ResList
{
    std::vector<Size> sizes;            // the last is 0, 0: the screen
    std::vector<std::string> text;
    std::vector<const char *> ptr;
};
ResList g_res;
int g_res_row = -1;                     // its row in g_table (the choices change with the shape and the screen)
std::vector<OverlayOption> g_table;

void display_size(int &w, int &h)
{
    host_display_pixels(w, h);
    if (w <= 0 || h <= 0) host_view_pixels(w, h);
}

// The sizes for the shape chosen now, smallest first, then the screen's own. Render thread (the menu's), as get and set.
void res_refresh()
{
    int dw = 0, dh = 0;
    display_size(dw, dh);
    const Size *list = nullptr;
    int n = 0;
    switch (get_aspect()) {
    case 0: list = k43, n = (int)(sizeof k43 / sizeof k43[0]); break;
    case 2: list = k1610, n = (int)(sizeof k1610 / sizeof k1610[0]); break;
    case 3: list = k169, n = (int)(sizeof k169 / sizeof k169[0]); break;
    case 4: list = k219, n = (int)(sizeof k219 / sizeof k219[0]); break;
    default: break;                     // Screen: its own size only
    }
    g_res.sizes.clear();
    g_res.text.clear();
    for (int i = 0; i < n; i++)
        if (list[i].w <= dw && list[i].h <= dh && !(list[i].w == dw && list[i].h == dh)) {
            g_res.sizes.push_back(list[i]);
            g_res.text.push_back(std::to_string(list[i].w) + "x" + std::to_string(list[i].h));
        }
    g_res.sizes.push_back({0, 0});
    g_res.text.push_back(dw > 0 ? "Screen (" + std::to_string(dw) + "x" + std::to_string(dh) + ")" : std::string("Screen"));
    g_res.ptr.clear();
    for (const std::string &s : g_res.text) g_res.ptr.push_back(s.c_str());
    if (g_res_row >= 0) {
        g_table[(size_t)g_res_row].choices = g_res.ptr.data();
        g_table[(size_t)g_res_row].n = (int)g_res.ptr.size();
        g_table[(size_t)g_res_row].def = (int)g_res.ptr.size() - 1;
    }
}
int get_res()
{
    res_refresh();
    for (size_t i = 0; i + 1 < g_res.sizes.size(); i++)
        if (g_res.sizes[i].w == g_cfg.fs_w && g_res.sizes[i].h == g_cfg.fs_h) return (int)i;
    return (int)g_res.sizes.size() - 1;     // the screen's own, or a size of another shape from before
}
void set_res(int c)
{
    if (c < 0 || c >= (int)g_res.sizes.size()) return;
    g_cfg.fs_w = g_res.sizes[(size_t)c].w;
    g_cfg.fs_h = g_res.sizes[(size_t)c].h;
    save();
}

void set_aspect(int c)
{
    g_cfg.aspect = c;
    // a size of the old shape would draw the new one squeezed into it: back to the screen's own, the new list's default
    g_cfg.fs_w = g_cfg.fs_h = 0;
    save();
    res_refresh();
}

// Render Scale, and how high the picture is drawn with it now (the renderer's lines_now: the screen or the resolution, the smaller,
// as a 4:3 box's height, times the scale)
int g_scale_row = -1;
std::string g_scale_where;
void scale_where_refresh()
{
    int w = 0, h = 0;
    host_view_pixels(w, h);
    if (w <= 0 || h <= 0 || g_scale_row < 0) return;
    const int screen_h = h;
    if (g_cfg.fullscreen && g_cfg.fs_w > 0 && g_cfg.fs_h > 0) w = std::min(w, g_cfg.fs_w), h = std::min(h, g_cfg.fs_h);
    const int lines = std::clamp((std::min(h, w * 3 / 4) * std::clamp(g_cfg.render_scale, 20, 200) + 50) / 100, 224, 4320);
    g_scale_where = "Everything the game draws: at once. Drawn now " + std::to_string(lines) + " pixels high, shown " +
                    std::to_string(screen_h) + (g_cfg.fullscreen ? " high on the screen" : " high in the window") + (lines > screen_h ? ": drawn larger, then scaled down." : ".");
    g_table[(size_t)g_scale_row].where = g_scale_where.c_str();
}
int get_scale()
{
    scale_where_refresh();
    return step_index(g_cfg.render_scale, 20, 200, 20);
}
void set_scale(int c) { g_cfg.render_scale = 20 + c * 20; save(); }

const char *const k_onoff_vsync[] = {"Off", "On"};
int get_vsync() { return g_cfg.vsync ? 1 : 0; }
void set_vsync(int c) { g_cfg.vsync = c; save(); }

// 1000 is Unlimited: as many pictures as the card makes. Above 60 the world still runs at 60, with pictures drawn in between.
const int kRates[] = {30, 60, 120, 144, 180, 240, 300, 360, 500, 1000};
const char *const k_rates[] = {"30 FPS", "60 FPS", "120 FPS", "144 FPS", "180 FPS", "240 FPS", "300 FPS", "360 FPS", "500 FPS", "Unlimited"};
int get_rate()
{
    int at = 0;
    for (int i = 0; i < 10; i++)
        if (kRates[i] <= pc_picture_rate()) at = i;
    return at;
}
void set_rate(int c)
{
    g_cfg.frame_rate = kRates[c] >= 60 ? 60 : 30;
    g_cfg.pictures = kRates[c] > 60 ? kRates[c] : 0;
    save();
}

int get_fps() { return g_cfg.show_fps ? 1 : 0; }
void set_fps(int c) { g_cfg.show_fps = c; save(); }

// ---- Graphics ----------------------------------------------------------------------------------
const int kDraw[] = {100, 200, 400, 600};
const char *const k_draw[] = {"Original", "Advanced", "High", "Maximum"};
int get_draw()
{
    int at = 0;
    for (int i = 0; i < 4; i++)
        if (kDraw[i] <= g_cfg.draw_distance) at = i;
    return at;
}
void set_draw(int c) { g_cfg.draw_distance = kDraw[c]; save(); }

const char *const k_lod[] = {"Original", "Maximum"};
int get_lod() { return g_cfg.max_detail ? 1 : 0; }
void set_lod(int c) { g_cfg.max_detail = c; save(); }

const char *const k_refl[] = {"PS2", "Advanced", "High"};
int get_refl() { return g_cfg.reflections; }
void set_refl(int c) { g_cfg.reflections = c; save(); }

const char *const k_shadows[] = {"PS2", "High", "Maximum", "Ultra"};
int get_shadows() { return g_cfg.shadows; }
void set_shadows(int c) { g_cfg.shadows = c; save(); }

const char *const k_particles[] = {"Original", "Advanced"};
int get_particles() { return g_cfg.particles ? 1 : 0; }
void set_particles(int c) { g_cfg.particles = c; save(); }

// Antialiasing's two rows (the way, its quality) as one: Off, MSAA 2x/4x, SMAA 2x/4x
const char *const k_aa[] = {"Off", "MSAA 2x", "MSAA 4x", "SMAA 2x", "SMAA 4x"};
int get_aa()
{
    if (g_cfg.aa_mode == 1) return g_cfg.aa_level == 2 ? 1 : 2;
    if (g_cfg.aa_mode == 2) return g_cfg.aa_level == 2 ? 3 : 4;
    return 0;
}
void set_aa(int c)
{
    const int mode = c == 0 ? 0 : c <= 2 ? 1 : 2, level = c == 1 || c == 3 ? 2 : 4;
    if (mode == 1 && gpu_limit(0) < level) {    // what the card has, no more (it has 2 or none)
        fprintf(stderr, "[overlay] this graphics card has no MSAA %dx\n", level);
        return;
    }
    g_cfg.aa_mode = mode;
    if (mode) g_cfg.aa_level = level;
    save();
}

const char *const k_aniso[] = {"Off", "2x", "4x", "8x", "16x"};
int get_aniso()
{
    const int v = std::min(g_cfg.aniso, gpu_limit(1));
    return v >= 16 ? 4 : v >= 8 ? 3 : v >= 4 ? 2 : v >= 2 ? 1 : 0;
}
void set_aniso(int c)
{
    const int want = c ? 1 << c : 0;
    g_cfg.aniso = std::min(want, gpu_limit(1));
    save();
}

int get_ao() { return g_cfg.ao ? 1 : 0; }
void set_ao(int c) { g_cfg.ao = c; save(); }
int get_deband() { return g_cfg.deband ? 1 : 0; }
void set_deband(int c) { g_cfg.deband = c; save(); }

std::string fmt_signed(int v) { return (v > 0 ? "+" : "") + std::to_string(v); }
std::string fmt_pct(int v) { return std::to_string(v) + "%"; }
std::string fmt_gamma(int v)
{
    char s[16];
    snprintf(s, sizeof s, "%d.%02d", v / 100, v % 100);
    return s;
}
int get_bright() { return step_index(g_cfg.brightness, -50, 50, 5); }
void set_bright(int c) { g_cfg.brightness = -50 + 5 * c; save(); }
int get_contrast() { return step_index(g_cfg.contrast, 50, 150, 5); }
void set_contrast(int c) { g_cfg.contrast = 50 + 5 * c; save(); }
int get_gamma() { return step_index(g_cfg.gamma, 50, 200, 5); }
void set_gamma(int c) { g_cfg.gamma = 50 + 5 * c; save(); }
void picture_defaults()
{
    g_cfg.brightness = 0;
    g_cfg.contrast = g_cfg.gamma = 100;
    save();
}

// ---- Audio -------------------------------------------------------------------------------------
int get_volume() { return step_index(g_cfg.volume, 0, 100, 10); }
void set_volume(int c) { g_cfg.volume = c * 10; save(); }

// ---- Controls ----------------------------------------------------------------------------------
int get_dead() { return step_index(g_cfg.pad_deadzone, 0, 50, 2); }
void set_dead(int c) { g_cfg.pad_deadzone = c * 2; save(); }
int get_steer() { return step_index(g_cfg.pad_steering, 50, 200, 10); }
void set_steer(int c) { g_cfg.pad_steering = 50 + c * 10; save(); }
const char *const k_icons[] = {"Auto", "Xbox", "PlayStation"};
int get_icons() { return g_cfg.pad_icons; }
void set_icons(int c) { g_cfg.pad_icons = c; save(); }
const char *const k_p2[] = {"Second gamepad", "First gamepad"};
int get_p2() { return g_cfg.player2 ? 1 : 0; }
void set_p2(int c) { g_cfg.player2 = c; save(); }
void pad_defaults() { g_cfg.default_pad(); save(); }
void key_defaults() { g_cfg.default_keys(); save(); }

// Player 1's driving keys: a fixed list of the keys the Mac's keyboard gives (host_sdl.cpp's vk_of), by pc_key_name's names.
// Esc pauses, F1 is this menu, Cmd is the Mac's: not among them.
std::vector<int> g_key_vk;
const char *const *key_labels()
{
    Labels &l = new_labels();
    g_key_vk.push_back(0);
    l.add("None");
    auto add = [&l](int vk) {
        g_key_vk.push_back(vk);
        l.add(pc_key_name(vk));
    };
    for (int c = 'A'; c <= 'Z'; c++) add(c);
    for (int c = '0'; c <= '9'; c++) add(c);
    for (int vk : {0x26, 0x28, 0x25, 0x27, 0x20, 0x0D, 0x09, 0x08, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0x14, 0x21, 0x22, 0x24, 0x23,
                   0x2D, 0x2E})
        add(vk);
    for (int vk = 0x60; vk <= 0x69; vk++) add(vk);
    for (int vk : {0x6A, 0x6B, 0x6D, 0x6E, 0x6F, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xDB, 0xDC, 0xDD, 0xDE}) add(vk);
    for (int vk = 0x71; vk <= 0x7B; vk++) add(vk);      // F2..F12
    return l.done();
}

// a row of the keyboard: a PS2 button's key, or one of the two zone cameras' (pcsettings.cpp's key_row)
int *key_row(PcConfig &cfg, bool p2, int row)
{
    if (row < PCB_COUNT) return (p2 ? cfg.key2 : cfg.key)[row];
    return (p2 ? cfg.key2_zone : cfg.key_zone)[row - PCB_COUNT];
}
const int kKeyRows = PCB_COUNT + 2;

int key_choice(int vk)
{
    for (size_t i = 0; i < g_key_vk.size(); i++)
        if (g_key_vk[i] == vk) return (int)i;
    return 0;
}
bool key_taken(int vk, int row)     // by another action, of either player
{
    for (int who = 0; who < 2; who++)
        for (int r = 0; r < kKeyRows; r++)
            for (int k = 0; k < 2; k++)
                if ((who || r != row) && key_row(g_cfg, who != 0, r)[k] == vk) return true;
    return false;
}
// An action has one key, and a key one action (as binding a key did): stepping through the list passes over the keys that
// another action has, so nothing is taken from it by the way.
template <int Row> int get_key() { return key_choice(key_row(g_cfg, false, Row)[0]); }
template <int Row> void set_key(int c)
{
    const int now = get_key<Row>(), d = c < now ? -1 : 1;
    for (int i = c; i >= 0 && i < (int)g_key_vk.size(); i += d)
        if (!g_key_vk[(size_t)i] || !key_taken(g_key_vk[(size_t)i], Row)) {
            int *mine = key_row(g_cfg, false, Row);
            mine[0] = g_key_vk[(size_t)i];
            mine[1] = 0;
            save();
            return;
        }
}

// ---- Game --------------------------------------------------------------------------------------
// The languages are the game's and data/lang's: read when the menu first shows the row (on its own thread, well after the game
// has read them with hp2.ini's language), so the row's list is filled in then.
// the game's text is Latin-1 (pclang.h), the menu's font takes UTF-8
std::string utf8_of_latin1(const std::string &s)
{
    std::string out;
    for (unsigned char c : s)
        if (c < 0x80) out += (char)c;
        else out += (char)(0xC0 | c >> 6), out += (char)(0x80 | (c & 0x3F));
    return out;
}

OverlayOption *g_lang_row;
Labels *g_lang_labels;
const char *const k_lang_none[] = {"-"};
int get_lang()
{
    if (g_lang_row && !g_lang_labels) {
        g_lang_labels = &new_labels();
        for (const PcLanguage &l : pc_languages()) g_lang_labels->add(utf8_of_latin1(l.name));
        if (!g_lang_labels->text.empty()) {
            g_lang_row->choices = g_lang_labels->done();
            g_lang_row->n = (int)g_lang_labels->text.size();
        }
    }
    return pc_language_index();
}
void set_lang(int c)
{
    // the game's own calls: on its thread, in the frontend (a race has its texts loaded)
    pc_game_job([c] {
        if (pc_racing()) {
            fprintf(stderr, "[overlay] the language changes in the menus, not in a race\n");
            return;
        }
        if (c == pc_language_index()) return;
        pc_language_choose(c);
        pc_retranslate();
    });
}

int get_debug() { return g_cfg.debug_menu ? 1 : 0; }
void set_debug(int c) { g_cfg.debug_menu = c; save(); }

template <int Section> int get_cheats() { return pc_cheats_on(Section) ? 1 : 0; }
template <int Section> void set_cheats(int c)
{
    pc_game_job([c] { pc_cheats_set(Section, c != 0); });     // the game's table of codes: its memory, its thread
}

void open_logs() { overlay_request(OVERLAY_REQ_OPEN_LOGS); }

// ---- the table -------------------------------------------------------------------------------

void row(const char *page, const char *label, const char *help, const char *where, const char *const *choices, int n, int def,
         int (*get)(void), void (*set)(int))
{
    OverlayOption o = {};
    o.page = page, o.label = label, o.help = help, o.where = where, o.choices = choices, o.n = n, o.def = def;
    o.get = get, o.set = set;
    g_table.push_back(o);
}
void action(const char *page, const char *label, const char *help, const char *where, void (*fn)(void), const char *value)
{
    OverlayOption o = {};
    o.page = page, o.label = label, o.help = help, o.where = where, o.action = fn, o.action_label = value;
    g_table.push_back(o);
}
#define CHOICES(a) a, (int)(sizeof a / sizeof a[0])

template <int Row> void key_row_option(const char *const *keys, int n, const char *label, const char *help)
{
    static PcConfig fresh;                  // the defaults
    row("Controls", label, help, "Driving with the keyboard: at once.", keys, n, key_choice(key_row(fresh, false, Row)[0]),
        get_key<Row>, set_key<Row>);
}

void build()
{
    const char *const *pct20 = range_labels(20, 200, 20, fmt_pct);

    row("Display", "Window", "A window, or borderless fullscreen. Cmd+Enter switches it too.", "The window: at once.",
        CHOICES(k_window), 0, get_window, set_window);
    row("Display", "Aspect Ratio", "The race's picture. 4:3: as on the PS2. Screen: your screen's shape. 16:10, 16:9, 21:9: that shape, "
        "with black bars where your screen's differs. The menus stay 4:3.", "Races: at once.", CHOICES(k_aspect), 0, get_aspect, set_aspect);
    g_res_row = (int)g_table.size();
    row("Display", "Resolution", "Fullscreen: the size the picture is drawn at, then scaled to fill the screen. Only sizes of the "
        "chosen shape that fit your screen; Screen is the sharpest. For more, raise Render Scale. A window uses its own size.",
        "Fullscreen: at once.", nullptr, 1, 0, get_res, set_res);
    res_refresh();
    g_scale_row = (int)g_table.size();
    row("Display", "Render Scale", "How finely the picture is drawn. 100%: at the resolution. Above: drawn larger than the screen, then "
        "scaled down (sharper, costs the GPU). Below: faster, softer.", "Everything the game draws: at once.", pct20, 10, 4, get_scale,
        set_scale);
    row("Display", "VSync", "On: each frame waits for the screen, no tearing. Off: shown at once.", "The whole picture: at once.",
        CHOICES(k_onoff_vsync), 1, get_vsync, set_vsync);
    row("Display", "Frame Rate", "The PS2's was 30. Above 60 the game still runs at 60, with pictures drawn in between.",
        "Races: at once. The menus run at 60.", CHOICES(k_rates), 0, get_rate, set_rate);
    row("Display", "Show FPS", "Frames per second in the corner of the picture.", "Top left of the picture: at once.",
        CHOICES(k_onoff), 0, get_fps, set_fps);

    row("Graphics", "Draw Distance", "How far away scenery still shows. Original: as on the PS2. Advanced: twice as far. High: four "
        "times. Maximum: six times.", "Races: at once.", CHOICES(k_draw), 1, get_draw, set_draw);
    row("Graphics", "Level of Detail", "Maximum: scenery and cars always at their most detailed models. Original: as on the PS2.",
        "Races: at once.", CHOICES(k_lod), 1, get_lod, set_lod);
    row("Graphics", "Reflections", "The reflections on the cars. Advanced: new every frame. High: new every frame, at twice the "
        "resolution.", "Car paint in races: at once.", CHOICES(k_refl), 0, get_refl, set_refl);
    row("Graphics", "Shadows", "The cars' shadows. High: the body's own outline, lying on whatever is under it. Maximum and Ultra: "
        "finer still.", "Races: at once.", CHOICES(k_shadows), 0, get_shadows, set_shadows);
    row("Graphics", "Particles", "Smoke, sparks and dust. Advanced: the same, with soft edges.", "Races: at once.",
        CHOICES(k_particles), 0, get_particles, set_particles);
    row("Graphics", "Anti-aliasing", "Smoother edges. MSAA draws the scene in samples; SMAA smooths the finished picture's edges.",
        "Races: at once.", CHOICES(k_aa), 0, get_aa, set_aa);
    row("Graphics", "Anisotropic Filtering", "Sharper textures far away, on everything in the world.", "Races: at once.",
        CHOICES(k_aniso), 0, get_aniso, set_aniso);
    row("Graphics", "Ambient Occlusion", "Soft shading where surfaces meet: under cars, by walls and kerbs.", "Races: at once.",
        CHOICES(k_onoff), 0, get_ao, set_ao);
    row("Graphics", "Dithering", "Smooth gradients without colour steps.", "The whole picture, menus too: at once.",
        CHOICES(k_onoff), 0, get_deband, set_deband);
    row("Graphics", "Brightness", "Lighter or darker, all over.", "The whole picture: at once.",
        range_labels(-50, 50, 5, fmt_signed), 21, 10, get_bright, set_bright);
    row("Graphics", "Contrast", "The difference between light and dark.", "The whole picture: at once.",
        range_labels(50, 150, 5, fmt_pct), 21, 10, get_contrast, set_contrast);
    row("Graphics", "Gamma", "Above 1.00 the dark parts get lighter.", "The whole picture: at once.",
        range_labels(50, 200, 5, fmt_gamma), 31, 10, get_gamma, set_gamma);
    action("Graphics", "Picture Defaults", "Brightness, contrast and gamma as they were.", nullptr, picture_defaults, "Reset");

    row("Audio", "Master Volume", "All of the game's sound, before the Mac's volume.", "Music, speech, engines, effects: at once.",
        range_labels(0, 100, 10, fmt_pct), 11, 10, get_volume, set_volume);

    row("Controls", "Stick Dead Zone", "How far the left stick moves before the car steers.", "Driving with a gamepad: at once.",
        range_labels(0, 50, 2, fmt_pct), 26, 12, get_dead, set_dead);
    row("Controls", "Steering", "Above 100%, the car turns sharper for a small move of the stick.", "Driving with a gamepad: at once.",
        range_labels(50, 200, 10, fmt_pct), 16, 5, get_steer, set_steer);
    row("Controls", "Button Icons", "Which buttons the game shows, and the menus' layout that goes with them.",
        "The game's menus and hints: at once.", CHOICES(k_icons), 0, get_icons, set_icons);
    row("Controls", "Player 2", "Player 2's controls: the second gamepad (or half the keyboard), or the first gamepad.",
        "Two-player races.", CHOICES(k_p2), 0, get_p2, set_p2);
    action("Controls", "Gamepad Defaults", "The gamepad's buttons, dead zone and steering as they were.", nullptr, pad_defaults,
           "Reset");
    {
        const char *const *keys = key_labels();
        const int n = (int)g_key_vk.size();
        const char *help = "Player 1's key for it. Keys another action has are passed over.";
        // the rows of the keyboard's page, in its order (pcsettings.cpp's kDriving; Start is Esc, always)
        key_row_option<PCB_CROSS>(keys, n, pc_button_action(PCB_CROSS), help);
        key_row_option<PCB_SQUARE>(keys, n, pc_button_action(PCB_SQUARE), help);
        key_row_option<PCB_LEFT>(keys, n, pc_button_action(PCB_LEFT), help);
        key_row_option<PCB_RIGHT>(keys, n, pc_button_action(PCB_RIGHT), help);
        key_row_option<PCB_R1>(keys, n, pc_button_action(PCB_R1), help);
        key_row_option<PCB_R3>(keys, n, pc_button_action(PCB_R3), "The cop's turbo: a few seconds faster.");
        key_row_option<PCB_L1>(keys, n, pc_button_action(PCB_L1), help);
        key_row_option<PCB_TRIANGLE>(keys, n, pc_button_action(PCB_TRIANGLE), help);
        key_row_option<PCB_CIRCLE>(keys, n, pc_button_action(PCB_CIRCLE), "Horn. Police car: siren. Cop: next suspect.");
        key_row_option<PCB_R2>(keys, n, pc_button_action(PCB_R2), help);
        key_row_option<PCB_L2>(keys, n, pc_button_action(PCB_L2), help);
        key_row_option<PCB_SELECT>(keys, n, pc_button_action(PCB_SELECT), help);
        key_row_option<PCB_UP>(keys, n, pc_button_action(PCB_UP), help);
        key_row_option<PCB_COUNT + 0>(keys, n, "Zone Freeze / Helicopter", "Racer: Zone Freeze. Cop: call the helicopter.");
        key_row_option<PCB_COUNT + 1>(keys, n, "Zone Zoom / Road Block", "Racer: Zone Zoom. Cop: ask for a road block.");
    }
    action("Controls", "Keyboard Defaults", "Player 1's keys as they were.", nullptr, key_defaults, "Reset");

    row("Game", "Language", "The game's texts and speech. Changes in the menus, not during a race.", "The menus: at once.",
        CHOICES(k_lang_none), 0, get_lang, set_lang);
    const size_t lang_at = g_table.size() - 1;
    row("Game", "Debug Menu", "The developers' Debug item, in the pause menu.", "The pause menu, in the next race.",
        CHOICES(k_onoff), 0, get_debug, set_debug);
    row("Game", "Cheats: Effects", "The game's easter eggs, all on or off, as if their codes had been entered.",
        "Races, from the menus on.", CHOICES(k_onoff), 0, get_cheats<0>, set_cheats<0>);
    row("Game", "Cheats: Cars", "The cars a code opens, all on or off.", "The car lists, from the menus on.", CHOICES(k_onoff), 0,
        get_cheats<1>, set_cheats<1>);
    row("Game", "Cheats: Cop Cars", "The cop cars for You're The Cop, all on or off.", "The car lists, from the menus on.",
        CHOICES(k_onoff), 0, get_cheats<2>, set_cheats<2>);
    row("Game", "Cheats: Tracks", "The tracks a code opens, all on or off.", "The track lists, from the menus on.",
        CHOICES(k_onoff), 0, get_cheats<3>, set_cheats<3>);
    action("Game", "Logs", "The game's logs, in Finder (for a bug report).", "Opens a Finder window.", open_logs, "Open");
    g_lang_row = &g_table[lang_at];         // (the table is complete: it does not move again)
}
}   // namespace

extern "C" const OverlayOption *overlay_game_options(int *count)
{
    if (g_table.empty()) build();
    *count = (int)g_table.size();
    return g_table.data();
}

extern "C" const char *overlay_game_title(void)
{
    return "NFS Hot Pursuit 2";
}

extern "C" const char *const *overlay_game_about(void)
{
    static const char *const lines[] = {
        "NFS Hot Pursuit 2 for Mac, built " __DATE__ ", macOS arm64, Vulkan on MoltenVK",
        "Free software under the GPL-3.0-or-later, with no warranty. Built on nfshp2-port-win (ZloiKILLER)",
        "and nfshp2-web. The settings are kept in hp2.ini, in the player's folder.",
        "Not affiliated with Electronic Arts or Sony. Your own disc, built on your Mac.",
        NULL,
    };
    return lines;
}
