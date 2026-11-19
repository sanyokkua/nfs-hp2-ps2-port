// What the overlay's portable part (overlay_core.cpp) gives its renderer's part (overlay_vk.cpp here; overlay.mm's Metal
// block in NFSU2). Render thread only.
#pragma once
#include <string>

// The ImGui context, the font and the style, once (before the renderer's backend is made).
void OverlayImGuiCreate();

// One frame of the menu at a picture of W x H pixels: the queued input, the layout, ImGui::NewFrame (after new_frame, the
// backend's own), DrawMenu, ImGui::Render. false: the menu is closed (or closed by this frame's input), nothing to draw.
bool OverlayBuildFrame(float W, float H, void (*new_frame)());

// HP2_OVERLAY_SHOT: the BMP to write this frame's picture to, or empty when no shot was asked for.
std::string OverlayShotPath();
