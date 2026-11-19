// The gamepads outside Windows: what the page's Gamepad API saw, from the host's input block (host_input.h), made into pad.h's PadState
// the same way pad.cpp makes SDL's (its dead zones, the steering's sensitivity, the triggers' threshold). The motors are the page's: the
// game's wish is left in the block.
#include "pad.h"

#include "host_input.h"
#include "pcconfig.h"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace {

std::atomic<bool> g_rumble_quiet;
std::atomic<bool> g_rumble_off[2];

// an SDL axis (-32768..32767) as a DualShock axis, with a dead zone (pad.cpp's)
uint8_t axis(int v, int dead, int sensitivity = 100)
{
    if (v > -dead && v < dead) return 0x80;
    int s = v < 0 ? -1 : 1, m = (v < 0 ? -v : v) - dead;
    float t = (float)m / (float)(32768 - dead);
    if (sensitivity != 100) t = powf(t, 100.0f / (float)sensitivity);
    int out = 0x80 + s * (int)(t * 128);
    return (uint8_t)(out < 0 ? 0 : out > 255 ? 255 : out);
}

// field by field: the page writes each as a whole word (a struct this size has no lock-free load, and wasm no libatomic)
HostPad seen(int slot)
{
    const HostPad &in = host_input().pads[slot & 1];
    HostPad p;
    const int32_t *from = (const int32_t *)&in;
    int32_t *to = (int32_t *)&p;
    for (size_t i = 0; i < sizeof(HostPad) / 4; i++) to[i] = __atomic_load_n(from + i, __ATOMIC_ACQUIRE);
    return p;
}

}   // namespace

void pad_virtual(int, bool, bool) {}

int pad_count()
{
    return (seen(0).ok ? 1 : 0) + (seen(1).ok ? 1 : 0);
}

PadState pad_read(int slot)
{
    PadState ps;
    const HostPad r = seen(slot);
    if (!r.ok) return ps;
    ps.ok = true;
    ps.mask = r.mask & 0xffff;
    ps.lt = (uint8_t)((r.lt < 0 ? 0 : r.lt > 32767 ? 32767 : r.lt) * 255 / 32767);
    ps.rt = (uint8_t)((r.rt < 0 ? 0 : r.rt > 32767 ? 32767 : r.rt) * 255 / 32767);
    if (ps.lt > 30) ps.mask |= 0x10000;
    if (ps.rt > 30) ps.mask |= 0x20000;
    const int kRightDead = 8689;
    int dead = g_cfg.pad_deadzone * 32768 / 100;
    ps.sticks[0] = axis(r.axis[0], kRightDead);
    ps.sticks[1] = axis(r.axis[1], kRightDead);
    ps.sticks[2] = axis(r.axis[2], dead, g_pad_steering_now);
    ps.sticks[3] = axis(r.axis[3], dead);
    return ps;
}

bool pad_is_playstation(int slot) { return seen(slot).playstation != 0; }

void pad_rumble_quiet(bool quiet) { g_rumble_quiet = quiet; }
void pad_rumble_gate(int slot, bool on) { g_rumble_off[slot & 1] = !on; }

void pad_rumble(int slot, int small_on, int large)
{
    HostPad &p = host_input().pads[slot & 1];
    const bool off = g_rumble_quiet || g_rumble_off[slot & 1];
    __atomic_store_n(&p.rumble_small, off ? 0 : (small_on ? 1 : 0), __ATOMIC_RELAXED);
    __atomic_store_n(&p.rumble_large, off ? 0 : (large & 255), __ATOMIC_RELAXED);
}

void pad_settle(int) {}
