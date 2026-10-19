// The accessibility tree reaching a screen reader's API: a window of every platform backend gets a
// UiAccessibilityAdapter (AccessKit); a Windows UI Automation client on another thread (as a screen reader is a
// client) finds the UI's nodes under the window and invokes a button, while this thread pumps the platform and runs
// frames. The tree is built only once the client asked for it.
#include <gfx/windows_headers.hpp>

#if defined(_WIN32)
#include <ole2.h>
#include <uiautomation.h>
#endif

#include <gtest/gtest.h>

import std;
import gfx.platform;
import gfx.ui;
import gfx.ui.platform;
import gfx.tests.platform_latin;

#if defined(_WIN32)

namespace
{

using namespace std::chrono_literals;
using gfx::platform::PlatformBackendKind;

/// What the client found under the window.
struct ClientResult
{
    std::vector<std::wstring> names{};
    long button_control_type = 0;
    bool found_button = false;
    bool invoked = false;
};

/// A UI Automation client (on its own COM apartment): queries every element under @p window until one is named
/// @p button (the tree arrives a frame after the client's first request) or @p deadline passes, then invokes it.
auto
query_window(HWND window,
             std::wstring_view button,
             std::chrono::steady_clock::time_point deadline) -> ClientResult
{
    ClientResult result;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {
        return result;
    }
    IUIAutomation* automation = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation, reinterpret_cast<void**>(&automation))))
    {
        CoUninitialize();
        return result;
    }
    while (!result.found_button && std::chrono::steady_clock::now() < deadline)
    {
        // Every element under the window, by name.
        IUIAutomationElement* root = nullptr;
        IUIAutomationCondition* every = nullptr;
        IUIAutomationElementArray* found = nullptr;
        automation->ElementFromHandle(window, &root);
        automation->CreateTrueCondition(&every);
        result.names.clear();
        if (root != nullptr && every != nullptr && SUCCEEDED(root->FindAll(TreeScope_Descendants, every, &found)) && found != nullptr)
        {
            int count = 0;
            found->get_Length(&count);
            for (int index = 0; index < count; ++index)
            {
                IUIAutomationElement* element = nullptr;
                found->GetElement(index, &element);
                if (element == nullptr)
                {
                    continue;
                }
                BSTR name = nullptr;
                element->get_CurrentName(&name);
                const std::wstring text = name != nullptr ? std::wstring{name} : std::wstring{};
                SysFreeString(name);
                result.names.push_back(text);

                // The button: its control type, and its Invoke pattern used.
                if (text == button && !result.found_button)
                {
                    result.found_button = true;
                    CONTROLTYPEID type = 0;
                    element->get_CurrentControlType(&type);
                    result.button_control_type = type;
                    IUIAutomationInvokePattern* invoke = nullptr;
                    element->GetCurrentPatternAs(UIA_InvokePatternId, IID_IUIAutomationInvokePattern, reinterpret_cast<void**>(&invoke));
                    if (invoke != nullptr)
                    {
                        result.invoked = SUCCEEDED(invoke->Invoke());
                        invoke->Release();
                    }
                }
                element->Release();
            }
            found->Release();
        }
        if (every != nullptr)
        {
            every->Release();
        }
        if (root != nullptr)
        {
            root->Release();
        }
        if (!result.found_button)
        {
            std::this_thread::sleep_for(50ms);
        }
    }
    automation->Release();
    CoUninitialize();
    return result;
}

class UiAccessibilityPlatformTest : public testing::TestWithParam<PlatformBackendKind>
{
};

TEST_P(UiAccessibilityPlatformTest, AUiAutomationClientFindsTheNodesAndInvokesAButton)
{
    // A visible window of the backend, a UI in it, the adapter.
    const std::unique_ptr<gfx::platform::Platform> platform = gfx::platform::create_platform(GetParam(), {.application_name = "gfx accessibility test"});
    gfx::platform::WindowDescription description;
    description.title = "gfx accessibility test";
    description.size = gfx::Extent{.width = 480, .height = 320, .depth = 1};
    const std::unique_ptr<gfx::platform::Window> window = platform->create_window(description);
    const gfx::NativeWindowHandle native_handle = window->native_handle();
    const gfx::Win32WindowHandle* handle = native_handle.get_if<gfx::Win32WindowHandle>();
    ASSERT_NE(handle, nullptr);
    const HWND native = reinterpret_cast<HWND>(handle->window);
    gfx::ui::UiContext ui;
    ui.load_font(gfx::ui::FontAtlas::load(gfx::tests::platform_atlases::platform_latin), 16.0F);
    gfx::ui::UiPlatformInput input{*window};
    gfx::ui::UiAccessibilityAdapter accessibility{*window};
    bool autosave = true;
    std::uint32_t saved = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto frame = [&]
    {
        // Pump (WM_GETOBJECT reaches the adapter), then a frame with the requests received, then the tree given.
        input.begin_frame();
        input.add_events(platform->poll_events());
        gfx::ui::UiFrameInput frame_input = input.frame_input(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        frame_input.accessibility_requests = accessibility.take_requests();
        gfx::ui::UiFrame& ui_frame = ui.begin_frame(frame_input);
        ui_frame.set_next_window_position({.x = 10, .y = 10}, gfx::ui::UiCondition::FIRST_USE_EVER);
        if (ui_frame.begin_window("Document"))
        {
            saved += ui_frame.button("Save") ? 1U : 0U;
            static_cast<void>(ui_frame.checkbox("Autosave", autosave));
        }
        ui_frame.end_window();
        static_cast<void>(ui.end_frame());
        accessibility.update(ui);
        std::this_thread::sleep_for(2ms);
    };
    for (std::int32_t warm_up = 0; warm_up < 5; ++warm_up)
    {
        frame();
    }
    EXPECT_FALSE(ui.accessibility_enabled()) << "no assistive technology asked yet";

    // The client on its own thread while this one pumps and runs frames; then the Invoke request arrives.
    std::atomic<bool> done{false};
    ClientResult result;
    std::jthread client{[&]
    {
        result = query_window(native, L"Save", std::chrono::steady_clock::now() + 20s);
        done = true;
    }};
    while (!done && std::chrono::steady_clock::now() - start < 30s)
    {
        frame();
    }
    client.join();
    for (std::int32_t settle = 0; settle < 10 && saved == 0; ++settle)
    {
        frame();
    }
    EXPECT_TRUE(accessibility.active());
    EXPECT_TRUE(ui.accessibility_enabled());
    ASSERT_TRUE(result.found_button) << "names found: " << result.names.size();
    EXPECT_EQ(result.button_control_type, UIA_ButtonControlTypeId);
    EXPECT_TRUE(std::ranges::contains(result.names, std::wstring{L"Document"}));
    EXPECT_TRUE(std::ranges::contains(result.names, std::wstring{L"Autosave"}));
    EXPECT_TRUE(result.invoked);
    EXPECT_EQ(saved, 1U) << "the Invoke reached the button as a CLICK request (after a FOCUS one, in the same frame or not)";
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         UiAccessibilityPlatformTest,
                         testing::ValuesIn(gfx::platform::available_platform_backends()),
                         [](const testing::TestParamInfo<PlatformBackendKind>& parameter) { return std::format("{}", parameter.param); });
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(UiAccessibilityPlatformTest);

} // namespace

#endif
