// overlay_vk.cpp -- the settings menu (overlay.h) drawn with Dear ImGui's Vulkan backend onto the window's swapchain image, after
// the game's picture is in it and before it is presented (native_vk.cpp, blit_to_window). NFSU2's overlay.mm does the same on
// Metal; the menu itself is overlay_core.cpp's, shared.
//
//   - Nothing happens while the menu is closed: no ImGui frame, no barrier, no pass (one atomic load).
//   - The renderer's Vulkan is loaded by hand (VK_NO_PROTOTYPES, MoltenVK dlopened), so the backend is built with
//     IMGUI_IMPL_VULKAN_NO_PROTOTYPES and gets its functions from the renderer's vkGetInstanceProcAddr, and so do the few here.
//   - A classic render pass (load, store) rather than dynamic rendering: the renderer's device is made without
//     VK_KHR_dynamic_rendering, and a render pass works on every Vulkan 1.0 driver. The image goes TRANSFER_DST -> COLOR_ATTACHMENT
//     -> (TRANSFER_SRC for a test shot) -> TRANSFER_DST again, so the renderer's own barrier to PRESENT_SRC is unchanged.
//   - The swapchain's image views and framebuffers are made as images are first drawn on, and dropped when the renderer makes
//     its swapchain again (overlay_vulkan_swapchain_gone, with the device idle). A new image format remakes the pass and the
//     backend's pipeline.
//   - HP2_OVERLAY_SHOT: the image is copied to a host buffer after the menu is drawn; the BMP is written at the next frame's
//     overlay_present_vulkan, when the renderer has waited for the last frame's fence (it records into one command buffer).
//   - Render thread only. The backend's texture uploads submit to the renderer's queue and wait: the same thread as all of the
//     renderer's submits, so the queue is never used from two threads at once.
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "overlay.h"
#include "overlay_internal.h"

namespace
{
#define OV_FUNCS(X)                                                                                                    \
    X(vkGetDeviceProcAddr) X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateRenderPass) X(vkDestroyRenderPass)         \
    X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) X(vkCmdPipelineBarrier)  \
    X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdCopyImageToBuffer) X(vkCreateBuffer) X(vkDestroyBuffer)        \
    X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory)
#define OV_DECLARE(name) PFN_##name name;
    OV_FUNCS(OV_DECLARE)

    OverlayVulkan g_vk;
    bool g_loaded, g_failed;        // the functions; a failure is said once and the menu is then not drawn
    bool g_backend;                 // ImGui_ImplVulkan_Init done for g_format
    VkFormat g_format = VK_FORMAT_UNDEFINED;
    VkRenderPass g_pass;

    struct Target
    {
        VkImage image;
        VkImageView view;
        VkFramebuffer fb;
        uint32_t w, h;
    };
    std::vector<Target> g_targets;  // per swapchain image

    // the test shot's buffer, and the BMP waiting for its frame to finish
    VkBuffer g_rb;
    VkDeviceMemory g_rb_mem;
    void *g_rb_map;
    VkDeviceSize g_rb_size;
    std::string g_rb_path;
    uint32_t g_rb_w, g_rb_h;
    bool g_rb_swap;                 // RGBA: swapped to the BMP's BGRA

    void CheckVk(VkResult r)
    {
        if (r != VK_SUCCESS)
            fprintf(stderr, "[overlay] Vulkan error %d\n", (int)r);
    }

    PFN_vkVoidFunction Load(const char *name, void *)
    {
        PFN_vkVoidFunction f = vkGetDeviceProcAddr ? vkGetDeviceProcAddr(g_vk.device, name) : nullptr;
        return f ? f : g_vk.get_instance_proc_addr(g_vk.instance, name);
    }

    bool LoadFunctions()
    {
        vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr)g_vk.get_instance_proc_addr(g_vk.instance, "vkGetDeviceProcAddr");
