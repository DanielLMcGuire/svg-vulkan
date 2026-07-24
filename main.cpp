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
#include <cctype>
#include <vector>
#include "renderer.h"

static const char* IMAGE = R"SVG(
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 100 100">
 <title>SVG logo</title>
    <rect width="100" height="100" fill="#FF9900" rx="4" ry="4"/>
    <rect width="50" height="50" fill="#FFB13B" rx="4" ry="4"/>
    <rect width="50" height="50" x="50" y="50" fill="#de8500" rx="4" ry="4"/>
    <g fill="#ff9900">
      <circle cx="50" cy="18.4" r="18.4"/>
      <circle cx="72.4" cy="27.6" r="18.4"/>
      <circle cx="81.6" cy="50" r="18.4"/>
      <circle cx="72.4" cy="72.4" r="18.4"/>
      <circle cx="50" cy="81.6" r="18.4"/>
      <circle cx="27.6" cy="72.4" r="18.4"/>
      <circle cx="18.4" cy="50" r="18.4"/>
      <circle cx="27.6" cy="27.6" r="18.4"/>
    </g>
    <path d="M63.086 18.385c0-7.227-5.859-13.086-13.1-13.086-7.235 0-13.096 5.859-13.096 13.086-5.1-5.11-13.395-5.11-18.497 0-5.119 5.12-5.119 13.408 0 18.524-7.234 0-13.103 5.859-13.103 13.085 0 7.23 5.87 13.098 13.103 13.098-5.119 5.11-5.119 13.395 0 18.515 5.102 5.104 13.397 5.104 18.497 0 0 7.228 5.86 13.083 13.096 13.083 7.24 0 13.1-5.855 13.1-13.083 5.118 5.104 13.416 5.104 18.513 0 5.101-5.12 5.101-13.41 0-18.515 7.216 0 13.081-5.869 13.081-13.098 0-7.227-5.865-13.085-13.081-13.085 5.101-5.119 5.101-13.406 0-18.524-5.097-5.11-13.393-5.11-18.513 0z"/>
    <path fill="#ffffff" d="M55.003 23.405v14.488L65.26 27.64c0-1.812.691-3.618 2.066-5.005 2.78-2.771 7.275-2.771 10.024 0 2.771 2.766 2.771 7.255 0 10.027-1.377 1.375-3.195 2.072-5.015 2.072L62.101 44.982H76.59c1.29-1.28 3.054-2.076 5.011-2.076 3.9 0 7.078 3.179 7.078 7.087 0 3.906-3.178 7.088-7.078 7.088-1.957 0-3.721-.798-5.011-2.072H62.1l10.229 10.244c1.824 0 3.642.694 5.015 2.086 2.774 2.759 2.774 7.25 0 10.01-2.75 2.774-7.239 2.774-10.025 0-1.372-1.372-2.064-3.192-2.064-5.003L55 62.094v14.499c1.271 1.276 2.084 3.054 2.084 5.013 0 3.906-3.177 7.077-7.098 7.077-3.919 0-7.094-3.167-7.094-7.077 0-1.959.811-3.732 2.081-5.013V62.094L34.738 72.346c0 1.812-.705 3.627-2.084 5.003-2.769 2.772-7.251 2.772-10.024 0-2.775-2.764-2.775-7.253 0-10.012 1.377-1.39 3.214-2.086 5.012-2.086l10.257-10.242H23.414c-1.289 1.276-3.072 2.072-5.015 2.072-3.917 0-7.096-3.18-7.096-7.088s3.177-7.087 7.096-7.087c1.94 0 3.725.796 5.015 2.076h14.488L27.646 34.736c-1.797 0-3.632-.697-5.012-2.071-2.775-2.772-2.775-7.26 0-10.027 2.773-2.771 7.256-2.771 10.027 0 1.375 1.386 2.083 3.195 2.083 5.005l10.235 10.252V23.407c-1.27-1.287-2.082-3.053-2.082-5.023 0-3.908 3.175-7.079 7.096-7.079 3.919 0 7.097 3.168 7.097 7.079-.002 1.972-.816 3.735-2.087 5.021z"/>
    <g>
      <path fill="#000000" d="M5.3 50h89.38v40q0 5-5 5H10.3q-5 0-5-5Z"/>
      <path fill="#3f3f3f" d="M14.657 54.211h71.394c2.908 0 5.312 2.385 5.312 5.315v17.91c-27.584-3.403-54.926-8.125-82.011-7.683V59.526c.001-2.93 2.391-5.315 5.305-5.315z"/>
      <path fill="#ffffff" stroke="#000000" stroke-width=".5035" d="M18.312 72.927c-2.103-2.107-3.407-5.028-3.407-8.253 0-6.445 5.223-11.672 11.666-11.672 6.446 0 11.667 5.225 11.667 11.672h-6.832c0-2.674-2.168-4.837-4.835-4.837-2.663 0-4.838 2.163-4.838 4.837 0 1.338.549 2.536 1.415 3.42.883.874 2.101 1.405 3.423 1.405v.012c3.232 0 6.145 1.309 8.243 3.416 2.118 2.111 3.424 5.034 3.424 8.248 0 6.454-5.221 11.68-11.667 11.68-6.442 0-11.666-5.222-11.666-11.68h6.828c0 2.679 2.175 4.835 4.838 4.835 2.667 0 4.835-2.156 4.835-4.835 0-1.329-.545-2.527-1.429-3.407-.864-.88-2.082-1.418-3.406-1.418-3.23 0-6.142-1.314-8.259-3.423zM61.588 53.005l-8.244 39.849h-6.85l-8.258-39.849h6.846l4.838 23.337 4.835-23.337zM73.255 69.513h11.683v11.664c0 6.452-5.226 11.678-11.669 11.678-6.441 0-11.666-5.226-11.666-11.678V64.676h-.017C61.586 58.229 66.827 53 73.253 53c6.459 0 11.683 5.225 11.683 11.676h-6.849c0-2.674-2.152-4.837-4.834-4.837-2.647 0-4.82 2.163-4.82 4.837v16.501c0 2.675 2.173 4.837 4.82 4.837 2.682 0 4.834-2.162 4.834-4.827V76.348h-4.834l.002-6.835z"/>
    </g>
