/*
Copyright (c) 2026 - Jacob Nilsson

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3_mixer/SDL_mixer.h>

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <deque>

#include "../../include/libwb/Banner.h"
#include "../../include/libwb/Renderer.h"
#include "../../include/libwb/Wad.h"

// hack, idk why this is needed
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

constexpr int FULL_CANVAS_WIDTH = 1920;
constexpr int FULL_CANVAS_HEIGHT = 1080;
constexpr int NO_CHANNEL_INDEX = -1;

constexpr std::array<Point, 4> g_banner_points = {{
    {1060, 20}, {1060, 403}, {1813, 20}, {1813, 403}
}};

constexpr std::array<Point, 4> g_icon_points = {{
    {1060, 0}, {1060, 520}, {1833, 0}, {1833, 520}
}};

static int upload_texture(const std::vector<unsigned char>& rgba, int w, int h) {
    if (w <= 0 || h <= 0 || rgba.empty()) return 0;

    int tex = 0; /* again should this be glint */

    glGenTextures(1, reinterpret_cast<GLuint *>(&tex));
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    return tex;
}

static void update_texture(int tex, const std::vector<unsigned char>& rgba, int w, int h) {
    if (!tex || rgba.empty())
        return;

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void draw_textured_quad(int tex, float x, float y, float w, float h) {
    if (!tex)
        return;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);

    glTexCoord2f(0, 0.f); glVertex2f(x,     y);
    glTexCoord2f(1, 0.f); glVertex2f(x + w, y);
    glTexCoord2f(1, 1.f); glVertex2f(x + w, y + h);
    glTexCoord2f(0, 1.f); glVertex2f(x,     y + h);

    glEnd();

    // is this even needed
    glDisable(GL_TEXTURE_2D);
}

static void draw_rect_outline(float x, float y, float w, float h, float r, float g, float b) {
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);

    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);

    glEnd();
}

static void fit_rect(float box_w, float box_h, float content_aspect, float& out_w, float& out_h) {
    out_w = box_w;
    out_h = out_w / content_aspect;

    if (out_h > box_h) {
        out_h = box_h;
        out_w = out_h * content_aspect;
    }
}

struct IconProcess {
    WiiBanner::Layout* layout = nullptr;
    int texture = 0; // should we use GLuint? seems useless on modern computers

    void destroy_texture() {
        if (texture) { glDeleteTextures(1, reinterpret_cast<GLuint *>(&texture)); texture = 0; }
    }
};

struct Banner {
    std::filesystem::path app_path;
    std::filesystem::path wad_extract_dir;
    std::string label;

    WiiBanner::Banner* banner = nullptr;
    IconProcess icon;

    void destroy() {
        icon.destroy_texture();
        delete banner;
        banner = nullptr;
    }
};

static bool resolve_channel_path(const std::filesystem::path& input, std::filesystem::path& out_app_path, std::filesystem::path& out_extract_dir) {
    if (input.extension() == ".wad") {
        std::ifstream in(input, std::ios::binary);
        if (!in) return false;

        out_extract_dir = std::filesystem::temp_directory_path() / ("wbr-menu-" + input.stem().string());
        Wad::extract_wad(in, out_extract_dir.string());

        for (const auto& entry : std::filesystem::directory_iterator(out_extract_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".app")
                continue;
            if (WiiBanner::Banner::is_valid(entry.path().string())) {
                out_app_path = entry.path();
                return true;
            }
        }
        return false;
    }

    if (WiiBanner::Banner::is_valid(input.string())) {
        out_app_path = input;
        out_extract_dir.clear();
        return true;
    }
    return false;
}

bool try_load_channel(const std::filesystem::path& input, const std::filesystem::path& font_archive, Banner& out) {
    Banner ch;
    try {
        if (!resolve_channel_path(input, ch.app_path, ch.wad_extract_dir)) {
            return false;
        }
    } catch (...) {
        return false;
    }
    ch.label = input.stem().string();

    try {
        ch.banner = new WiiBanner::Banner(ch.app_path.string(), font_archive.string());
        ch.banner->LoadIcon();
        ch.icon.layout = ch.banner->GetIcon();
        if (!ch.icon.layout) {
            delete ch.banner;
            return false;
        }

        ch.icon.layout->SetLanguage("ENG"); // TODO make this an option?
    } catch (const std::exception&) {
        delete ch.banner;
        return false;
    }

    out = std::move(ch);

    return true;
}

