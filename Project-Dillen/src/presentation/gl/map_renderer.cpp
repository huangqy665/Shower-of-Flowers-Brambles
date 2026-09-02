#include "map_renderer.hpp"

#include <cmath>
#include <cstring>
#include <vector>

#include <SDL3/SDL.h>

namespace dillen::presentation::gl {

namespace {

// The slice of OpenGL 3.3 core this renderer uses, declared here rather than
// pulled from a loader library.
//
// A generated loader would add a second vendored dependency for about thirty
// entry points. Declaring them is eighty lines, has no build story, and makes
// the surface this renderer actually depends on legible in one place.
using GLenum = unsigned int;
using GLbitfield = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;
using GLboolean = unsigned char;
using GLintptr = std::intptr_t;
using GLsizeiptr = std::intptr_t;

constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_DEPTH_BUFFER_BIT = 0x00000100;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_UNSIGNED_INT = 0x1405;
constexpr GLenum GL_UNSIGNED_SHORT = 0x1403;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_FALSE = 0;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE1 = 0x84C1;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_R16UI = 0x8234;
constexpr GLenum GL_RED_INTEGER = 0x8D94;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_COLOR_ATTACHMENT1 = 0x8CE1;
constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_NEAREST_FILTER_BLIT = 0x2600;

struct Api
{
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*Clear)(GLbitfield) = nullptr;
    void (*Enable)(GLenum) = nullptr;
    void (*Disable)(GLenum) = nullptr;
    void (*GenVertexArrays)(GLsizei, GLuint*) = nullptr;
    void (*BindVertexArray)(GLuint) = nullptr;
    void (*DeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
    void (*GenBuffers)(GLsizei, GLuint*) = nullptr;
    void (*BindBuffer)(GLenum, GLuint) = nullptr;
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
    void (*DeleteBuffers)(GLsizei, const GLuint*) = nullptr;
    void (*VertexAttribPointer)(
        GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;
    void (*EnableVertexAttribArray)(GLuint) = nullptr;
    GLuint (*CreateShader)(GLenum) = nullptr;
    void (*ShaderSource)(
        GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*CompileShader)(GLuint) = nullptr;
    void (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    GLuint (*CreateProgram)() = nullptr;
    void (*AttachShader)(GLuint, GLuint) = nullptr;
    void (*LinkProgram)(GLuint) = nullptr;
    void (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*UseProgram)(GLuint) = nullptr;
    void (*DeleteShader)(GLuint) = nullptr;
    void (*DeleteProgram)(GLuint) = nullptr;
    GLint (*GetUniformLocation)(GLuint, const GLchar*) = nullptr;
    void (*UniformMatrix4fv)(
        GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;
    void (*Uniform1i)(GLint, GLint) = nullptr;
    void (*Uniform2f)(GLint, GLfloat, GLfloat) = nullptr;
    void (*GenTextures)(GLsizei, GLuint*) = nullptr;
    void (*BindTexture)(GLenum, GLuint) = nullptr;
    void (*TexImage2D)(
        GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
        const void*) = nullptr;
    void (*TexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void (*ActiveTexture)(GLenum) = nullptr;
    void (*DeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (*DrawElements)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    void (*GenFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (*BindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*FramebufferTexture2D)(
        GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    void (*FramebufferRenderbuffer)(
        GLenum, GLenum, GLenum, GLuint) = nullptr;
    void (*DeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    GLenum (*CheckFramebufferStatus)(GLenum) = nullptr;
    void (*GenRenderbuffers)(GLsizei, GLuint*) = nullptr;
    void (*BindRenderbuffer)(GLenum, GLuint) = nullptr;
    void (*RenderbufferStorage)(
        GLenum, GLenum, GLsizei, GLsizei) = nullptr;
    void (*DeleteRenderbuffers)(GLsizei, const GLuint*) = nullptr;
    void (*DrawBuffers)(GLsizei, const GLenum*) = nullptr;
    void (*ReadBuffer)(GLenum) = nullptr;
    void (*ReadPixels)(
        GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*BlitFramebuffer)(
        GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint,
        GLbitfield, GLenum) = nullptr;
    void (*ClearBufferuiv)(GLenum, GLint, const GLuint*) = nullptr;
};

template <typename T>
bool Load(T& slot, const char* name)
{
    slot = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return slot != nullptr;
}

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexture;
uniform mat4 uViewProjection;
out vec2 vTexture;
void main()
{
    vTexture = aTexture;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

// Two fetches, no arithmetic over provinces.
//
// texelFetch on an integer texture, which cannot filter even by accident:
// interpolating two province indices produces a third index belonging to an
// unrelated province, and there is no filtering that is meaningful on identity
// data.
const char* kFragmentShader = R"(#version 330 core
in vec2 vTexture;
layout(location = 0) out vec4 oColour;
layout(location = 1) out uint oIndex;
uniform usampler2D uIndexTexture;
uniform sampler2D uPalette;
uniform vec2 uIndexSize;
void main()
{
    ivec2 texel = ivec2(clamp(vTexture, vec2(0.0), vec2(1.0)) * (uIndexSize - vec2(1.0)));
    uint index = texelFetch(uIndexTexture, texel, 0).r;
    oIndex = index;
    // The palette is 128x128 rather than 16384x1: GL 3.3 only guarantees a
    // maximum texture size of 1024, and the two-dimensional layout costs
    // nothing while removing the risk entirely.
    ivec2 slot = ivec2(int(index) % 128, int(index) / 128);
    oColour = texelFetch(uPalette, slot, 0);
}
)";

}

struct MapRenderer::Impl
{
    Api api;
    GLuint vao = 0;
    GLuint vertexBuffer = 0;
    GLuint textureBuffer = 0;
    GLuint indexBuffer = 0;
    GLuint program = 0;
    GLuint indexTexture = 0;
    GLuint paletteTexture = 0;
    GLuint framebuffer = 0;
    GLuint colourTarget = 0;
    GLuint idTarget = 0;
    GLuint depthTarget = 0;
    GLint viewProjectionUniform = -1;
    GLint indexTextureUniform = -1;
    GLint paletteUniform = -1;
    GLint indexSizeUniform = -1;
    GLsizei elementCount = 0;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
    std::uint32_t windowWidth = 0;
    std::uint32_t windowHeight = 0;
    std::uint32_t rasterWidth = 0;
    std::uint32_t rasterHeight = 0;
    double uploadedBend = -1.0;
    std::vector<float> positions;
};

MapRenderer::~MapRenderer()
{
    Close();
}

MapRendererStatus MapRenderer::Open(
    const MapRendererOptions& options,
    const MapIndexRaster& raster,
    std::string& message
)
{
    if (!raster)
    {
        message = "the index raster is not loaded";
        return MapRendererStatus::RasterInvalid;
    }
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        message = SDL_GetError();
        return MapRendererStatus::SdlInitFailed;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(
        SDL_GL_CONTEXT_PROFILE_MASK,
        SDL_GL_CONTEXT_PROFILE_CORE
    );
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
    if (!options.visible)
    {
        flags |= SDL_WINDOW_HIDDEN;
    }
    SDL_Window* window = SDL_CreateWindow(
        options.title.c_str(),
        static_cast<int>(options.windowWidth),
        static_cast<int>(options.windowHeight),
        flags
    );
    if (window == nullptr)
    {
        message = SDL_GetError();
        SDL_Quit();
        return MapRendererStatus::WindowFailed;
    }
    window_ = window;

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (context == nullptr)
    {
        message = SDL_GetError();
        Close();
        return MapRendererStatus::ContextFailed;
    }
    context_ = context;

    impl_ = new Impl();
    impl_->columns = options.gridColumns;
    impl_->rows = options.gridRows;
    impl_->windowWidth = options.windowWidth;
    impl_->windowHeight = options.windowHeight;
    impl_->rasterWidth = raster.width;
    impl_->rasterHeight = raster.height;

    Api& api = impl_->api;
    const bool loaded =
        Load(api.Viewport, "glViewport")
        && Load(api.ClearColor, "glClearColor")
        && Load(api.Clear, "glClear")
        && Load(api.Enable, "glEnable")
        && Load(api.Disable, "glDisable")
        && Load(api.GenVertexArrays, "glGenVertexArrays")
        && Load(api.BindVertexArray, "glBindVertexArray")
        && Load(api.DeleteVertexArrays, "glDeleteVertexArrays")
        && Load(api.GenBuffers, "glGenBuffers")
        && Load(api.BindBuffer, "glBindBuffer")
        && Load(api.BufferData, "glBufferData")
        && Load(api.BufferSubData, "glBufferSubData")
        && Load(api.DeleteBuffers, "glDeleteBuffers")
        && Load(api.VertexAttribPointer, "glVertexAttribPointer")
        && Load(api.EnableVertexAttribArray, "glEnableVertexAttribArray")
        && Load(api.CreateShader, "glCreateShader")
        && Load(api.ShaderSource, "glShaderSource")
        && Load(api.CompileShader, "glCompileShader")
        && Load(api.GetShaderiv, "glGetShaderiv")
        && Load(api.GetShaderInfoLog, "glGetShaderInfoLog")
        && Load(api.CreateProgram, "glCreateProgram")
        && Load(api.AttachShader, "glAttachShader")
        && Load(api.LinkProgram, "glLinkProgram")
        && Load(api.GetProgramiv, "glGetProgramiv")
        && Load(api.GetProgramInfoLog, "glGetProgramInfoLog")
        && Load(api.UseProgram, "glUseProgram")
        && Load(api.DeleteShader, "glDeleteShader")
        && Load(api.DeleteProgram, "glDeleteProgram")
        && Load(api.GetUniformLocation, "glGetUniformLocation")
        && Load(api.UniformMatrix4fv, "glUniformMatrix4fv")
        && Load(api.Uniform1i, "glUniform1i")
        && Load(api.Uniform2f, "glUniform2f")
        && Load(api.GenTextures, "glGenTextures")
        && Load(api.BindTexture, "glBindTexture")
        && Load(api.TexImage2D, "glTexImage2D")
        && Load(api.TexParameteri, "glTexParameteri")
        && Load(api.ActiveTexture, "glActiveTexture")
        && Load(api.DeleteTextures, "glDeleteTextures")
        && Load(api.DrawElements, "glDrawElements")
        && Load(api.GenFramebuffers, "glGenFramebuffers")
        && Load(api.BindFramebuffer, "glBindFramebuffer")
        && Load(api.FramebufferTexture2D, "glFramebufferTexture2D")
        && Load(api.FramebufferRenderbuffer, "glFramebufferRenderbuffer")
        && Load(api.DeleteFramebuffers, "glDeleteFramebuffers")
        && Load(api.CheckFramebufferStatus, "glCheckFramebufferStatus")
        && Load(api.GenRenderbuffers, "glGenRenderbuffers")
        && Load(api.BindRenderbuffer, "glBindRenderbuffer")
        && Load(api.RenderbufferStorage, "glRenderbufferStorage")
        && Load(api.DeleteRenderbuffers, "glDeleteRenderbuffers")
        && Load(api.DrawBuffers, "glDrawBuffers")
        && Load(api.ReadBuffer, "glReadBuffer")
        && Load(api.ReadPixels, "glReadPixels")
        && Load(api.BlitFramebuffer, "glBlitFramebuffer")
        && Load(api.ClearBufferuiv, "glClearBufferuiv");
    if (!loaded)
    {
        message = "the OpenGL 3.3 core entry points this renderer needs are "
                  "not all available";
        Close();
        return MapRendererStatus::ExtensionMissing;
    }

    // --- geometry ---
    //
    // Texture coordinates are static; positions are recomputed on the CPU
    // whenever the bend changes. That is deliberate. Writing the morph a
    // second time in GLSL would give this project two implementations of one
    // piece of semantics -- the mistake it has already paid for twice with the
    // declarative and controlled-script backends -- and the drawn shape could
    // then disagree with the shape the camera and the probes use. 131841
    // vertices is 1.6 MB, re-uploaded only while the bend is actually moving.
    const std::uint32_t columns = impl_->columns + 1;
    const std::uint32_t rows = impl_->rows + 1;
    std::vector<float> textureCoordinates;
    textureCoordinates.reserve(
        static_cast<std::size_t>(columns) * rows * 2
    );
    for (std::uint32_t row = 0; row < rows; ++row)
    {
        for (std::uint32_t column = 0; column < columns; ++column)
        {
            textureCoordinates.push_back(
                static_cast<float>(column) / static_cast<float>(impl_->columns)
            );
            textureCoordinates.push_back(
                static_cast<float>(row) / static_cast<float>(impl_->rows)
            );
        }
    }
    impl_->positions.assign(
        static_cast<std::size_t>(columns) * rows * 3,
        0.0f
    );

    std::vector<std::uint32_t> elements;
    elements.reserve(
        static_cast<std::size_t>(impl_->columns) * impl_->rows * 6
    );
    for (std::uint32_t row = 0; row < impl_->rows; ++row)
    {
        for (std::uint32_t column = 0; column < impl_->columns; ++column)
        {
            const std::uint32_t topLeft = row * columns + column;
            const std::uint32_t topRight = topLeft + 1;
            const std::uint32_t bottomLeft = topLeft + columns;
            const std::uint32_t bottomRight = bottomLeft + 1;
            elements.push_back(topLeft);
            elements.push_back(bottomLeft);
            elements.push_back(topRight);
            elements.push_back(topRight);
            elements.push_back(bottomLeft);
            elements.push_back(bottomRight);
        }
    }
    impl_->elementCount = static_cast<GLsizei>(elements.size());

    api.GenVertexArrays(1, &impl_->vao);
    api.BindVertexArray(impl_->vao);

    api.GenBuffers(1, &impl_->vertexBuffer);
    api.BindBuffer(GL_ARRAY_BUFFER, impl_->vertexBuffer);
    api.BufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(impl_->positions.size() * sizeof(float)),
        impl_->positions.data(),
        GL_DYNAMIC_DRAW
    );
    api.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    api.EnableVertexAttribArray(0);

    api.GenBuffers(1, &impl_->textureBuffer);
    api.BindBuffer(GL_ARRAY_BUFFER, impl_->textureBuffer);
    api.BufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(
            textureCoordinates.size() * sizeof(float)
        ),
        textureCoordinates.data(),
        GL_STATIC_DRAW
    );
    api.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    api.EnableVertexAttribArray(1);

    api.GenBuffers(1, &impl_->indexBuffer);
    api.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->indexBuffer);
    api.BufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(
            elements.size() * sizeof(std::uint32_t)
        ),
        elements.data(),
        GL_STATIC_DRAW
    );

    // --- shaders ---
    const auto compile = [&api, &message](GLenum stage, const char* source)
        -> GLuint
    {
        const GLuint shader = api.CreateShader(stage);
        api.ShaderSource(shader, 1, &source, nullptr);
        api.CompileShader(shader);
        GLint status = 0;
        api.GetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status == 0)
        {
            char log[1024] = {};
            api.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
            message = log;
            api.DeleteShader(shader);
            return 0;
        }
        return shader;
    };
    const GLuint vertex = compile(GL_VERTEX_SHADER, kVertexShader);
    if (vertex == 0)
    {
        Close();
        return MapRendererStatus::ShaderFailed;
    }
    const GLuint fragment = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (fragment == 0)
    {
        api.DeleteShader(vertex);
        Close();
        return MapRendererStatus::ShaderFailed;
    }
    impl_->program = api.CreateProgram();
    api.AttachShader(impl_->program, vertex);
    api.AttachShader(impl_->program, fragment);
    api.LinkProgram(impl_->program);
    GLint linked = 0;
    api.GetProgramiv(impl_->program, GL_LINK_STATUS, &linked);
    api.DeleteShader(vertex);
    api.DeleteShader(fragment);
    if (linked == 0)
    {
        char log[1024] = {};
        api.GetProgramInfoLog(impl_->program, sizeof(log), nullptr, log);
        message = log;
        Close();
        return MapRendererStatus::ShaderFailed;
    }
    impl_->viewProjectionUniform =
        api.GetUniformLocation(impl_->program, "uViewProjection");
    impl_->indexTextureUniform =
        api.GetUniformLocation(impl_->program, "uIndexTexture");
    impl_->paletteUniform =
        api.GetUniformLocation(impl_->program, "uPalette");
    impl_->indexSizeUniform =
        api.GetUniformLocation(impl_->program, "uIndexSize");

    // --- textures ---
    api.GenTextures(1, &impl_->indexTexture);
    api.BindTexture(GL_TEXTURE_2D, impl_->indexTexture);
    api.TexImage2D(
        GL_TEXTURE_2D,
        0,
        static_cast<GLint>(GL_R16UI),
        static_cast<GLsizei>(raster.width),
        static_cast<GLsizei>(raster.height),
        0,
        GL_RED_INTEGER,
        GL_UNSIGNED_SHORT,
        raster.indices.data()
    );
    // NEAREST and no mipmaps, on purpose. See the header.
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    api.GenTextures(1, &impl_->paletteTexture);
    api.BindTexture(GL_TEXTURE_2D, impl_->paletteTexture);
    const std::vector<std::uint32_t> blank(128 * 128, 0xFF202020u);
    api.TexImage2D(
        GL_TEXTURE_2D,
        0,
        static_cast<GLint>(GL_RGBA8),
        128,
        128,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        blank.data()
    );
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // --- offscreen targets ---
    //
    // Colour and province index are written in ONE pass, as two attachments.
    // Picking then reads the same pixels the viewer is looking at, which is
    // the only way a pick can be guaranteed to agree with the picture at an
    // arbitrary bend -- inverting the morph analytically is not possible.
    api.GenTextures(1, &impl_->colourTarget);
    api.BindTexture(GL_TEXTURE_2D, impl_->colourTarget);
    api.TexImage2D(
        GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8),
        static_cast<GLsizei>(impl_->windowWidth),
        static_cast<GLsizei>(impl_->windowHeight),
        0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr
    );
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    api.GenTextures(1, &impl_->idTarget);
    api.BindTexture(GL_TEXTURE_2D, impl_->idTarget);
    api.TexImage2D(
        GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R16UI),
        static_cast<GLsizei>(impl_->windowWidth),
        static_cast<GLsizei>(impl_->windowHeight),
        0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr
    );
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    api.GenRenderbuffers(1, &impl_->depthTarget);
    api.BindRenderbuffer(GL_RENDERBUFFER, impl_->depthTarget);
    api.RenderbufferStorage(
        GL_RENDERBUFFER,
        GL_DEPTH_COMPONENT24,
        static_cast<GLsizei>(impl_->windowWidth),
        static_cast<GLsizei>(impl_->windowHeight)
    );

    api.GenFramebuffers(1, &impl_->framebuffer);
    api.BindFramebuffer(GL_FRAMEBUFFER, impl_->framebuffer);
    api.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        impl_->colourTarget, 0
    );
    api.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
        impl_->idTarget, 0
    );
    api.FramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
        impl_->depthTarget
    );
    const GLenum targets[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    api.DrawBuffers(2, targets);
    if (api.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        message = "the offscreen framebuffer is incomplete";
        Close();
        return MapRendererStatus::ContextFailed;
    }
    api.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return MapRendererStatus::Ok;
}