</svg>
)SVG";

struct CliOptions {
    std::string svgPath;
    std::string ppmPath;
    int         rasterWidth  = 800;
    int         rasterHeight = 600;
    bool        wantRaster   = false;
};

static std::vector<std::string> splitCommandLine(const std::string& cmd) {
    std::vector<std::string> tokens;
    std::string cur;
    bool inQuotes = false;
    for (char c : cmd) {
        if (c == '"') { inQuotes = !inQuotes; continue; }
        if (!inQuotes && std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) tokens.push_back(cur);
    return tokens;
}

static bool parseSizeArg(const std::string& s, int& outW, int& outH) {
    size_t xpos = s.find_first_of("xX");
    if (xpos == std::string::npos || xpos == 0 || xpos + 1 >= s.size()) return false;
    try {
        int w = std::stoi(s.substr(0, xpos));
        int h = std::stoi(s.substr(xpos + 1));
        if (w <= 0 || h <= 0) return false;
        outW = w; outH = h;
        return true;
    } catch (...) {
        return false;
    }
}

static CliOptions parseArgs(const std::vector<std::string>& tokens) {
    CliOptions opts;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& tok = tokens[i];
        if (tok == "--ppm" && i + 1 < tokens.size()) {
            opts.ppmPath    = tokens[++i];
            opts.wantRaster = true;
        } else if (tok.rfind("--ppm=", 0) == 0) {
            opts.ppmPath    = tok.substr(6);
            opts.wantRaster = true;
        } else if (tok == "--size" && i + 1 < tokens.size()) {
            int w, h;
            if (parseSizeArg(tokens[++i], w, h)) { opts.rasterWidth = w; opts.rasterHeight = h; }
        } else if (tok.rfind("--size=", 0) == 0) {
            int w, h;
            if (parseSizeArg(tok.substr(7), w, h)) { opts.rasterWidth = w; opts.rasterHeight = h; }
        } else if (opts.svgPath.empty() && tok.rfind("--", 0) != 0) {
            opts.svgPath = tok;
        }
    }
    return opts;
}

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

    CliOptions opts = parseArgs(splitCommandLine(lpCmd ? lpCmd : ""));
    if (opts.wantRaster) {
        g_width  = opts.rasterWidth;
        g_height = opts.rasterHeight;
    }

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
    if (!opts.svgPath.empty()) {
        std::ifstream f(opts.svgPath);
        if (f.good()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            svgContent = ss.str();
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not open '%s', using fallback.", opts.svgPath.c_str());
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

    if (opts.wantRaster) {
        g_renderer.render(1.f, 1.f, 1.f);
        bool ok = g_renderer.saveFrameToPPM(opts.ppmPath);
        if (!ok) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Failed to write PPM to '%s'.", opts.ppmPath.c_str());
            MessageBoxA(nullptr, msg, "PPM Export Error", MB_OK | MB_ICONERROR);
            DestroyWindow(hwnd);
            return 1;
        }
        char msg[512];
        snprintf(msg, sizeof(msg), "Saved %dx%d PPM raster to '%s'.",
                 g_width, g_height, opts.ppmPath.c_str());
        MessageBoxA(nullptr, msg, "PPM Export", MB_OK | MB_ICONINFORMATION);
        DestroyWindow(hwnd);
        return 0;
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

    CliOptions opts = parseArgs(std::vector<std::string>(argv + 1, argv + argc));
    if (opts.wantRaster) {
        g_width  = opts.rasterWidth;
        g_height = opts.rasterHeight;
    }

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
    if (!opts.svgPath.empty()) {
        std::ifstream f(opts.svgPath);
        if (f.good()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            svgContent = ss.str();
        } else {
            char msg[512];
            snprintf(msg, sizeof(msg), "Could not open '%s', using fallback.", opts.svgPath.c_str());
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

    if (opts.wantRaster) {
        g_renderer.render(1.f, 1.f, 1.f);
        bool ok = g_renderer.saveFrameToPPM(opts.ppmPath);
        XDestroyWindow(display, hwnd);
        XCloseDisplay(display);
        if (!ok) {
            fprintf(stderr, "Failed to write PPM to '%s'.\n", opts.ppmPath.c_str());
            return 1;
        }
        printf("Saved %dx%d PPM raster to '%s'.\n", g_width, g_height, opts.ppmPath.c_str());
        return 0;
    }

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