static std::deque<Banner> scan_channels(const std::filesystem::path& dir, const std::filesystem::path& font_archive) {
    std::deque<Banner> channels;
    if (!std::filesystem::is_directory(dir)) {
        std::cerr << dir << " is not a directory\n";
        return channels;
    }

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension();
        if (ext != ".app" && ext != ".bnr" && ext != ".wad")
            continue;

        Banner ch;

        if (try_load_channel(entry.path(), font_archive, ch))
            channels.push_back(std::move(ch));
    }

    return channels;
}

struct Render {
    IconProcess* visual = nullptr;
    Rect crop{};
    bool advance = false;
};

struct RenderHandle {
    IconProcess* visual = nullptr;
    std::vector<unsigned char> pixels;
    int width = 0, height = 0;
};

struct RenderQueue {
    std::mutex mtx;
    std::condition_variable cv;
    std::unordered_map<IconProcess*, Render> pending;
    std::vector<RenderHandle> ready;
    std::atomic<IconProcess*> in_flight{nullptr};
    std::atomic<bool> stop{false};

    void submit(const Render& render) {
        std::lock_guard<std::mutex> lk(mtx);
        pending[render.visual] = render;
        cv.notify_one();
    }

    void cancel_and_wait(IconProcess* visual) {
        {
            std::lock_guard<std::mutex> lk(mtx);
            pending.erase(visual);
        }
        while (in_flight.load() == visual)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::vector<RenderHandle> drain_ready() {
        std::lock_guard<std::mutex> lk(mtx);
        std::vector<RenderHandle> out;
        out.swap(ready);
        return out;
    }
};

static void render_worker(RenderQueue& queue) {
    Renderer canvas_renderer(FULL_CANVAS_WIDTH, FULL_CANVAS_HEIGHT);

    std::unique_lock<std::mutex> lk(queue.mtx);
    while (!queue.stop.load()) {
        if (queue.pending.empty()) {
            queue.cv.wait_for(lk, std::chrono::milliseconds(50));
            continue;
        }

        auto it = queue.pending.begin();

        Render render = it->second;
        queue.pending.erase(it);

        lk.unlock();

        queue.in_flight.store(render.visual);

        if (render.visual && render.visual->layout) {
            canvas_renderer.BeginFrame();
            render.visual->layout->Render(1.0f, 0xff, true);
            canvas_renderer.EndFrame();

            RenderHandle outcome;
            outcome.visual = render.visual;
            outcome.pixels = canvas_renderer.GetFrameData(render.crop);
            outcome.width = render.crop.width;
            outcome.height = render.crop.height;

            if (render.advance) render.visual->layout->AdvanceFrame();

            {
                std::lock_guard<std::mutex> rlk(queue.mtx);
                queue.ready.push_back(std::move(outcome));
            }
        }

        queue.in_flight.store(nullptr);

        lk.lock();
    }
}

static std::once_flag flag;
static std::filesystem::path get_temp_dir() {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "wbr";

    std::call_once(flag, [&dir]() {
       if (!std::filesystem::is_directory(dir)) {
           std::filesystem::create_directories(dir);
       }

        if (!std::filesystem::is_directory(dir)) {
            throw std::runtime_error{"failed to create temp directory"};
        }
    });

    return dir;
}

struct PendingAdds {
    std::mutex mtx;
    std::vector<std::string> paths;
};

static void SDLCALL on_files_selected(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* pending = static_cast<PendingAdds*>(userdata);

    if (!filelist) {
        std::cerr << "File dialog error: " << SDL_GetError() << "\n";
        return;
    }

    std::lock_guard<std::mutex> lk(pending->mtx);

    for (int i = 0; filelist[i] != nullptr; ++i)
        pending->paths.emplace_back(filelist[i]);
}

int main(int argc, char** argv) {
    std::filesystem::path channel_dir{};
    std::filesystem::path font_archive{};

    if (argc >= 2) {
        channel_dir = argv[1];
        font_archive = argc > 2 ? argv[2] : std::filesystem::path{};
    } else {
        channel_dir = std::filesystem::current_path();
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return EXIT_FAILURE;
    }

    /* idk if we need to backwards compat this far? */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int win_w = 1280;
    int win_h = 720;

    SDL_Window* window = SDL_CreateWindow("Wii Banner Player", win_w, win_h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    if (!gl_context) {
        std::cerr << "SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_RaiseWindow(window);
    SDL_GL_SetSwapInterval(0);

    MIX_Mixer* mixer = nullptr;

    if (!MIX_Init()) {
        std::cerr << "MIX_Init failed: " << SDL_GetError() << " (continuing without audio)\n";
    } else {
        SDL_AudioSpec spec{};
        spec.freq = 48000;
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        if (!mixer)
            std::cerr << "MIX_CreateMixerDevice failed: " << SDL_GetError() << " (continuing without audio)\n";
    }

    std::deque<Banner> channels = scan_channels(channel_dir, font_archive);

    if (channels.empty())
        std::cerr << "No usable channels -- the grid will be empty. Check the skip reasons above.\n";

    Rect icon_crop = GetCrop(g_icon_points);
    Rect banner_crop = GetCrop(g_banner_points);

    RenderQueue queue;
    std::thread renderer_thread(render_worker, std::ref(queue));

    PendingAdds pending_adds;

    int scroll_y = 0;
    int selected_index = 0;

    enum class ViewMode { GRID, BANNER } mode = ViewMode::GRID;

    IconProcess active_banner_visual;
    MIX_Audio* active_audio = nullptr;
    MIX_Track* active_audio_track = nullptr;
    std::filesystem::path active_temp_wav;
    int active_channel_index = NO_CHANNEL_INDEX;

    /* i hate this? */
    auto open_add_files_dialog = [&]() {
        static constexpr SDL_DialogFileFilter filters[] = {
            {"Wii banners", "app;bnr;wad"},
            {"All files", "*"},
        };

        SDL_ShowOpenFileDialog(on_files_selected, &pending_adds, window, filters, 2, nullptr, true);
    };

    auto stop_audio = [&]() {
        if (active_audio_track) { MIX_StopTrack(active_audio_track, 0); MIX_DestroyTrack(active_audio_track); active_audio_track = nullptr; }
        if (active_audio) { MIX_DestroyAudio(active_audio); active_audio = nullptr; }
    };

    auto launch_banner = [&](int index) {
        Banner& ch = channels[index];

        try {
            ch.banner->LoadBanner();

            WiiBanner::Layout* layout = ch.banner->GetBanner();

            if (!layout)
                throw std::runtime_error("no banner layout");

            layout->SetLanguage("ENG"); /* TODO make this customizable */
            layout->SetFrame(0);

            active_banner_visual.destroy_texture();
            active_banner_visual.layout = layout;

            active_channel_index = index;

            ch.banner->LoadSound();

            if (mixer && ch.banner->GetSound()) {
                active_temp_wav = get_temp_dir() / ("wbr-menu-play-" + ch.label + ".wav");

                if (!std::filesystem::is_regular_file(active_temp_wav))
                    ch.banner->GetSound()->WriteWAV(active_temp_wav.string());

                active_audio = MIX_LoadAudio(mixer, active_temp_wav.string().c_str(), true);

                if (!active_audio) {
                    std::cerr << "MIX_LoadAudio failed: " << SDL_GetError() << "\n";
                } else {
                    active_audio_track = MIX_CreateTrack(mixer);
                    if (!active_audio_track || !MIX_SetTrackAudio(active_audio_track, active_audio)) {
                        std::cerr << "MIX_CreateTrack/SetTrackAudio failed: " << SDL_GetError() << "\n";
                        if (active_audio_track) { MIX_DestroyTrack(active_audio_track); active_audio_track = nullptr; }
                        MIX_DestroyAudio(active_audio);
                        active_audio = nullptr;
                    } else if (!MIX_PlayTrack(active_audio_track, 0)) {
                        std::cerr << "MIX_PlayTrack failed: " << SDL_GetError() << "\n";
                    }
                }
            }

            mode = ViewMode::BANNER;
        } catch (const std::exception&) {
            stop_audio();

            queue.cancel_and_wait(&active_banner_visual);

            active_banner_visual.destroy_texture();
            active_banner_visual.layout = nullptr;
        }
    };

    auto return_to_grid = [&]() {
        stop_audio();

        queue.cancel_and_wait(&active_banner_visual);

        active_banner_visual.destroy_texture();
        active_banner_visual.layout = nullptr;

        if (active_channel_index >= 0)
            channels[active_channel_index].banner->UnloadBanner();

        active_channel_index = NO_CHANNEL_INDEX;
        mode = ViewMode::GRID;
    };

    auto switch_banner = [&](int index) {
        if (channels.empty())
            return;

        index = ((index % static_cast<int>(channels.size())) + static_cast<int>(channels.size()))
                % static_cast<int>(channels.size());

        if (index == active_channel_index)
            return;

        stop_audio();
        queue.cancel_and_wait(&active_banner_visual);

        active_banner_visual.destroy_texture();
        active_banner_visual.layout = nullptr;

        if (active_channel_index >= 0)
            channels[active_channel_index].banner->UnloadBanner();

        active_channel_index = -1;

        launch_banner(index);
        selected_index = index;
    };

    uint64_t last_frame_ms = SDL_GetTicks();

    bool running = true;

    while (running) {
        constexpr int cell_padding = 20;
        constexpr int cell_size = 160;
        constexpr int frame_rate = 60;
        constexpr int extra_tiles = 1;

        int cols = std::max(1, win_w / (cell_size + cell_padding));
        int total_slots = static_cast<int>(channels.size()) + extra_tiles;

        auto ensure_selected_visible = [&]() {
            int row = selected_index / cols;
            float y = row * (cell_size + cell_padding) + cell_padding / 2.f - scroll_y;

            if (y < 0)
                scroll_y += static_cast<int>(y);
            else if (y + cell_size > win_h)
                scroll_y += static_cast<int>(y + cell_size - win_h);

            scroll_y = std::max(0, scroll_y);
        };

        auto activate_selected = [&]() {
            if (selected_index == static_cast<int>(channels.size())) // fine for now but might cause issues if we add more buttons
                open_add_files_dialog();
            else if (selected_index >= 0 && selected_index < static_cast<int>(channels.size()))
                switch_banner(selected_index);
        };

        SDL_Event e;

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                win_w = e.window.data1;
                win_h = e.window.data2;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_ESCAPE) {
                    if (mode == ViewMode::BANNER) return_to_grid();
                    else running = false;
                } else if (mode == ViewMode::GRID) {
                    if (e.key.key == SDLK_LEFT) {
                        selected_index = std::max(0, selected_index - 1);
                        ensure_selected_visible();
                    } else if (e.key.key == SDLK_RIGHT) {
                        selected_index = std::min(total_slots - 1, selected_index + 1);
                        ensure_selected_visible();
                    } else if (e.key.key == SDLK_UP) {
                        selected_index = std::max(0, selected_index - cols);
                        ensure_selected_visible();
                    } else if (e.key.key == SDLK_DOWN) {
                        selected_index = std::min(total_slots - 1, selected_index + cols);
                        ensure_selected_visible();
                    } else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) {
                        activate_selected();
                    } else if (e.key.key == SDLK_O) {
                        open_add_files_dialog();
                    }
                } else if (mode == ViewMode::BANNER) {
                    if (e.key.key == SDLK_LEFT) {
                        switch_banner(active_channel_index - 1);
                    } else if (e.key.key == SDLK_RIGHT) {
                        switch_banner(active_channel_index + 1);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (mode == ViewMode::GRID)
                    scroll_y = std::max(0, scroll_y - static_cast<int>(e.wheel.y * 40));
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button != SDL_BUTTON_LEFT) break;
                if (mode == ViewMode::GRID) {
                    int col = static_cast<int>(e.button.x) / (cell_size + cell_padding);
                    int row = (static_cast<int>(e.button.y) + scroll_y) / (cell_size + cell_padding);
                    int index = row * cols + col;
                    if (col < cols && index >= 0 && index < total_slots) {
                        selected_index = index;
                        activate_selected();
                    }
                } else {
                    return_to_grid();
                }
                break;
            default:
                break;
            }
        }

        {
            std::vector<std::string> new_paths;

            {
                std::lock_guard<std::mutex> lk(pending_adds.mtx);
                new_paths.swap(pending_adds.paths);
            }

            for (const auto& p : new_paths) {
                Banner ch;

                if (try_load_channel(std::filesystem::path(p), font_archive, ch))
                    channels.push_back(std::move(ch));
            }
        }

        if (mode == ViewMode::GRID) {
            for (size_t i = 0; i < channels.size(); ++i) {
                int row = static_cast<int>(i) / cols;

                float y = row * (cell_size + cell_padding) + cell_padding / 2.f - scroll_y;

                if (y + cell_size < 0 || y > win_h)
                    continue;


                queue.submit(Render{&channels[i].icon, icon_crop, /*advance=*/true});
            }
        } else if (active_banner_visual.layout) {
            queue.submit(Render{&active_banner_visual, banner_crop, /*advance=*/true});
        }

        for (RenderHandle& outcome : queue.drain_ready()) {
            IconProcess* v = outcome.visual;

            if (v->texture == 0) {
                v->texture = upload_texture(outcome.pixels, outcome.width, outcome.height);
                continue;
            }

            update_texture(v->texture, outcome.pixels, outcome.width, outcome.height);
        }

        glViewport(0, 0, win_w, win_h);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, win_w, win_h, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glClearColor(0.08f, 0.08f, 0.1f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (mode == ViewMode::GRID) {
            float icon_aspect = static_cast<float>(icon_crop.width) / static_cast<float>(icon_crop.height);
            for (int i = 0; i < total_slots; ++i) {
                int col = i % cols;
                int row = i / cols;

                float x = col * (cell_size + cell_padding) + cell_padding / 2.f;
                float y = row * (cell_size + cell_padding) + cell_padding / 2.f - scroll_y;

                if (y + cell_size < 0 || y > win_h)
                    continue;

                if (i < static_cast<int>(channels.size())) {
                    float iw, ih;

                    fit_rect(cell_size, cell_size, icon_aspect, iw, ih);

                    float ix = x + (cell_size - iw) / 2.f;
                    float iy = y + (cell_size - ih) / 2.f;

                    draw_textured_quad(channels[i].icon.texture, ix, iy, iw, ih);
                } else { /* draw plus button for adding more */
                    float cx = x + cell_size / 2.f, cy = y + cell_size / 2.f;
                    float arm = cell_size * 0.25f;
                    glColor3f(0.5f, 0.5f, 0.55f);
                    glBegin(GL_LINES);
                        glVertex2f(cx - arm, cy); glVertex2f(cx + arm, cy);
                        glVertex2f(cx, cy - arm); glVertex2f(cx, cy + arm);
                    glEnd();
                }

                if (i == selected_index)
                    draw_rect_outline(x, y, cell_size, cell_size, 1.f, 1.f, 1.f);
            }
        } else {
            float aspect = static_cast<float>(banner_crop.width) / static_cast<float>(banner_crop.height);
            float w = static_cast<float>(win_w), h = w / aspect;

            if (h > win_h) {
                h = static_cast<float>(win_h);
                w = h * aspect;
            }

            float x = (win_w - w) / 2.f, y = (win_h - h) / 2.f;

            draw_textured_quad(active_banner_visual.texture, x, y, w, h);
        }

        SDL_GL_SwapWindow(window);

        uint64_t now = SDL_GetTicks();
        uint64_t elapsed = now - last_frame_ms;
        
        constexpr uint64_t target = 1000 / frame_rate;
        
        if (elapsed < target)
            SDL_Delay(static_cast<Uint32>(target - elapsed));

        last_frame_ms = SDL_GetTicks();
    }

    queue.stop.store(true);
    queue.cv.notify_all();

    renderer_thread.join();

    stop_audio();
    active_banner_visual.destroy_texture();

    for (auto& ch : channels) {
        ch.destroy();

        if (!ch.wad_extract_dir.empty() && std::filesystem::exists(ch.wad_extract_dir))
            std::filesystem::remove_all(ch.wad_extract_dir);
    }

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (std::filesystem::is_directory(get_temp_dir())) {
        std::filesystem::remove_all(get_temp_dir());
    }

    return EXIT_SUCCESS;
}