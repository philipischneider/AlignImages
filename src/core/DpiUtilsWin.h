#pragma once

#include "data/SessionModel.h"

struct GLFWwindow;

namespace align
{
void EnablePerMonitorDpiAwareness();
float GetWindowDpiScale(GLFWwindow* window);
float ResolveUiScaleForWindow(GLFWwindow* window, const UiPreferences& preferences);
} // namespace align

