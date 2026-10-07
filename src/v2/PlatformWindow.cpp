// Kept apart from the JUCE headers: <windows.h> and its macros stay in here.
#include "PlatformWindow.h"

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #define GLFW_INCLUDE_NONE
 #include <GLFW/glfw3.h>
 #define GLFW_EXPOSE_NATIVE_WIN32
 #include <GLFW/glfw3native.h>
#endif

namespace signalpatch::v2
{
void preparePlatformWindow (GLFWwindow* window)
{
   #if defined(_WIN32)
    auto* hwnd = glfwGetWin32Window (window);
    if (hwnd == nullptr)
        return;
    // The tablet service reads this window property (values from tpcshrd.h).
    constexpr DWORD disablePressAndHold = 0x00000001, disablePenTapFeedback = 0x00000008,
                    disablePenBarrelFeedback = 0x00000010, disableFlicks = 0x00010000;
    const wchar_t* property = L"MicrosoftTabletPenServiceProperty";
    if (const auto atom = GlobalAddAtomW (property); atom != 0)
    {
        SetPropW (hwnd, property, reinterpret_cast<HANDLE> (static_cast<ULONG_PTR> (disablePressAndHold | disablePenTapFeedback
                                                                                     | disablePenBarrelFeedback | disableFlicks)));
        GlobalDeleteAtom (atom);
    }
    // No contact rings or press-and-hold animation drawn by Windows over the rack.
    BOOL off = FALSE;
    for (const auto feedback : { FEEDBACK_TOUCH_CONTACTVISUALIZATION, FEEDBACK_TOUCH_TAP, FEEDBACK_TOUCH_DOUBLETAP,
                                 FEEDBACK_TOUCH_PRESSANDHOLD, FEEDBACK_TOUCH_RIGHTTAP, FEEDBACK_GESTURE_PRESSANDTAP })
        SetWindowFeedbackSetting (hwnd, feedback, 0, sizeof (off), &off);
   #else
    (void) window;
   #endif
}
} // namespace signalpatch::v2
