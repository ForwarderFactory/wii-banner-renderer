#include "Renderer.h"
#include "WrapGx.h"
#include <GL/glew.h>
#include <GL/gl.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#define GL_WIN
#else
#define GL_LINUX
#endif

#ifdef GL_LINUX
#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#include <algorithm>
#include <stdexcept>
#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

Renderer::Renderer(int width, int height)
    :
    m_width(width),
    m_height(height),
    m_pixels(width * height * 4)
{
    this->CreateContext();

    GX_Init(0, 0);
    glViewport(0, 0, width, height);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1000.0, 1000.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
}


Renderer::~Renderer()
{
    DestroyContext();
}

#ifdef GL_WIN
void Renderer::CreateContext()
{
	WNDCLASS wc = {};
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = DefWindowProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.lpszClassName = "kek";


	RegisterClass(&wc);

	HWND hwnd = CreateWindow(
	    wc.lpszClassName,
	    "kek",
	    WS_OVERLAPPEDWINDOW,
	    0, 0, m_width, m_height,
	    nullptr,
	    nullptr,
	    wc.hInstance,
	    nullptr
	);

	if (!hwnd)
	    throw std::runtime_error("CreateWindow failed");

	HDC hdc = GetDC(hwnd);

	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;
	pfd.iLayerType = PFD_MAIN_PLANE;

	int pixelFormat = ChoosePixelFormat(hdc, &pfd);
	if (pixelFormat == 0)
	    throw std::runtime_error("ChoosePixelFormat failed");

	if (!SetPixelFormat(hdc, pixelFormat, &pfd))
	    throw std::runtime_error("SetPixelFormat failed");

	HGLRC context = wglCreateContext(hdc);
	if (!context)
	    throw std::runtime_error("wglCreateContext failed");

	if (!wglMakeCurrent(hdc, context))
	    throw std::runtime_error("wglMakeCurrent failed");

	GLenum err = glewInit();
	if (err != GLEW_OK)
	{
	    throw std::runtime_error(
		reinterpret_cast<const char*>(glewGetErrorString(err))
	    );
	}


	m_hwnd = hwnd;
	m_hdc = hdc;
	m_context = context;
}

void Renderer::DestroyContext()
{
	if (!m_context)
		return;

	wglMakeCurrent(nullptr, nullptr);

	wglDeleteContext(static_cast<HGLRC>(m_context));

	ReleaseDC(static_cast<HWND>(m_hwnd), static_cast<HDC>(m_hdc));
	DestroyWindow(static_cast<HWND>(m_hwnd));

	m_context = nullptr;
	m_hdc = nullptr;
	m_hwnd = nullptr;
}
#else
EGLDisplay CreateEGLDisplay()
{
    PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT =
        (PFNEGLQUERYDEVICESEXTPROC)
        eglGetProcAddress("eglQueryDevicesEXT");

    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)
        eglGetProcAddress("eglGetPlatformDisplayEXT");

    if (eglQueryDevicesEXT && eglGetPlatformDisplayEXT)
    {
        EGLDeviceEXT devices[8];
        EGLint numDevices = 0;

        if (eglQueryDevicesEXT(8, devices, &numDevices) &&
            numDevices > 0)
        {
            return eglGetPlatformDisplayEXT(
                EGL_PLATFORM_DEVICE_EXT,
                devices[0],
                nullptr
            );
        }
    }

    return eglGetPlatformDisplay(
        EGL_PLATFORM_SURFACELESS_MESA,
        EGL_DEFAULT_DISPLAY,
        nullptr
    );
}
void Renderer::CreateContext()
{
    EGLDisplay display = CreateEGLDisplay();

    if (display == EGL_NO_DISPLAY)
        throw std::runtime_error("eglGetDisplay failed");

    if (!eglInitialize(display, nullptr, nullptr))
        throw std::runtime_error("eglInitialize failed");

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs))
        throw std::runtime_error("eglChooseConfig failed");

    if (!eglBindAPI(EGL_OPENGL_API))
        throw std::runtime_error("eglBindAPI failed");

    EGLint pbufferAttribs[] = {
        EGL_WIDTH, m_width,
        EGL_HEIGHT, m_height,
        EGL_NONE,
    };

    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
    if (surface == EGL_NO_SURFACE)
        throw std::runtime_error("eglCreatePbufferSurface failed");

    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, nullptr);
    if (context == EGL_NO_CONTEXT)
        throw std::runtime_error("eglCreateContext failed");

    if (!eglMakeCurrent(display, surface, surface, context))
        throw std::runtime_error("eglMakeCurrent failed");

    //GLenum err = glewInit();
    //if (err != GLEW_OK)
    //    throw std::runtime_error((const char*)glewGetErrorString(err));

    /*
    printf("Vendor: %s\n", glGetString(GL_VENDOR));
    printf("Renderer: %s\n", glGetString(GL_RENDERER));
    printf("Version: %s\n", glGetString(GL_VERSION));
    */

    m_display = display;
    m_surface = surface;
    m_context = context;
}

void Renderer::DestroyContext()
{
    if (!m_context)
        return;

    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    eglDestroyContext(m_display, (EGLContext)m_context);
    eglDestroySurface(m_display, (EGLSurface)m_surface);
    eglTerminate(m_display);

    m_context = nullptr;
    m_surface = nullptr;
    m_display = nullptr;
}
#endif


void Renderer::BeginFrame()
{
    glClearColor(0, 0, 0, 1);

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT |
        GL_STENCIL_BUFFER_BIT
    );
}


void Renderer::EndFrame()
{
    glFinish();

    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glReadPixels(
        0,
        0,
        m_width,
        m_height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        m_pixels.data()
    );
}

bool Renderer::SavePNG(const std::string& path, int width, int height)
{
    const int channels = 4;
    const int rowSize = width * channels;

    std::vector<unsigned char> flipped(m_pixels.size());

    for (int y = 0; y < height; y++)
    {
        memcpy(
            flipped.data() + y * rowSize,
            m_pixels.data() + (height - 1 - y) * rowSize,
            rowSize
        );
    }


    stbi_flip_vertically_on_write(true);
    stbi_write_png(
        path.c_str(),
        width,
        height,
        channels,
        flipped.data(), 
        width * channels
    );

    return true;
}

void Renderer::ReadPixelsTo(FILE* output) {
    const int channels = 4;
    const int rowSize = m_width * channels;

    fwrite(
        m_pixels.data(),
        1,
        m_pixels.size(),
        output
    );
}