void MapRenderer::Close()
{
    if (impl_ != nullptr)
    {
        Api& api = impl_->api;
        if (api.DeleteFramebuffers != nullptr && impl_->framebuffer != 0)
        {
            api.DeleteFramebuffers(1, &impl_->framebuffer);
        }
        if (api.DeleteRenderbuffers != nullptr && impl_->depthTarget != 0)
        {
            api.DeleteRenderbuffers(1, &impl_->depthTarget);
        }
        if (api.DeleteTextures != nullptr)
        {
            const GLuint textures[4] = {
                impl_->indexTexture,
                impl_->paletteTexture,
                impl_->colourTarget,
                impl_->idTarget
            };
            for (const GLuint texture : textures)
            {
                if (texture != 0)
                {
                    api.DeleteTextures(1, &texture);
                }
            }
        }
        if (api.DeleteBuffers != nullptr)
        {
            const GLuint buffers[3] = {
                impl_->vertexBuffer,
                impl_->textureBuffer,
                impl_->indexBuffer
            };
            for (const GLuint buffer : buffers)
            {
                if (buffer != 0)
                {
                    api.DeleteBuffers(1, &buffer);
                }
            }
        }
        if (api.DeleteVertexArrays != nullptr && impl_->vao != 0)
        {
            api.DeleteVertexArrays(1, &impl_->vao);
        }
        if (api.DeleteProgram != nullptr && impl_->program != 0)
        {
            api.DeleteProgram(impl_->program);
        }
        delete impl_;
        impl_ = nullptr;
    }
    if (context_ != nullptr)
    {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(context_));
        context_ = nullptr;
    }
    if (window_ != nullptr)
    {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
        window_ = nullptr;
        SDL_Quit();
    }
}

