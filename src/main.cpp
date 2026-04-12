#include "app/Application.h"

#include <windows.h>

#include <exception>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    try
    {
        align::Application app;
        return app.Run();
    }
    catch (const std::exception&)
    {
        return 1;
    }
}
