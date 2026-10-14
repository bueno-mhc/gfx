#include <gtest/gtest.h>

import std;
import gfx;

namespace {

using namespace gfx;

auto contains(std::string_view text, std::string_view part) -> bool {
    return text.find(part) != std::string_view::npos;
}

TEST(ErrorsTest, EveryErrorIsAStdRuntimeError) 
{
    static_assert(std::derived_from<Error, std::runtime_error>);
    static_assert(std::derived_from<BackendCapabilityError, Error>);
    static_assert(std::derived_from<InvalidUsageError, Error>);
    static_assert(std::derived_from<InvalidHandleError, Error>);
    static_assert(std::derived_from<OutOfMemoryError, Error>);
    static_assert(std::derived_from<DeviceLostError, Error>);
    static_assert(std::derived_from<BackendApiError, Error>);
    static_assert(std::derived_from<ShaderCompilationError, Error>);
    SUCCEED();
}

TEST(ErrorsTest, BackendCapabilityErrorNamesBackendOperationCapabilityReasonAndSuggestion) 
{
    UnsupportedCapabilityDescription description{
        .backend = BackendKind::METAL,
        .operation = "Device::create_event",
        .capability = "Event (split barrier)",
        .reason = "Metal has no GPU-side split-barrier object",
        .suggestion = "use TimelineSemaphore across queues",
    };
    // The location defaults to the line of the constructor call (GCC reports a call spanning several
    // lines at its last line, so the call is kept on one line).
    const auto expected_line = std::source_location::current().line() + 2;
    try {
        throw BackendCapabilityError(std::move(description));
    } catch (const BackendCapabilityError& error) 
{
        const std::string_view message = error.what();
        EXPECT_TRUE(message.starts_with(
            "[gfx::METAL] Device::create_event: capability 'Event (split barrier)' is not supported.\n"
            "  Reason: Metal has no GPU-side split-barrier object\n"
            "  Suggestion: use TimelineSemaphore across queues\n"
            "  Location: "))
            << message;
        EXPECT_TRUE(contains(message, "errors_test.cpp")) << message;
        EXPECT_EQ(error.backend(), BackendKind::METAL);
        EXPECT_EQ(error.operation(), "Device::create_event");
        EXPECT_EQ(error.capability(), "Event (split barrier)");
        EXPECT_EQ(error.reason(), "Metal has no GPU-side split-barrier object");
        EXPECT_EQ(error.suggestion(), "use TimelineSemaphore across queues");
        EXPECT_EQ(error.location().line(), expected_line);
        return;
    }
    FAIL() << "BackendCapabilityError was not thrown";
}

TEST(ErrorsTest, ErrorsCanBeCaughtThroughTheBaseClass) 
{
    EXPECT_THROW(throw DeviceLostError({.backend = BackendKind::VULKAN, .operation = "Queue::submit", .details = ""}), Error);
    EXPECT_THROW(throw DeviceLostError({.backend = BackendKind::VULKAN, .operation = "Queue::submit", .details = ""}),
                 std::runtime_error);
}

TEST(ErrorsTest, BackendIndependentErrorsUseTheLibraryPrefix) 
{
    const InvalidUsageError error({
        .backend = std::nullopt,
        .operation = "Device::create_buffer",
        .problem = "BufferDescription::size is 0",
        .suggestion = "",
    });
    const std::string_view message = error.what();
    EXPECT_TRUE(message.starts_with("[gfx] Device::create_buffer: BufferDescription::size is 0\n  Location: ")) << message;
    EXPECT_FALSE(contains(message, "Suggestion")) << message;
    EXPECT_FALSE(error.backend().has_value());
}

TEST(ErrorsTest, InvalidHandleErrorNamesTheHandleKind) 
{
    const InvalidHandleError error({
        .backend = BackendKind::VULKAN,
        .operation = "Recorder::bind",
        .handle_type = "ComputePipeline",
        .problem = "the pipeline was destroyed",
    });
    EXPECT_TRUE(contains(error.what(), "[gfx::VULKAN] Recorder::bind: invalid ComputePipeline handle: the pipeline was destroyed"))
        << error.what();
    EXPECT_EQ(error.handle_type(), "ComputePipeline");
}

TEST(ErrorsTest, OutOfMemoryErrorReportsDomainAndSize) 
{
    const OutOfMemoryError error({
        .backend = BackendKind::VULKAN,
        .operation = "Device::create_image",
        .domain = MemoryDomain::DEVICE,
        .requested_bytes = 268'435'456,
        .details = "heap 0 is full",
    });
    EXPECT_TRUE(contains(error.what(), "out of device memory while allocating 268435456 bytes.\n  Details: heap 0 is full"))
        << error.what();
    EXPECT_EQ(error.domain(), MemoryDomain::DEVICE);
    EXPECT_EQ(error.requested_bytes(), 268'435'456U);
}

TEST(ErrorsTest, BackendApiErrorReportsTheNativeResult) 
{
    const BackendApiError error({
        .backend = BackendKind::VULKAN,
        .operation = "Instance::create",
        .api_function = "vkCreateInstance",
        .result_code = -3,
        .result_name = "VK_ERROR_INITIALIZATION_FAILED",
    });
    EXPECT_TRUE(contains(error.what(), "vkCreateInstance failed with VK_ERROR_INITIALIZATION_FAILED (-3)")) << error.what();
    EXPECT_EQ(error.result_code(), -3);
    EXPECT_EQ(error.result_name(), "VK_ERROR_INITIALIZATION_FAILED");
}

TEST(ErrorsTest, ShaderCompilationErrorIndentsTheCompilerLog) 
{
    const ShaderCompilationError error({
        .operation = "Device::create_shader_module",
        .shader_name = "blur.frag",
        .stage = ShaderStage::FRAGMENT,
        .compiler_log = "ERROR: 0:12: 'color' : undeclared identifier\nERROR: 1 compilation errors.",
    });
    EXPECT_TRUE(contains(error.what(),
                         "[gfx] Device::create_shader_module: failed to compile FRAGMENT shader 'blur.frag':\n"
                         "    ERROR: 0:12: 'color' : undeclared identifier\n"
                         "    ERROR: 1 compilation errors."))
        << error.what();
    EXPECT_EQ(error.stage(), ShaderStage::FRAGMENT);
}

} // namespace
