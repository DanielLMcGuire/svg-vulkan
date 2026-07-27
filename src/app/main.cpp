#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <iostream>
#endif

#include "cli_options.h"
#include "fallback_svg.h"
#include "renderer.h"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static const char *BASE_WINDOW_TITLE = "Vulkan SVG Renderer";

#define BUILD_TITLE(x, y, z)                                                   \
    ((y).empty() ? ((z).empty() ? (x) : ((x) + std::string(" | ") + (z)))      \
                 : ((z).empty() ? ((x) + std::string(" | ") + (y))             \
                                : ((x) + std::string(" | ") + (y) +            \
                                   std::string(" | ") + (z))))

static VulkanSVGRenderer g_renderer;
static int g_width = 800;
static int g_height = 600;
static bool g_running = true;

#ifdef _WIN32
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
    {
        int w = LOWORD(lp), h = HIWORD(lp);
        if (w > 0 && h > 0 && (w != g_width || h != g_height))
        {
            g_width = w;
            g_height = h;
            g_renderer.resize(w, h);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
        {
            g_running = false;
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
#else
void ShowError(const std::string &title, const std::string &message)
{
    fprintf(stderr, "MESSAGE: [%s] %s\n", title.c_str(), message.c_str());
}
#endif

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmd, int nShow)
{
#ifdef _DEBUG
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    printf("[DBG]  Vulkan SVG Renderer starting\n");
#endif

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VulkanSVGRenderer";
    RegisterClassExW(&wc);

    CliOptions opts = parseArgs(splitCommandLine(lpCmd ? lpCmd : ""));
    if (opts.wantRaster)
    {
        g_width = opts.rasterWidth;
        g_height = opts.rasterHeight;
    }

    RECT wr = {0, 0, g_width, g_height};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(
        0, L"VulkanSVGRenderer", L"Vulkan SVG Renderer", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd)
    {
        MessageBoxW(nullptr, L"CreateWindow failed", L"Error",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    try
    {
        if (!g_renderer.init(hwnd, g_width, g_height))
        {
            MessageBoxW(nullptr, L"Renderer init failed", L"Error",
                        MB_OK | MB_ICONERROR);
            return 1;
        }
    }
    catch (std::exception &e)
    {
        MessageBoxA(nullptr, e.what(), "Vulkan Init Error",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    std::string svgContent;
    if (!opts.svgPath.empty())
    {
        std::ifstream f(opts.svgPath);
        if (f.good())
        {
            std::ostringstream ss;
            ss << f.rdbuf();
            svgContent = ss.str();
        }
        else
        {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not open '%s', using fallback.",
                     opts.svgPath.c_str());
            MessageBoxA(nullptr, msg, "Warning", MB_OK | MB_ICONWARNING);
            svgContent = kFallbackSVG;
        }
    }
    else
    {
        svgContent = kFallbackSVG;
    }

    try
    {
        if (!opts.svgPath.empty())
        {
            g_renderer.loadSVGString(svgContent, opts.svgPath);
        }
        else
        {
            g_renderer.loadSVGString(svgContent, "default.svg");
        }
    }
    catch (std::exception &e)
    {
        MessageBoxA(nullptr, e.what(), "SVG Load Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::string newTitle = BUILD_TITLE(BASE_WINDOW_TITLE, g_renderer.svgTitle(),
                                       g_renderer.svgPath());
    int wlen =
        MultiByteToWideChar(CP_UTF8, 0, newTitle.c_str(), -1, nullptr, 0);
    if (wlen > 0)
    {
        std::wstring wtitle(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, newTitle.c_str(), -1, wtitle.data(),
                            wlen);
        SetWindowTextW(hwnd, wtitle.c_str());
    }

    if (opts.wantRaster)
    {
        g_renderer.render(1.f, 1.f, 1.f);
        bool ok = g_renderer.saveFrameToPPM(opts.ppmPath);
        if (!ok)
        {
            char msg[512];
            snprintf(msg, sizeof(msg), "Failed to write PPM to '%s'.",
                     opts.ppmPath.c_str());
            MessageBoxA(nullptr, msg, "PPM Export Error", MB_OK | MB_ICONERROR);
            DestroyWindow(hwnd);
            return 1;
        }
        char msg[512];
        snprintf(msg, sizeof(msg), "Saved %dx%d PPM raster to '%s'.", g_width,
                 g_height, opts.ppmPath.c_str());
        MessageBoxA(nullptr, msg, "PPM Export", MB_OK | MB_ICONINFORMATION);
        DestroyWindow(hwnd);
        return 0;
    }

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (g_running)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                g_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!g_running)
            break;

        g_renderer.render(0.f, 0.f, 0.f);
        g_renderer.present(true);
    }

    return (int)msg.wParam;
}
#else
int main(int argc, char **argv)
{
#ifdef _DEBUG
    printf("[DBG]  Vulkan SVG Renderer starting\n");
#endif

    CliOptions opts =
        parseArgs(std::vector<std::string>(argv + 1, argv + argc));
    if (opts.wantRaster)
    {
        g_width = opts.rasterWidth;
        g_height = opts.rasterHeight;
    }

    Display *display = XOpenDisplay(nullptr);
    if (!display)
    {
        ShowError("Error", "Cannot open X display");
        return 1;
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    Window hwnd = XCreateSimpleWindow(display, root, 0, 0, g_width, g_height, 1,
                                      BlackPixel(display, screen),
                                      WhitePixel(display, screen));

    if (!hwnd)
    {
        ShowError("Error", "CreateWindow failed");
        XCloseDisplay(display);
        return 1;
    }

    XStoreName(display, hwnd, "Vulkan SVG Renderer");
    XSelectInput(display, hwnd,
                 ExposureMask | KeyPressMask | StructureNotifyMask);

    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, hwnd, &wmDeleteMessage, 1);

    try
    {
        if (!g_renderer.init(display, hwnd, g_width, g_height))
        {
            ShowError("Error", "Renderer init failed");
            XCloseDisplay(display);
            return 1;
        }
    }
    catch (std::exception &e)
    {
        ShowError("Vulkan Init Error", e.what());
        XCloseDisplay(display);
        return 1;
    }

    std::string svgContent;
    if (!opts.svgPath.empty())
    {
        std::ifstream f(opts.svgPath);
        if (f.good())
        {
            std::ostringstream ss;
            ss << f.rdbuf();
            svgContent = ss.str();
        }
        else
        {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not open '%s', using fallback.",
                     opts.svgPath.c_str());
            ShowError("Warning", msg);
            svgContent = kFallbackSVG;
        }
    }
    else
    {
        svgContent = kFallbackSVG;
    }

    try
    {
        if (!opts.svgPath.empty())
        {
            g_renderer.loadSVGString(svgContent, opts.svgPath);
        }
        else
        {
            g_renderer.loadSVGString(svgContent, "default.svg");
        }
    }
    catch (std::exception &e)
    {
        ShowError("SVG Load Error", e.what());
        XCloseDisplay(display);
        return 1;
    }

    std::string newTitle = BUILD_TITLE(BASE_WINDOW_TITLE, g_renderer.svgTitle(),
                                       g_renderer.svgPath());
    XStoreName(display, hwnd, newTitle.c_str());

    XMapWindow(display, hwnd);
    XFlush(display);

    if (opts.wantRaster)
    {
        g_renderer.render(1.f, 1.f, 1.f);
        bool ok = g_renderer.saveFrameToPPM(opts.ppmPath);
        XDestroyWindow(display, hwnd);
        XCloseDisplay(display);
        if (!ok)
        {
            fprintf(stderr, "Failed to write PPM to '%s'.\n",
                    opts.ppmPath.c_str());
            return 1;
        }
        printf("Saved %dx%d PPM raster to '%s'.\n", g_width, g_height,
               opts.ppmPath.c_str());
        return 0;
    }

    XEvent ev;
    while (g_running)
    {
        while (XPending(display))
        {
            XNextEvent(display, &ev);

            switch (ev.type)
            {
            case ClientMessage:
                if (ev.xclient.data.l[0] == wmDeleteMessage)
                {
                    g_running = false;
                }
                break;

            case ConfigureNotify:
            {
                int w = ev.xconfigure.width;
                int h = ev.xconfigure.height;
                if (w > 0 && h > 0 && (w != g_width || h != g_height))
                {
                    g_width = w;
                    g_height = h;
                    g_renderer.resize(w, h);
                }
                break;
            }

            case KeyPress:
            {
                KeySym keysym = XLookupKeysym(&ev.xkey, 0);
                if (keysym == XK_Escape)
                {
                    g_running = false;
                }
                break;
            }
            }
        }

        if (!g_running)
            break;

        g_renderer.render(0.f, 0.f, 0.f);
        g_renderer.present(true);
    }

    XDestroyWindow(display, hwnd);
    XCloseDisplay(display);

    return 0;
}
#endif