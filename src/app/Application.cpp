#include "app/Application.h"

#include "core/DpiUtilsWin.h"
#include "core/OpenGLHeaders.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <objbase.h>
#include <stdexcept>

namespace
{
constexpr const char* kGlslVersion = "#version 130";
}

namespace align
{
Application::Application() = default;

Application::~Application()
{
    Shutdown();
}

int Application::Run()
{
    Initialize();
    MainLoop();
    Shutdown();
    return 0;
}

void Application::Initialize()
{
    if (m_initialized)
    {
        return;
    }

    EnablePerMonitorDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (glfwInit() == GLFW_FALSE)
    {
        throw std::runtime_error("Failed to initialize GLFW.");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);

    m_window = glfwCreateWindow(1920, 1080, "Align Images", nullptr, nullptr);
    if (m_window == nullptr)
    {
        throw std::runtime_error("Failed to create application window.");
    }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ConfigureImGui();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init(kGlslVersion);

    m_context.session = CreateDefaultSession();
    ApplyDpiScale();

    m_initialized = true;
}

void Application::Shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_window != nullptr)
    {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();
    CoUninitialize();
    m_initialized = false;
}

void Application::MainLoop()
{
    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();

        ApplyDpiScale();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        m_mainWindow.Draw(m_context, m_window);

        if (m_context.showDemoWindow)
        {
            ImGui::ShowDemoWindow(&m_context.showDemoWindow);
        }

        ImGui::Render();

        int displayWidth = 0;
        int displayHeight = 0;
        glfwGetFramebufferSize(m_window, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(m_window);
    }
}

void Application::ConfigureImGui() const
{
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
}

void Application::ApplyDpiScale()
{
    if (m_window == nullptr)
    {
        return;
    }

    float resolvedScale = ResolveUiScaleForWindow(m_window, m_context.session.uiPreferences);
    if (resolvedScale == m_lastAppliedUiScale)
    {
        return;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiStyle defaultStyle;
    style = defaultStyle;
    ImGui::StyleColorsDark();
    style.ScaleAllSizes(resolvedScale);
    ImGui::GetIO().FontGlobalScale = resolvedScale;
    m_lastAppliedUiScale = resolvedScale;
}
} // namespace align