void MapRenderer::SetPalette(const std::vector<std::uint32_t>& palette)
{
    if (impl_ == nullptr)
    {
        return;
    }
    std::vector<std::uint32_t> texels(128 * 128, 0u);
    const std::size_t count = palette.size() < texels.size()
        ? palette.size()
        : texels.size();
    std::memcpy(texels.data(), palette.data(), count * sizeof(std::uint32_t));
    Api& api = impl_->api;
    api.BindTexture(GL_TEXTURE_2D, impl_->paletteTexture);
    api.TexImage2D(
        GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), 128, 128, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, texels.data()
    );
}

void MapRenderer::Draw(
    const MapProjection& projection,
    const MapCamera& camera
)
{
    if (impl_ == nullptr)
    {
        return;
    }
    Api& api = impl_->api;

    if (impl_->uploadedBend != camera.bend)
    {
        const std::uint32_t columns = impl_->columns + 1;
        const std::uint32_t rows = impl_->rows + 1;
        std::size_t cursor = 0;
        for (std::uint32_t row = 0; row < rows; ++row)
        {
            const double v =
                static_cast<double>(row) / static_cast<double>(impl_->rows);
            for (std::uint32_t column = 0; column < columns; ++column)
            {
                const double u = static_cast<double>(column)
                    / static_cast<double>(impl_->columns);
                const MapPoint point =
                    ProjectMapPoint(projection, u, v, camera.bend);
                impl_->positions[cursor++] = static_cast<float>(point.x);
                impl_->positions[cursor++] = static_cast<float>(point.y);
                impl_->positions[cursor++] = static_cast<float>(point.z);
            }
        }
        api.BindBuffer(GL_ARRAY_BUFFER, impl_->vertexBuffer);
        api.BufferSubData(
            GL_ARRAY_BUFFER,
            0,
            static_cast<GLsizeiptr>(
                impl_->positions.size() * sizeof(float)
            ),
            impl_->positions.data()
        );
        impl_->uploadedBend = camera.bend;
    }

    const MapViewMatrix view = BuildMapViewMatrix(projection, camera);
    const double aspect = static_cast<double>(impl_->windowWidth)
        / static_cast<double>(impl_->windowHeight);
    const double fov = 45.0 * 3.14159265358979323846 / 180.0;
    const double focal = 1.0 / std::tan(fov * 0.5);
    const double nearPlane = 0.01;
    const double farPlane = 100.0;
    double perspective[16] = {};
    perspective[0] = focal / aspect;
    perspective[5] = focal;
    perspective[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    perspective[11] = (2.0 * farPlane * nearPlane) / (nearPlane - farPlane);
    perspective[14] = -1.0;

    float mvp[16] = {};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            double sum = 0.0;
            for (int inner = 0; inner < 4; ++inner)
            {
                sum += perspective[row * 4 + inner]
                    * view[static_cast<std::size_t>(inner * 4 + column)];
            }
            // Transposed on the way out: GLSL wants column-major and both
            // matrices here are row-major.
            mvp[column * 4 + row] = static_cast<float>(sum);
        }
    }

    api.BindFramebuffer(GL_FRAMEBUFFER, impl_->framebuffer);
    const GLenum targets[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    api.DrawBuffers(2, targets);
    api.Viewport(
        0,
        0,
        static_cast<GLsizei>(impl_->windowWidth),
        static_cast<GLsizei>(impl_->windowHeight)
    );
    api.ClearColor(0.05f, 0.06f, 0.09f, 1.0f);
    api.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // The integer attachment needs its own clear: a float clear leaves it
    // undefined, and picking would then read whatever the last frame left.
    const GLuint zero[4] = {0u, 0u, 0u, 0u};
    api.ClearBufferuiv(0x1800 /* GL_COLOR */, 1, zero);
    api.Enable(GL_DEPTH_TEST);

    api.UseProgram(impl_->program);
    api.UniformMatrix4fv(impl_->viewProjectionUniform, 1, GL_FALSE, mvp);
    api.ActiveTexture(GL_TEXTURE0);
    api.BindTexture(GL_TEXTURE_2D, impl_->indexTexture);
    api.Uniform1i(impl_->indexTextureUniform, 0);
    api.ActiveTexture(GL_TEXTURE1);
    api.BindTexture(GL_TEXTURE_2D, impl_->paletteTexture);
    api.Uniform1i(impl_->paletteUniform, 1);
    api.Uniform2f(
        impl_->indexSizeUniform,
        static_cast<float>(impl_->rasterWidth),
        static_cast<float>(impl_->rasterHeight)
    );
    api.BindVertexArray(impl_->vao);
    api.DrawElements(
        GL_TRIANGLES,
        impl_->elementCount,
        GL_UNSIGNED_INT,
        nullptr
    );

    api.BindFramebuffer(GL_READ_FRAMEBUFFER, impl_->framebuffer);
    api.ReadBuffer(GL_COLOR_ATTACHMENT0);
    api.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    api.BlitFramebuffer(
        0, 0,
        static_cast<GLint>(impl_->windowWidth),
        static_cast<GLint>(impl_->windowHeight),
        0, 0,
        static_cast<GLint>(impl_->windowWidth),
        static_cast<GLint>(impl_->windowHeight),
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST_FILTER_BLIT
    );
    api.BindFramebuffer(GL_FRAMEBUFFER, 0);
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(window_));
}

std::uint16_t MapRenderer::PickAt(std::uint32_t x, std::uint32_t y)
{
    if (impl_ == nullptr
        || x >= impl_->windowWidth
        || y >= impl_->windowHeight)
    {
        return 0;
    }
    Api& api = impl_->api;
    api.BindFramebuffer(GL_READ_FRAMEBUFFER, impl_->framebuffer);
    api.ReadBuffer(GL_COLOR_ATTACHMENT1);
    std::uint16_t value = 0;
    // GL's origin is bottom-left; window coordinates are top-left.
    const GLint row =
        static_cast<GLint>(impl_->windowHeight) - 1 - static_cast<GLint>(y);
    api.ReadPixels(
        static_cast<GLint>(x),
        row,
        1,
        1,
        GL_RED_INTEGER,
        GL_UNSIGNED_SHORT,
        &value
    );
    api.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return value;
}

}
