#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #  define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #  define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/Xos.h>
    #include <iostream>
#endif

#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include "renderer.h"

static const char* IMAGE = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" width="800" height="600" viewBox="0 0 800 600" style="background:black">
</svg>
)SVG";

static VulkanSVGRenderer g_renderer;
static int  g_width   = 800;
static int  g_height  = 600;
static bool g_running = true;

#ifdef _WIN32
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp);
        if(w > 0 && h > 0 && (w != g_width || h != g_height)) {
            g_width = w; g_height = h;
            g_renderer.resize(w, h);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if(wp == VK_ESCAPE) { g_running = false; PostQuitMessage(0); }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
#else
void ShowError(const std::string& title, const std::string& message) {
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
    printf("[DEBUG] Vulkan SVG Renderer starting\n");
#endif

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VulkanSVGRenderer";
    RegisterClassExW(&wc);

    RECT wr = { 0, 0, g_width, g_height };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, L"VulkanSVGRenderer",
        L"Vulkan SVG Renderer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInst, nullptr);

    if(!hwnd) {
        MessageBoxW(nullptr, L"CreateWindow failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    try {
        if(!g_renderer.init(hwnd, g_width, g_height)) {
            MessageBoxW(nullptr, L"Renderer init failed", L"Error", MB_OK | MB_ICONERROR);
            return 1;
        }
    } catch(std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Vulkan Init Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::string svgContent;
    if(lpCmd && lpCmd[0]) {
        std::string path(lpCmd);
        if(!path.empty() && path.front() == '"') path = path.substr(1);
        if(!path.empty() && path.back()  == '"') path.pop_back();
        std::ifstream f(path);
        if(f.good()) {
            std::ostringstream ss; ss << f.rdbuf();
            svgContent = ss.str();
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not open '%s', using fallback.", path.c_str());
            MessageBoxA(nullptr, msg, "Warning", MB_OK | MB_ICONWARNING);
            svgContent = IMAGE;
        }
    } else {
        svgContent = IMAGE;
    }

    try {
        g_renderer.loadSVGString(svgContent);
    } catch(std::exception& e) {
        MessageBoxA(nullptr, e.what(), "SVG Load Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while(g_running) {
        while(PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if(msg.message == WM_QUIT) { g_running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if(!g_running) break;

        g_renderer.render(0.f, 0.f, 0.f);
        g_renderer.present(true);
    }

    return (int)msg.wParam;
}
#else
int main(int argc, char** argv)
{
#ifdef _DEBUG
    printf("[DEBUG] Vulkan SVG Renderer starting\n");
#endif

    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        ShowError("Error", "Cannot open X display");
        return 1;
    }

    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    Window hwnd = XCreateSimpleWindow(
        display, root, 
        0, 0, g_width, g_height, 
        1, 
        BlackPixel(display, screen), 
        WhitePixel(display, screen)
    );

    XStoreName(display, hwnd, "Vulkan SVG Renderer");
    XSelectInput(display, hwnd, ExposureEvent | KeyPressEvent | StructureNotifyEvent);

    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, hwnd, &wmDeleteMessage, 1);

    if (!hwnd) {
        ShowError("Error", "CreateWindow failed");
        XCloseDisplay(display);
        return 1;
    }

    try {
        if (!g_renderer.init(hwnd, g_width, g_height)) {
            ShowError("Error", "Renderer init failed");
            XCloseDisplay(display);
            return 1;
        }
    } catch (std::exception& e) {
        ShowError("Vulkan Init Error", e.what());
        XCloseDisplay(display);
        return 1;
    }

    std::string svgContent;
    if (argc > 1) {
        std::string path(argv[1]);
        if (!path.empty() && path.front() == '"') path = path.substr(1);
        if (!path.empty() && path.back() == '"') path.pop_back();
        
        std::ifstream f(path);
        if (f.good()) {
            std::ostringstream ss; 
            ss << f.rdbuf();
            svgContent = ss.str();
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not open '%s', using fallback.", path.c_str());
            ShowError("Warning", msg);
            svgContent = IMAGE;
        }
    } else {
        svgContent = IMAGE;
    }

    try {
        g_renderer.loadSVGString(svgContent);
    } catch (std::exception& e) {
        ShowError("SVG Load Error", e.what());
        XCloseDisplay(display);
        return 1;
    }

    XMapWindow(display, hwnd);
    XFlush(display);

    XEvent ev;
    while (g_running) {
        while (XPending(display)) {
            XNextEvent(display, &ev);

            switch (ev.type) {
                case ClientMessage:
                    if (ev.xclient.data.l[0] == wmDeleteMessage) {
                        g_running = false;
                    }
                    break;

                case ConfigureNotify: {
                    int w = ev.xconfigure.width;
                    int h = ev.xconfigure.height;
                    if (w > 0 && h > 0 && (w != g_width || h != g_height)) {
                        g_width = w; 
                        g_height = h;
                        g_renderer.resize(w, h);
                    }
                    break;
                }

                case KeyPress: {
                    KeySym keysym = XLookupKeysym(&ev.xkey, 0);
                    if (keysym == XK_Escape) {
                        g_running = false;
                    }
                    break;
                }
            }
        }

        if (!g_running) break;

        g_renderer.render(0.f, 0.f, 0.f);
        g_renderer.present(true);
    }

    XDestroyWindow(display, hwnd);
    XCloseDisplay(display);

    return 0;
}
#endif