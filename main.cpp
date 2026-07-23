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
    #include <X11/keysym.h>
    #include <iostream>
#endif

#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include "renderer.h"

static const char* IMAGE = R"SVG(
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2009.9 997.63">
<g style="isolation:isolate">
    <path d="M1533.17 997.15c-101.92-8.27-193.85-73.53-335.16-237.9-42.61-49.56-82.5-99.2-167.55-208.5C901.1 384.51 851.87 324 793.92 260.25c-21.35-23.5-72-74.13-88.95-88.94C650.75 124 607.85 97.68 567 86.67c-18.14-4.89-25.8-5.83-47-5.79-16.57 0-21.73.41-30.09 2.19-37.17 7.93-69.15 24.1-106.41 53.81-16.31 13-51.55 47.47-68.89 67.37-58.42 67.06-117.07 154.93-205.59 308-29.91 51.72-32.6 56.22-36.76 61.5-5.16 6.56-9.39 10-16.46 13.28-5.21 2.44-6.92 2.72-16.8 2.72s-11.62-.29-16.5-2.6a42 42 0 0 1-19.22-18.7C.16 562.18 0 561.16 0 551.27c0-9.16.36-11.24 2.6-16 5.1-11 68.73-120 98.65-169 91.64-150 165.59-242.1 240.16-298.91Q464.54-26.47 593.16 10.4c98.06 28.11 201.29 117 356.88 307.35 31.67 38.74 63.62 79.21 135.41 171.5 109.45 140.7 162.17 205.29 215.88 264.5 18.08 19.93 65.78 67.63 81.14 81.13 50.59 44.48 91.83 68.82 133 78.52 13 3.05 45.83 3.35 59 .53 75.26-16.07 149.61-91.06 239.16-241.18 27.65-46.34 60.43-106.59 109.88-201.91 8.66-16.7 12.64-23.3 16.75-27.73 15.94-17.22 42.26-17.78 58.56-1.25 10.54 10.7 13.66 24.63 8.92 39.89-4.92 15.84-81.09 158.91-118.47 222.53-92.07 156.72-171.57 241.58-258.8 276.29-31.74 12.63-68.6 18.91-97.3 16.58" style="fill:#7aa8ff"/>
</g>
</svg>
)SVG";

static VulkanSVGRenderer g_renderer;
static int g_width = 800;
static int g_height = 600;
static bool g_running = true;

#ifdef _WIN32
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE: {
        int w = LOWORD(lp), h = HIWORD(lp);
        if (w > 0 && h > 0 && (w != g_width || h != g_height)) {
            g_width = w;
            g_height = h;
            g_renderer.resize(w, h);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            g_running = false;
            PostQuitMessage(0);
        }
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
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VulkanSVGRenderer";
    RegisterClassExW(&wc);

    RECT wr = { 0, 0, g_width, g_height };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(
        0,
        L"VulkanSVGRenderer",
        L"Vulkan SVG Renderer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        wr.right - wr.left,
        wr.bottom - wr.top,
        nullptr,
        nullptr,
        hInst,
        nullptr
    );

    if (!hwnd) {
        MessageBoxW(nullptr, L"CreateWindow failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    try {
        if (!g_renderer.init(hwnd, g_width, g_height)) {
            MessageBoxW(nullptr, L"Renderer init failed", L"Error", MB_OK | MB_ICONERROR);
            return 1;
        }
    } catch (std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Vulkan Init Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::string svgContent;
    if (lpCmd && lpCmd[0]) {
        std::string path(lpCmd);
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
            MessageBoxA(nullptr, msg, "Warning", MB_OK | MB_ICONWARNING);
            svgContent = IMAGE;
        }
    } else {
        svgContent = IMAGE;
    }

    try {
        g_renderer.loadSVGString(svgContent);
    } catch (std::exception& e) {
        MessageBoxA(nullptr, e.what(), "SVG Load Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!g_running) break;

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
        display,
        root,
        0, 0,
        g_width,
        g_height,
        1,
        BlackPixel(display, screen),
        WhitePixel(display, screen)
    );

    if (!hwnd) {
        ShowError("Error", "CreateWindow failed");
        XCloseDisplay(display);
        return 1;
    }

    XStoreName(display, hwnd, "Vulkan SVG Renderer");
    XSelectInput(display, hwnd, ExposureMask | KeyPressMask | StructureNotifyMask);

    Atom wmDeleteMessage = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, hwnd, &wmDeleteMessage, 1);

    try {
        if (!g_renderer.init(display, hwnd, g_width, g_height)) {
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