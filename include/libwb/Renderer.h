#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#endif

struct Rect
{
    int x;
    int y;
    int width;
    int height;
};

struct Point {
    double x;
    double y;
};

constexpr Rect GetCrop(const std::array<Point, 4>& pts) {
    double min_x = pts[0].x;
    double max_x = pts[0].x;
    double min_y = pts[0].y;
    double max_y = pts[0].y;

    for (const auto& p : pts) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }

    int width = static_cast<int>(max_x - min_x);
    int height = static_cast<int>(max_y - min_y);

    width &= ~1;
    height &= ~1;

    return {
        static_cast<int>(min_x),
        static_cast<int>(min_y),
        width,
        height
    };
}

class Renderer
{
public:
    Renderer(int width, int height);
    ~Renderer();

    void BeginFrame();
    void EndFrame();
    bool SavePNG(const std::string& path, int width, int height);
    void ReadPixelsTo(FILE* output);
    void ReadPixelsTo(FILE *output, Rect crop) const;

    [[nodiscard]] const uint8_t* Pixels() const
    {
        return m_pixels.data();
    }

    [[nodiscard]] int Width() const
    {
        return m_width;
    }

    [[nodiscard]] int Height() const
    {
        return m_height;
    }

private:
    void CreateContext();
    void DestroyContext();
#if defined(__APPLE__)
    void CreateFramebuffer();
    void DestroyFramebuffer();

    uint32_t m_framebuffer = 0;
    uint32_t m_depth_buffer = 0;
    uint32_t m_color_texture = 0;
#endif

    int m_width;
    int m_height;

    std::vector<uint8_t> m_pixels;

    void* m_display = nullptr;
    void* m_surface = nullptr;
    void* m_context = nullptr;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
    HWND m_hwnd = nullptr;
    HDC m_hdc = nullptr;
    HGLRC m_glrc = nullptr;
#endif
};
