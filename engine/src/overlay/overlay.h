/*
 * overlay.h -- the in-game overlay: a settings menu drawn over the game with
 * Dear ImGui, the same for every port (NFSU2 first, then NFSU1, HP2...).
 * Modelled on SpeedBreaker's settings menu (project(u), GPL-3.0-or-later):
 * pages, one row per option, a controller first, Quit Game in the footer.
 * This is HP2's copy of NFSU2's overlay.h: the same API on SDL3, with a
 * Vulkan present (overlay_vk.cpp) in place of the Metal one.
 *
 *   - Open/close: F1, Back + Start on a controller, Cmd+, (the key).
 *     Esc or B closes it too.
 *   - While it is open it owns the keyboard and the controllers: the game
 *     reads a neutral pad (overlay_input_blocked) until every button is up
 *     again.
 *   - Threads: SDL events arrive on the main thread (overlay_event, queued);
 *     the menu is laid out and drawn where the game presents its frame
 *     (overlay_present_vulkan, the render thread). Things only the main
 *     thread may do (full screen, quit, opening a folder) are requested from
 *     the menu and run by overlay_main_tick.
 *   - Which rows exist is the game's table (overlay_options.h), so each game
 *     shows only its own settings, and marks those that apply at the next
 *     start.
 */
#ifndef OVERLAY_H
#define OVERLAY_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Main thread, before the window exists: settings.ini (the working directory)
 * read, and every option's value applied (environment defaults for the
 * game's start-up, live state for the rest). */
void overlay_settings_load(void);

/* Main thread, once the window exists. */
void overlay_init(SDL_Window *window);

/* Main thread, every SDL event before anyone else sees it. 1: the overlay took
 * it (the game must not see it). */
int overlay_event(const SDL_Event *e);

/* Main thread, every turn of the event loop: runs what the menu asked for. */
void overlay_main_tick(void);

/* Any thread. */
int overlay_is_open(void);
void overlay_toggle(void);

/* Any thread: 1 while the game must read a neutral pad and keyboard (open, and
 * after closing until every key and button is up). */
int overlay_input_blocked(void);

/* The host's part (defined by the host, called by overlay_main_tick on the
 * main thread): the window to full screen or back, and Quit Game, which
 * leaves the way closing the window does. */
void overlay_host_fullscreen(int on);
void overlay_host_quit(void);

/* The Vulkan present: only where <vulkan/vulkan.h> came first (the renderer),
 * so the host needs no Vulkan headers. */
#ifdef VK_VERSION_1_0
typedef struct OverlayVulkan {
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    uint32_t queue_family;
    VkQueue queue;                  /* the renderer's; the overlay uses it on the render thread only */
} OverlayVulkan;

/* Render thread, after the game's frame is in the swapchain image, before it
 * is presented: draws the menu when it is open (nothing at all when closed).
 * The image is in TRANSFER_DST_OPTIMAL before and after. It needs the image
 * made with COLOR_ATTACHMENT usage (and TRANSFER_SRC for HP2_OVERLAY_SHOT);
 * usage says which the swapchain has. */
void overlay_present_vulkan(const OverlayVulkan *vk, VkCommandBuffer cmd, VkImage image, VkFormat format,
                            uint32_t width, uint32_t height, VkImageUsageFlags usage);

/* Render thread, with the device idle, before the swapchain is destroyed or
 * made again: the overlay lets go of its views of the swapchain's images. */
void overlay_vulkan_swapchain_gone(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