#define OV_LOAD(name)                                   \
        name = (PFN_##name)Load(#name, nullptr);        \
        if (!name) {                                    \
            fprintf(stderr, "[overlay] no %s\n", #name); \
            return false;                               \
        }
        OV_FUNCS(OV_LOAD)
#undef OV_LOAD
        return ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_0, Load);
    }

    void DropTargets()
    {
        for (Target &t : g_targets) {
            vkDestroyFramebuffer(g_vk.device, t.fb, nullptr);
            vkDestroyImageView(g_vk.device, t.view, nullptr);
        }
        g_targets.clear();
    }

    // The pass and the backend for this image format (made again when it changes).
    bool Backend(VkFormat format)
    {
        if (g_backend && format == g_format)
            return true;
        if (g_backend) {        // a new format comes with a new swapchain, made with the device idle
            ImGui_ImplVulkan_Shutdown();
            DropTargets();
            vkDestroyRenderPass(g_vk.device, g_pass, nullptr);
            g_pass = VK_NULL_HANDLE;
            g_backend = false;
        }
        VkAttachmentDescription a = {};
        a.format = format;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;            // over the game's picture
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sub = {};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkRenderPassCreateInfo rp = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rp.attachmentCount = 1;
        rp.pAttachments = &a;
        rp.subpassCount = 1;
        rp.pSubpasses = &sub;
        if (vkCreateRenderPass(g_vk.device, &rp, nullptr, &g_pass) != VK_SUCCESS)
            return false;
        ImGui_ImplVulkan_InitInfo info = {};
        info.ApiVersion = VK_API_VERSION_1_0;
        info.Instance = g_vk.instance;
        info.PhysicalDevice = g_vk.physical_device;
        info.Device = g_vk.device;
        info.QueueFamily = g_vk.queue_family;
        info.Queue = g_vk.queue;
        info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE + 8;
        // the backend's vertex buffers are reused after ImageCount frames; the renderer has one frame in flight
        info.MinImageCount = 2;
        info.ImageCount = 3;
        info.PipelineInfoMain.RenderPass = g_pass;
        info.PipelineInfoMain.Subpass = 0;
        info.CheckVkResultFn = CheckVk;
        if (!ImGui_ImplVulkan_Init(&info))
            return false;
        g_format = format;
        g_backend = true;
        fprintf(stderr, "[overlay] Dear ImGui %s on Vulkan, format %d\n", ImGui::GetVersion(), (int)format);
        return true;
    }

    Target *TargetFor(VkImage image, VkFormat format, uint32_t w, uint32_t h)
    {
        for (Target &t : g_targets)
            if (t.image == image && t.w == w && t.h == h)
                return &t;
        Target t = { image, VK_NULL_HANDLE, VK_NULL_HANDLE, w, h };
        VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(g_vk.device, &vi, nullptr, &t.view) != VK_SUCCESS)
            return nullptr;
        VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fi.renderPass = g_pass;
        fi.attachmentCount = 1;
        fi.pAttachments = &t.view;
        fi.width = w;
        fi.height = h;
        fi.layers = 1;
        if (vkCreateFramebuffer(g_vk.device, &fi, nullptr, &t.fb) != VK_SUCCESS) {
            vkDestroyImageView(g_vk.device, t.view, nullptr);
            return nullptr;
        }
        g_targets.push_back(t);
        return &g_targets.back();
    }

    void Barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src, VkAccessFlags dst,
                 VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage)
    {
        VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcAccessMask = src;
        b.dstAccessMask = dst;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    // A host-visible buffer for the test shot, w x h x 4 bytes.
    bool ShotBuffer(VkDeviceSize size)
    {
        if (g_rb && g_rb_size >= size)
            return true;
        if (g_rb) {
            vkDestroyBuffer(g_vk.device, g_rb, nullptr);
            vkFreeMemory(g_vk.device, g_rb_mem, nullptr);
            g_rb = VK_NULL_HANDLE;
        }
        VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (vkCreateBuffer(g_vk.device, &bi, nullptr, &g_rb) != VK_SUCCESS)
            return false;
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(g_vk.device, g_rb, &req);
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(g_vk.physical_device, &mp);
        const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < mp.memoryTypeCount && type == UINT32_MAX; i++)
            if ((req.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
                type = i;
        VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = type;
        if (type == UINT32_MAX || vkAllocateMemory(g_vk.device, &ai, nullptr, &g_rb_mem) != VK_SUCCESS) {
            vkDestroyBuffer(g_vk.device, g_rb, nullptr);
            g_rb = VK_NULL_HANDLE;
            return false;
        }
        vkBindBufferMemory(g_vk.device, g_rb, g_rb_mem, 0);
        vkMapMemory(g_vk.device, g_rb_mem, 0, VK_WHOLE_SIZE, 0, &g_rb_map);
        g_rb_size = size;
        return true;
    }

    // The last frame's shot to its BMP (that frame is done: the renderer waited for its fence before this one).
    void WriteShot()
    {
        const std::string path = g_rb_path;
        g_rb_path.clear();
        FILE *f = fopen(path.c_str(), "wb");
        if (!f) {
            fprintf(stderr, "[overlay] cannot write %s\n", path.c_str());
            return;
        }
        const uint32_t w = g_rb_w, h = g_rb_h, row = w * 4;
        uint32_t size = 54 + row * h;
        uint8_t hd[54] = { 'B', 'M' };
        memcpy(hd + 2, &size, 4);
        uint32_t off = 54, hs = 40, img = row * h;
        int32_t iw = (int32_t)w, ih = -(int32_t)h;      // top-down
        uint16_t planes = 1, bpp = 32;
        memcpy(hd + 10, &off, 4); memcpy(hd + 14, &hs, 4); memcpy(hd + 18, &iw, 4); memcpy(hd + 22, &ih, 4);
        memcpy(hd + 26, &planes, 2); memcpy(hd + 28, &bpp, 2); memcpy(hd + 34, &img, 4);
        fwrite(hd, 1, 54, f);
        std::vector<uint8_t> line(row);
        for (uint32_t y = 0; y < h; y++) {
            memcpy(line.data(), (const uint8_t *)g_rb_map + (size_t)y * row, row);
            if (g_rb_swap)
                for (uint32_t x = 0; x < w; x++)
                    std::swap(line[x * 4], line[x * 4 + 2]);
            fwrite(line.data(), 1, row, f);
        }
        fclose(f);
        fprintf(stderr, "[overlay] shot %s\n", path.c_str());
    }
}

extern "C" void overlay_vulkan_swapchain_gone(void)
{
    if (g_loaded)
        DropTargets();
}

extern "C" void overlay_present_vulkan(const OverlayVulkan *vk, VkCommandBuffer cmd, VkImage image, VkFormat format,
                                       uint32_t width, uint32_t height, VkImageUsageFlags usage)
{
    if (!g_rb_path.empty())
        WriteShot();
    if (!overlay_is_open() || g_failed)
        return;
    if (!(usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) || !width || !height)
        return;
    if (!g_loaded) {
        g_vk = *vk;
        if (!LoadFunctions()) {
            g_failed = true;
            fprintf(stderr, "[overlay] the Vulkan functions are not there: no menu\n");
            return;
        }
        OverlayImGuiCreate();
        g_loaded = true;
    }
    if (!Backend(format)) {
        g_failed = true;
        fprintf(stderr, "[overlay] the ImGui Vulkan backend did not start: no menu\n");
        return;
    }
    if (!OverlayBuildFrame((float)width, (float)height, ImGui_ImplVulkan_NewFrame))
        return;
    Target *t = TargetFor(image, format, width, height);
    if (!t)
        return;

    Barrier(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    VkRenderPassBeginInfo rb = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rb.renderPass = g_pass;
    rb.framebuffer = t->fb;
    rb.renderArea.extent = { width, height };
    vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRenderPass(cmd);

    const std::string shot = OverlayShotPath();
    const VkDeviceSize bytes = (VkDeviceSize)width * height * 4;
    const bool bgra = format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
    const bool rgba = format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
    if (!shot.empty() && (usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) && (bgra || rgba) && ShotBuffer(bytes)) {
        Barrier(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy c = {};
        c.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        c.imageExtent = { width, height, 1 };
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_rb, 1, &c);
        Barrier(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        g_rb_path = shot;
        g_rb_w = width, g_rb_h = height, g_rb_swap = rgba;
    } else {
        if (!shot.empty())
            fprintf(stderr, "[overlay] no shot: the window's images cannot be read (usage %x, format %d)\n", (unsigned)usage, (int)format);
        Barrier(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
}
