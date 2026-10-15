/// @file vulkan_diagnostics.cpp
/// @brief Frame capture tools (RenderDoc's in-application API, tools reported by the driver) and the
///        report of a lost device: GPU breadcrumbs (NVIDIA diagnostic checkpoints) and the driver's fault
///        report (VK_EXT_device_fault).
module;

#include <volk.h>

#include "renderdoc_capture.hpp"

module gfx.backends.vulkan;

import std;
import gfx.enums;
import gfx.errors;
import gfx.handles;
import gfx.interface;
import :common;
import :instance;
import :device;

namespace gfx::vulkan
{

// ---------------------------------------------------------------------------------------------------
// Capture tools
// ---------------------------------------------------------------------------------------------------

auto
VulkanDevice::detect_capture_tool() const -> CaptureTool
{
    // RenderDoc injected into the process exposes its API.
    if (renderdoc::is_attached())
    {
        return CaptureTool::RENDERDOC;
    }

    // Other tools announce themselves to the driver (Vulkan 1.3 tool properties).
    std::uint32_t count = 0;
    if (vkGetPhysicalDeviceToolProperties(m_physical_device.native(), &count, nullptr) != VK_SUCCESS)
    {
        return CaptureTool::NONE;
    }
    std::vector<VkPhysicalDeviceToolProperties> tools(count, {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TOOL_PROPERTIES});
    if (vkGetPhysicalDeviceToolProperties(m_physical_device.native(), &count, tools.data()) != VK_SUCCESS)
    {
        return CaptureTool::NONE;
    }
    for (const VkPhysicalDeviceToolProperties& tool : tools)
    {
        const std::string_view name = tool.name;
        if (name.contains("RenderDoc"))
        {
            return CaptureTool::RENDERDOC;
        }
        if (name.contains("Nsight"))
        {
            return CaptureTool::NSIGHT_GRAPHICS;
        }
        if (name.contains("PIX"))
        {
            return CaptureTool::PIX;
        }
    }
    return CaptureTool::NONE;
}

auto
VulkanDevice::capture_tool() const noexcept -> CaptureTool
{
    return m_capture_tool;
}

void
VulkanDevice::begin_capture(std::string_view title)
{
    static constexpr std::string_view OPERATION = "Device::begin_capture";

    // Only RenderDoc starts captures on request; other tools capture from their own interface.
    if (m_capture_tool != CaptureTool::RENDERDOC || !renderdoc::is_attached())
    {
        throw_unsupported(OPERATION, "programmatic frame capture",
                          m_capture_tool == CaptureTool::NONE
                              ? std::string{"no capture tool is attached to the process"}
                              : std::format("{} captures are started from its own interface", m_capture_tool),
                          "start the application from RenderDoc (or renderdoccmd capture) to capture from code");
    }
    if (m_capturing.exchange(true))
    {
        throw InvalidUsageError({.backend = BackendKind::VULKAN,
                                 .operation = std::string{OPERATION},
                                 .problem = "a capture is already running",
                                 .suggestion = "call Device::end_capture() first"});
    }
    const std::string terminated{title};
    if (!renderdoc::start_capture(m_physical_device.instance().native(), terminated.c_str()))
    {
        m_capturing = false;
        throw_unsupported(OPERATION, "programmatic frame capture", "RenderDoc refused to start the capture", "");
    }
}

void
VulkanDevice::end_capture()
{
    static constexpr std::string_view OPERATION = "Device::end_capture";
    if (!m_capturing.exchange(false))
    {
        throw InvalidUsageError({.backend = BackendKind::VULKAN,
                                 .operation = std::string{OPERATION},
                                 .problem = "no capture is running",
                                 .suggestion = "call Device::begin_capture() first"});
    }
    if (!renderdoc::end_capture(m_physical_device.instance().native()))
    {
        throw_unsupported(OPERATION, "programmatic frame capture", "RenderDoc did not write the capture",
                          "check RenderDoc's capture directory and log");
    }
}

// ---------------------------------------------------------------------------------------------------
// Lost devices
// ---------------------------------------------------------------------------------------------------

void
VulkanDevice::report_device_lost(std::string_view operation,
                                 std::string_view function,
                                 std::source_location location) const
{
    DeviceLostDescription description{
        .backend = BackendKind::VULKAN,
        .operation = std::string{operation},
        .details = std::format("{} returned VK_ERROR_DEVICE_LOST on '{}'", function, m_physical_device.properties().name),
    };

    // Breadcrumbs: the labels each queue's last checkpoint was inside (the deepest chain wins).
    if (has_any(m_crash_diagnostics, CrashDiagnostic::BREADCRUMBS) && enabled(DeviceFeature::GPU_CRASH_MARKERS))
    {
        for (const std::unique_ptr<NativeQueue>& queue : m_native_queues)
        {
            std::uint32_t count = 0;
            m_functions.vkGetQueueCheckpointDataNV(queue->queue, &count, nullptr);
            std::vector<VkCheckpointDataNV> checkpoints(count, {.sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV});
            m_functions.vkGetQueueCheckpointDataNV(queue->queue, &count, checkpoints.data());
            for (const VkCheckpointDataNV& checkpoint : checkpoints)
            {
                // A beginning means the GPU is inside that label; an end, inside its parent.
                const auto* node = static_cast<const BreadcrumbLabel*>(checkpoint.pCheckpointMarker);
                const BreadcrumbLabel* inside = node == nullptr ? nullptr : node->opened != nullptr ? node->parent : node;
                std::vector<std::string> chain;
                for (const BreadcrumbLabel* label = inside; label != nullptr; label = label->parent)
                {
                    chain.insert(chain.begin(), label->name);
                }
                if (chain.size() > description.active_markers.size())
                {
                    description.active_markers = std::move(chain);
                }
            }
        }
    }

    // The driver's own report, written next to the application.
    if (has_any(m_crash_diagnostics, CrashDiagnostic::VENDOR_REPORT))
    {
        try
        {
            description.crash_report_path = write_fault_report();
        }
        catch (...)
        {
            description.details += "; the driver's fault report could not be written";
        }
    }
    throw DeviceLostError(std::move(description), location);
}

auto
VulkanDevice::write_fault_report() const -> std::string
{
    // How much the driver has to say, then what it says.
    VkDeviceFaultCountsEXT counts{.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
    m_functions.vkGetDeviceFaultInfoEXT(m_device, &counts, nullptr);
    std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
    std::vector<VkDeviceFaultVendorInfoEXT> vendors(counts.vendorInfoCount);
    std::vector<std::byte> binary(static_cast<std::size_t>(counts.vendorBinarySize));
    VkDeviceFaultInfoEXT information{
        .sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
        .pAddressInfos = addresses.empty() ? nullptr : addresses.data(),
        .pVendorInfos = vendors.empty() ? nullptr : vendors.data(),
        .pVendorBinaryData = binary.empty() ? nullptr : binary.data(),
    };
    m_functions.vkGetDeviceFaultInfoEXT(m_device, &counts, &information);

    // A text report, and the vendor's binary dump beside it.
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::filesystem::path text_path = std::filesystem::current_path() / std::format("gfx-device-fault-{:%Y%m%d-%H%M%S}.txt", now);
    std::ofstream report{text_path};
    report << std::format("gfx device fault report\nDevice: {}\nDescription: {}\n", m_physical_device.properties().name,
                          information.description);
    for (std::size_t index = 0; index < addresses.size(); ++index)
    {
        report << std::format("Address {}: type {}, address {:#x}, precision {:#x}\n", index, static_cast<int>(addresses[index].addressType),
                              addresses[index].reportedAddress, addresses[index].addressPrecision);
    }
    for (std::size_t index = 0; index < vendors.size(); ++index)
    {
        report << std::format("Vendor information {}: {} (code {:#x}, data {:#x})\n", index, vendors[index].description,
                              vendors[index].vendorFaultCode, vendors[index].vendorFaultData);
    }
    if (!binary.empty())
    {
        std::filesystem::path binary_path = text_path;
        binary_path.replace_extension(".bin");
        std::ofstream dump{binary_path, std::ios::binary};
        dump.write(reinterpret_cast<const char*>(binary.data()), static_cast<std::streamsize>(binary.size()));
        report << std::format("Vendor binary: {} ({} bytes)\n", binary_path.generic_string(), binary.size());
    }
    return text_path.generic_string();
}

} // namespace gfx::vulkan
