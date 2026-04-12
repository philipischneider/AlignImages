#pragma once

#include "app/AppContext.h"
#include "ui/MainWindow.h"

struct GLFWwindow;

namespace align
{
class Application
{
public:
    Application();
    ~Application();

    int Run();

private:
    void Initialize();
    void Shutdown();
    void MainLoop();
    void ConfigureImGui() const;
    void ApplyDpiScale();

    GLFWwindow* m_window = nullptr;
    AppContext m_context;
    MainWindow m_mainWindow;
    float m_lastAppliedUiScale = 1.0f;
    bool m_initialized = false;
};
} // namespace align

