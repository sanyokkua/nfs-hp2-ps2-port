// NFS Hot Pursuit 2. GPL-3.0-or-later.
// The headless build (no Vulkan renderer): what native_vk.cpp defines and the rest of the runtime refers to.
#include "native.h"

NativeLimit g_native_limits[NL_COUNT];
void native_vk_gpus(std::vector<std::string> &names, std::string &now)
{
    names.clear();
    now = "none (headless)";
}
