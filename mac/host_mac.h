// SpeedBreaker HP2. GPL-3.0-or-later.
// What the macOS host (host_sdl.cpp) gives the runtime's renderer (native_vk.cpp): safe to call from any thread.
#pragma once

void *host_metal_layer();               // the window's CAMetalLayer (for vkCreateMetalSurfaceEXT); null before the window exists
void host_view_pixels(int &w, int &h);  // the window's drawable size in pixels (0, 0 while minimized)
int host_refresh_hz();                  // the window's screen, frames a second (0: unknown)
