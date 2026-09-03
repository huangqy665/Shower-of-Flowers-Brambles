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
constexpr GLenum GL_COLOR_ATTACHMENT2 = 0x8CE2;
constexpr GLenum GL_RG16 = 0x822C;
constexpr GLenum GL_RG = 0x8227;
constexpr GLenum GL_NONE = 0;
constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_NEAREST_FILTER_BLIT = 0x2600;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_BACK = 0x0405;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_RED = 0x1903;
constexpr GLenum GL_R8 = 0x8229;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_PIXEL_PACK_BUFFER = 0x88EB;
constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;
constexpr GLenum GL_STREAM_READ = 0x88E1;
constexpr GLenum GL_READ_ONLY = 0x88B8;

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
    void (*Uniform1f)(GLint, GLfloat) = nullptr;
    void (*Uniform2f)(GLint, GLfloat, GLfloat) = nullptr;
    void (*Uniform1ui)(GLint, GLuint) = nullptr;
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat)
        = nullptr;
    void (*BlendFunc)(GLenum, GLenum) = nullptr;
    void (*PixelStorei)(GLenum, GLint) = nullptr;
    void (*DrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    void* (*MapBuffer)(GLenum, GLenum) = nullptr;
    GLboolean (*UnmapBuffer)(GLenum) = nullptr;
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
// The map coordinate this pixel came from, kept so a host can ask what is
// under the cursor. The fragment already has it; recovering it afterwards
// would mean inverting the morph, which is not possible at an arbitrary bend.
layout(location = 2) out vec2 oMapPoint;
uniform usampler2D uIndexTexture;
uniform sampler2D uPalette;
uniform vec2 uIndexSize;
uniform uint uSelected;
uniform int uPaletteSide;
// How far the map is turned under the grid.
//
// The grid is built centred on the camera, so its own u runs -1/2 .. +1/2 with
// the viewer at 0. The MAP coordinate is that plus this offset, wrapped -- and
// the wrap is the whole seam fix: the cut edge of the cylinder sits at the far
// end of the grid, which is behind the viewer, always. A province the cut
// passes through is drawn at both ends of the grid, twice, and both copies
// carry the same index -- so it is one province rendered twice, not two
// provinces.
uniform float uMapOffset;
void main()
{
    vec2 mapPoint = vec2(
        fract(vTexture.x + uMapOffset),
        clamp(vTexture.y, 0.0, 1.0)
    );
    ivec2 texel = ivec2(mapPoint * (uIndexSize - vec2(1.0)));
    uint index = texelFetch(uIndexTexture, texel, 0).r;
    oIndex = index;
    oMapPoint = mapPoint;
    // The palette is square rather than a single row: GL 3.3 only guarantees
    // a maximum texture size of 1024, so a 16384-wide strip is not a texture
    // anywhere. The side is a uniform because it is computed from how many
    // provinces the map has -- a fixed 128 was a rendering ceiling of 16383
    // that the content had no way to know about.
    ivec2 slot = ivec2(int(index) % uPaletteSide, int(index) / uPaletteSide);
    vec4 colour = texelFetch(uPalette, slot, 0);
    // The selection highlight, decided in the same invocation that wrote
    // oIndex. A separate outline pass would have to reconstruct which pixels
    // belong to the province; here it is the comparison that already happened.
    if (index != 0u && index == uSelected)
    {
        colour = vec4(mix(colour.rgb, vec3(1.0, 0.95, 0.6), 0.55), colour.a);
    }
    oColour = colour;
}
)";

// Screen space, in pixels, with the origin at the top left -- the same
// coordinates the layout solver works in, so no conversion happens between
// what a probe asserts and what is drawn.
const char* kOverlayVertexShader = R"(#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aAtlas;
layout(location = 2) in vec4 aColour;
layout(location = 3) in float aTextured;
uniform vec2 uViewport;
uniform vec2 uAtlasSize;
out vec2 vAtlas;
out vec4 vColour;
out float vTextured;
void main()
{
    vAtlas = aAtlas / max(uAtlasSize, vec2(1.0));
    vColour = aColour;
    vTextured = aTextured;
    vec2 ndc = vec2(
        aPosition.x / uViewport.x * 2.0 - 1.0,
        1.0 - aPosition.y / uViewport.y * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

const char* kOverlayFragmentShader = R"(#version 330 core
in vec2 vAtlas;
in vec4 vColour;
in float vTextured;
layout(location = 0) out vec4 oColour;
uniform sampler2D uAtlas;
void main()
{
    float coverage = vTextured > 0.5 ? texture(uAtlas, vAtlas).r : 1.0;
    oColour = vec4(vColour.rgb, vColour.a * coverage);
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
    GLuint mapPointTarget = 0;
    bool mapPointReadback = false;
    GLuint depthTarget = 0;
    GLint viewProjectionUniform = -1;
    GLint indexTextureUniform = -1;
    GLint paletteUniform = -1;
    GLint indexSizeUniform = -1;
    GLint selectedUniform = -1;
    GLint paletteSideUniform = -1;
    GLint mapOffsetUniform = -1;
    // Side of the square palette texture, in texels. Chosen at Open from the
    // province count rather than fixed, so the map's size decides it and the
    // renderer imposes no ceiling of its own.
    std::uint32_t paletteSide = 0;
    GLuint overlayProgram = 0;
    GLuint overlayVao = 0;
    GLuint overlayBuffer = 0;
    GLuint atlasTexture = 0;
    GLint overlayViewportUniform = -1;
    GLint overlayAtlasUniform = -1;
    GLint overlayAtlasSizeUniform = -1;
    std::uint32_t atlasWidth = 0;
    std::uint32_t atlasHeight = 0;
    std::uint16_t selected = 0;
    std::vector<float> overlayVertices;
    // Two pixel pack buffers used in turn: a request is issued into one while
    // the other, filled at least a frame ago, is mapped and read. One buffer
    // would be no better than a synchronous read, because mapping it would
    // wait for the transfer that was just issued into it.
    GLuint pickBuffers[2] = {0, 0};
    std::uint32_t pickCursor = 0;
    bool pickPending[2] = {false, false};
    std::uint16_t lastPick = 0;
    const MapEntityIndex* entities = nullptr;
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
    // The id attachment is R16UI, so a province index has to fit in sixteen
    // bits. Beyond that the attachment would wrap and picking would name the
    // wrong province with no sign that anything was wrong, so it is refused
    // here instead. Raising it is a format change -- R32UI and a wider
    // readback -- not a constant.
    if (raster.provinceCount >= 0xFFFFu)
    {
        message = "this backend addresses at most 65534 provinces; the map "
                  "declares " + std::to_string(raster.provinceCount);
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
    if (options.resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
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
    // Vsync when there is a window to look at. Without it an interactive
    // viewer spins a core producing frames nobody sees; a hidden smoke test
    // wants none of that wait.
    SDL_GL_SetSwapInterval(options.visible ? 1 : 0);

    impl_ = new Impl();
    impl_->columns = options.gridColumns;
    impl_->rows = options.gridRows;
    impl_->windowWidth = options.windowWidth;
    impl_->windowHeight = options.windowHeight;
    impl_->mapPointReadback = options.mapPointReadback;
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
        && Load(api.Uniform1f, "glUniform1f")
        && Load(api.Uniform2f, "glUniform2f")
        && Load(api.Uniform1ui, "glUniform1ui")
        && Load(api.Uniform4f, "glUniform4f")
        && Load(api.BlendFunc, "glBlendFunc")
        && Load(api.PixelStorei, "glPixelStorei")
        && Load(api.DrawArrays, "glDrawArrays")
        && Load(api.MapBuffer, "glMapBuffer")
        && Load(api.UnmapBuffer, "glUnmapBuffer")
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
    impl_->selectedUniform =
        api.GetUniformLocation(impl_->program, "uSelected");
    impl_->paletteSideUniform =
        api.GetUniformLocation(impl_->program, "uPaletteSide");
    impl_->mapOffsetUniform =
        api.GetUniformLocation(impl_->program, "uMapOffset");

    // --- the overlay program ---
    const GLuint overlayVertex =
        compile(GL_VERTEX_SHADER, kOverlayVertexShader);
    const GLuint overlayFragment = overlayVertex == 0
        ? 0
        : compile(GL_FRAGMENT_SHADER, kOverlayFragmentShader);
    if (overlayVertex == 0 || overlayFragment == 0)
    {
        if (overlayVertex != 0)
        {
            api.DeleteShader(overlayVertex);
        }
        Close();
        return MapRendererStatus::ShaderFailed;
    }
    impl_->overlayProgram = api.CreateProgram();
    api.AttachShader(impl_->overlayProgram, overlayVertex);
    api.AttachShader(impl_->overlayProgram, overlayFragment);
    api.LinkProgram(impl_->overlayProgram);
    GLint overlayLinked = 0;
    api.GetProgramiv(impl_->overlayProgram, GL_LINK_STATUS, &overlayLinked);
    api.DeleteShader(overlayVertex);
    api.DeleteShader(overlayFragment);
    if (overlayLinked == 0)
    {
        char log[1024] = {};
        api.GetProgramInfoLog(impl_->overlayProgram, sizeof(log), nullptr, log);
        message = log;
        Close();
        return MapRendererStatus::ShaderFailed;
    }
    impl_->overlayViewportUniform =
        api.GetUniformLocation(impl_->overlayProgram, "uViewport");
    impl_->overlayAtlasUniform =
        api.GetUniformLocation(impl_->overlayProgram, "uAtlas");
    impl_->overlayAtlasSizeUniform =
        api.GetUniformLocation(impl_->overlayProgram, "uAtlasSize");

    api.GenVertexArrays(1, &impl_->overlayVao);
    api.BindVertexArray(impl_->overlayVao);
    api.GenBuffers(2, impl_->pickBuffers);
    for (const GLuint buffer : impl_->pickBuffers)
    {
        api.BindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
        // Four bytes for a two-byte pixel. GL pads a packed row out to
        // GL_PACK_ALIGNMENT, which defaults to four, so a buffer sized to the
        // pixel is too small for the transfer and the read silently writes
        // nothing -- which is exactly what it did, and the async pick came
        // back as zero. The alignment is set to one below as well; the slack
        // here costs nothing and removes the dependency on getting that right
        // in two places.
        api.BufferData(
            GL_PIXEL_PACK_BUFFER,
            static_cast<GLsizeiptr>(4),
            nullptr,
            GL_STREAM_READ
        );
    }
    api.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    api.GenBuffers(1, &impl_->overlayBuffer);
    api.BindBuffer(GL_ARRAY_BUFFER, impl_->overlayBuffer);
    // Nine floats per vertex: position, atlas texel, colour, textured flag.
    const GLsizei stride = 9 * static_cast<GLsizei>(sizeof(float));
    api.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    api.EnableVertexAttribArray(0);
    api.VertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(2 * sizeof(float)));
    api.EnableVertexAttribArray(1);
    api.VertexAttribPointer(
        2, 4, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(4 * sizeof(float)));
    api.EnableVertexAttribArray(2);
    api.VertexAttribPointer(
        3, 1, GL_FLOAT, GL_FALSE, stride,
        reinterpret_cast<const void*>(8 * sizeof(float)));
    api.EnableVertexAttribArray(3);
    api.BindVertexArray(0);

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
    // One texel per province, plus row 0 for "no province", rounded up to a
    // square. `provinceCount` comes from the raster the Package shipped, so a
    // larger map simply gets a larger palette; nothing here caps it.
    //
    // The remaining ceiling is the id attachment, which is R16UI and so can
    // name 65535 provinces. That one is refused at Open rather than wrapped
    // silently -- see the check above it.
    impl_->paletteSide = 1;
    while (static_cast<std::uint64_t>(impl_->paletteSide)
            * impl_->paletteSide
        < static_cast<std::uint64_t>(raster.provinceCount) + 1u)
    {
        impl_->paletteSide *= 2;
    }
    const std::vector<std::uint32_t> blank(
        static_cast<std::size_t>(impl_->paletteSide) * impl_->paletteSide,
        0xFF202020u
    );
    api.TexImage2D(
        GL_TEXTURE_2D,
        0,
        static_cast<GLint>(GL_RGBA8),
        static_cast<GLsizei>(impl_->paletteSide),
        static_cast<GLsizei>(impl_->paletteSide),
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
    if (!BuildTargets())
    {
        message = "the offscreen framebuffer is incomplete";
        Close();
        return MapRendererStatus::ContextFailed;
    }
    return MapRendererStatus::Ok;
}

// The colour, id and depth attachments, at the current window size.
//
// Separated from Open because a resizable window needs them rebuilt, and
// rebuilding them is the whole of what resizing costs: the grid, the shaders,
// the index texture and the palette are all size-independent.
bool MapRenderer::BuildTargets()
{
    Api& api = impl_->api;
    const GLsizei width = static_cast<GLsizei>(impl_->windowWidth);
    const GLsizei height = static_cast<GLsizei>(impl_->windowHeight);

    if (impl_->framebuffer != 0)
    {
        api.DeleteFramebuffers(1, &impl_->framebuffer);
        impl_->framebuffer = 0;
    }
    if (impl_->depthTarget != 0)
    {
        api.DeleteRenderbuffers(1, &impl_->depthTarget);
        impl_->depthTarget = 0;
    }
    for (GLuint* target : {
            &impl_->colourTarget,
            &impl_->idTarget,
            &impl_->mapPointTarget})
    {
        if (*target != 0)
        {
            api.DeleteTextures(1, target);
            *target = 0;
        }
    }

    api.GenTextures(1, &impl_->colourTarget);
    api.BindTexture(GL_TEXTURE_2D, impl_->colourTarget);
    api.TexImage2D(
        GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8),
        width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr
    );
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    if (impl_->mapPointReadback)
    {
        api.GenTextures(1, &impl_->mapPointTarget);
        api.BindTexture(GL_TEXTURE_2D, impl_->mapPointTarget);
        api.TexImage2D(
            GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RG16),
            width, height, 0, GL_RG, GL_UNSIGNED_SHORT, nullptr
        );
        api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    api.GenTextures(1, &impl_->idTarget);
    api.BindTexture(GL_TEXTURE_2D, impl_->idTarget);
    api.TexImage2D(
        GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R16UI),
        width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr
    );
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    api.GenRenderbuffers(1, &impl_->depthTarget);
    api.BindRenderbuffer(GL_RENDERBUFFER, impl_->depthTarget);
    api.RenderbufferStorage(
        GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

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
    if (impl_->mapPointReadback)
    {
        api.FramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D,
            impl_->mapPointTarget, 0
        );
    }
    api.FramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
        impl_->depthTarget
    );
    // GL_NONE for a slot the shader still writes: the write is discarded, so
    // one shader serves both configurations rather than two that could drift.
    const GLenum targets[3] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        impl_->mapPointReadback ? GL_COLOR_ATTACHMENT2 : GL_NONE
    };
    api.DrawBuffers(3, targets);
    const bool complete =
        api.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    api.BindFramebuffer(GL_FRAMEBUFFER, 0);

    // Any pick already in flight was issued against the old attachments, so
    // its answer would name a pixel that no longer exists.
    impl_->pickPending[0] = false;
    impl_->pickPending[1] = false;
    impl_->lastPick = 0;
    return complete;
}

void MapRenderer::Resize(std::uint32_t width, std::uint32_t height)
{
    if (impl_ == nullptr || width == 0 || height == 0
        || (width == impl_->windowWidth && height == impl_->windowHeight))
    {
        return;
    }
    impl_->windowWidth = width;
    impl_->windowHeight = height;
    if (!BuildTargets())
    {
        diagnostics_ = "the offscreen framebuffer is incomplete after a "
                       "resize";
    }
}

void MapRenderer::RequestPick(std::uint32_t x, std::uint32_t y)
{
    if (impl_ == nullptr
        || x >= impl_->windowWidth
        || y >= impl_->windowHeight)
    {
        return;
    }
    Api& api = impl_->api;
    const std::uint32_t collect = impl_->pickCursor;
    const std::uint32_t issue = 1u - impl_->pickCursor;

    // Collect the older request first. Mapping the buffer that was just
    // written to would wait for exactly the transfer this call is trying not
    // to wait for.
    if (impl_->pickPending[collect])
    {
        api.BindBuffer(GL_PIXEL_PACK_BUFFER, impl_->pickBuffers[collect]);
        if (const void* mapped = api.MapBuffer(
                GL_PIXEL_PACK_BUFFER, GL_READ_ONLY))
        {
            std::uint16_t value = 0;
            std::memcpy(&value, mapped, sizeof(value));
            impl_->lastPick = value;
            api.UnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        impl_->pickPending[collect] = false;
    }

    api.BindFramebuffer(GL_READ_FRAMEBUFFER, impl_->framebuffer);
    api.ReadBuffer(GL_COLOR_ATTACHMENT1);
    api.BindBuffer(GL_PIXEL_PACK_BUFFER, impl_->pickBuffers[issue]);
    api.PixelStorei(GL_PACK_ALIGNMENT, 1);
    // GL's origin is bottom-left; window coordinates are top-left.
    const GLint row =
        static_cast<GLint>(impl_->windowHeight) - 1 - static_cast<GLint>(y);
    // A null pointer here is an offset into the bound pack buffer, not an
    // address: this is the call that becomes asynchronous.
    api.ReadPixels(
        static_cast<GLint>(x), row, 1, 1,
        GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr
    );
    impl_->pickPending[issue] = true;
    api.PixelStorei(GL_PACK_ALIGNMENT, 4);
    api.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    api.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    impl_->pickCursor = issue;
}

std::uint16_t MapRenderer::LastPick() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->lastPick;
}

void MapRenderer::SetEntityIndex(const MapEntityIndex* entities) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->entities = entities;
    }
}

kernel::EntityId MapRenderer::PickEntityAt(std::uint32_t x, std::uint32_t y)
{
    const std::uint16_t index = PickAt(x, y);
    if (impl_ == nullptr || impl_->entities == nullptr)
    {
        return {};
    }
    return impl_->entities->EntityFor(index);
}

kernel::EntityId MapRenderer::LastPickedEntity() const noexcept
{
    if (impl_ == nullptr || impl_->entities == nullptr)
    {
        return {};
    }
    return impl_->entities->EntityFor(impl_->lastPick);
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
            const GLuint textures[6] = {
                impl_->indexTexture,
                impl_->paletteTexture,
                impl_->colourTarget,
                impl_->idTarget,
                impl_->mapPointTarget,
                impl_->atlasTexture
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
            const GLuint buffers[6] = {
                impl_->vertexBuffer,
                impl_->textureBuffer,
                impl_->indexBuffer,
                impl_->overlayBuffer,
                impl_->pickBuffers[0],
                impl_->pickBuffers[1]
            };
            for (const GLuint buffer : buffers)
            {
                if (buffer != 0)
                {
                    api.DeleteBuffers(1, &buffer);
                }
            }
        }
        if (api.DeleteVertexArrays != nullptr)
        {
            const GLuint arrays[2] = {impl_->vao, impl_->overlayVao};
            for (const GLuint array : arrays)
            {
                if (array != 0)
                {
                    api.DeleteVertexArrays(1, &array);
                }
            }
        }
        if (api.DeleteProgram != nullptr)
        {
            const GLuint programs[2] = {
                impl_->program,
                impl_->overlayProgram
            };
            for (const GLuint program : programs)
            {
                if (program != 0)
                {
                    api.DeleteProgram(program);
                }
            }
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

bool MapRenderer::MapPointAt(
    std::uint32_t x,
    std::uint32_t y,
    double& u,
    double& v
)
{
    if (impl_ == nullptr
        || !impl_->mapPointReadback
        || x >= impl_->windowWidth
        || y >= impl_->windowHeight)
    {
        return false;
    }
    // Off the map reads back as the cleared zero, which is also a legitimate
    // corner of the map -- so the id attachment is what says whether the pixel
    // is on the surface at all, and this only says where.
    if (PickAt(x, y) == 0)
    {
        return false;
    }
    Api& api = impl_->api;
    api.BindFramebuffer(GL_READ_FRAMEBUFFER, impl_->framebuffer);
    api.ReadBuffer(GL_COLOR_ATTACHMENT2);
    std::uint16_t pixel[2] = {0, 0};
    const GLint row =
        static_cast<GLint>(impl_->windowHeight) - 1 - static_cast<GLint>(y);
    api.PixelStorei(GL_PACK_ALIGNMENT, 1);
    api.ReadPixels(
        static_cast<GLint>(x), row, 1, 1,
        GL_RG, GL_UNSIGNED_SHORT, pixel
    );
    api.PixelStorei(GL_PACK_ALIGNMENT, 4);
    api.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    u = static_cast<double>(pixel[0]) / 65535.0;
    v = static_cast<double>(pixel[1]) / 65535.0;
    return true;
}

std::uint32_t MapRenderer::PaletteSide() const noexcept
{
    return impl_ == nullptr ? 0 : impl_->paletteSide;
}

std::size_t MapRenderer::PaletteSize() const noexcept
{
    return impl_ == nullptr
        ? 0
        : static_cast<std::size_t>(impl_->paletteSide) * impl_->paletteSide;
}

void MapRenderer::SetPalette(const std::vector<std::uint32_t>& palette)
{
    if (impl_ == nullptr)
    {
        return;
    }
    std::vector<std::uint32_t> texels(
        static_cast<std::size_t>(impl_->paletteSide) * impl_->paletteSide,
        0u
    );
    const std::size_t count = palette.size() < texels.size()
        ? palette.size()
        : texels.size();
    std::memcpy(texels.data(), palette.data(), count * sizeof(std::uint32_t));
    Api& api = impl_->api;
    api.BindTexture(GL_TEXTURE_2D, impl_->paletteTexture);
    api.TexImage2D(
        GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8),
        static_cast<GLsizei>(impl_->paletteSide),
        static_cast<GLsizei>(impl_->paletteSide), 0,
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

    // The grid is generated CENTRED ON THE CAMERA, not on u = 0.
    //
    // Below a full sphere the surface is an open cylinder with a cut edge, and
    // a grid pinned to u = 0 puts that edge at a fixed longitude -- so turning
    // the map far enough swings it into view. Building the grid around the
    // camera instead puts the edge at the far end of the strip, which is
    // behind the viewer whatever the camera does. Nothing has to be clamped
    // and no longitude is out of reach.
    //
    // It depends only on the bend, so it is still rebuilt only when the bend
    // moves: turning the map changes the map OFFSET, which is a uniform.
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

    // The camera sits at the middle of the grid by construction; the real
    // longitude is carried by uMapOffset.
    const MapViewMatrix view = BuildMapViewMatrix(
        projection,
        CameraInGridSpace(camera)
    );
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
    const GLenum targets[3] = {
        GL_COLOR_ATTACHMENT0,
        GL_COLOR_ATTACHMENT1,
        impl_->mapPointReadback ? GL_COLOR_ATTACHMENT2 : GL_NONE
    };
    api.DrawBuffers(3, targets);
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
    api.Uniform1ui(
        impl_->selectedUniform,
        static_cast<GLuint>(impl_->selected)
    );
    api.Uniform1i(
        impl_->paletteSideUniform,
        static_cast<GLint>(impl_->paletteSide)
    );
    // The map's own longitude, minus the grid's centre.
    const double offset = camera.lookAtU - 0.5;
    api.Uniform1f(
        impl_->mapOffsetUniform,
        static_cast<float>(offset - std::floor(offset))
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
}

void MapRenderer::SetSelection(std::uint16_t provinceIndex)
{
    if (impl_ != nullptr)
    {
        impl_->selected = provinceIndex;
    }
}

void MapRenderer::SetFontAtlas(
    const std::vector<std::uint8_t>& coverage,
    std::uint32_t width,
    std::uint32_t height
)
{
    if (impl_ == nullptr
        || width == 0
        || height == 0
        || coverage.size()
            != static_cast<std::size_t>(width) * height)
    {
        return;
    }
    Api& api = impl_->api;
    if (impl_->atlasTexture == 0)
    {
        api.GenTextures(1, &impl_->atlasTexture);
    }
    api.BindTexture(GL_TEXTURE_2D, impl_->atlasTexture);
    // Rows of a coverage bitmap are not multiples of four bytes. Without this
    // every row after the first is read at the wrong offset, which looks like
    // a font that shears.
    api.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    api.TexImage2D(
        GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R8),
        static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
        GL_RED, GL_UNSIGNED_BYTE, coverage.data()
    );
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    api.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    api.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    impl_->atlasWidth = width;
    impl_->atlasHeight = height;
}

void MapRenderer::DrawOverlay(const std::vector<OverlayQuad>& quads)
{
    if (impl_ == nullptr || quads.empty())
    {
        return;
    }
    Api& api = impl_->api;
    impl_->overlayVertices.clear();
    impl_->overlayVertices.reserve(quads.size() * 6 * 9);
    for (const OverlayQuad& quad : quads)
    {
        if (quad.width <= 0 || quad.height <= 0)
        {
            continue;
        }
        const float red = static_cast<float>(quad.colour & 0xFFu) / 255.0f;
        const float green =
            static_cast<float>((quad.colour >> 8) & 0xFFu) / 255.0f;
        const float blue =
            static_cast<float>((quad.colour >> 16) & 0xFFu) / 255.0f;
        const float alpha =
            static_cast<float>((quad.colour >> 24) & 0xFFu) / 255.0f;
        const float textured = quad.textured ? 1.0f : 0.0f;
        const float left = static_cast<float>(quad.x);
        const float top = static_cast<float>(quad.y);
        const float right = left + static_cast<float>(quad.width);
        const float bottom = top + static_cast<float>(quad.height);
        const float atlasLeft = static_cast<float>(quad.atlasX);
        const float atlasTop = static_cast<float>(quad.atlasY);
        const float atlasRight = atlasLeft + static_cast<float>(quad.width);
        const float atlasBottom = atlasTop + static_cast<float>(quad.height);
        const float corners[6][4] = {
            {left, top, atlasLeft, atlasTop},
            {right, top, atlasRight, atlasTop},
            {right, bottom, atlasRight, atlasBottom},
            {left, top, atlasLeft, atlasTop},
            {right, bottom, atlasRight, atlasBottom},
            {left, bottom, atlasLeft, atlasBottom}
        };
        for (const float* corner : corners)
        {
            impl_->overlayVertices.push_back(corner[0]);
            impl_->overlayVertices.push_back(corner[1]);
            impl_->overlayVertices.push_back(corner[2]);
            impl_->overlayVertices.push_back(corner[3]);
            impl_->overlayVertices.push_back(red);
            impl_->overlayVertices.push_back(green);
            impl_->overlayVertices.push_back(blue);
            impl_->overlayVertices.push_back(alpha);
            impl_->overlayVertices.push_back(textured);
        }
    }
    if (impl_->overlayVertices.empty())
    {
        return;
    }

    api.BindFramebuffer(GL_FRAMEBUFFER, 0);
    api.Viewport(
        0, 0,
        static_cast<GLsizei>(impl_->windowWidth),
        static_cast<GLsizei>(impl_->windowHeight)
    );
    api.Disable(GL_DEPTH_TEST);
    api.Enable(GL_BLEND);
    api.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    api.UseProgram(impl_->overlayProgram);
    api.Uniform2f(
        impl_->overlayViewportUniform,
        static_cast<float>(impl_->windowWidth),
        static_cast<float>(impl_->windowHeight)
    );
    api.Uniform2f(
        impl_->overlayAtlasSizeUniform,
        static_cast<float>(impl_->atlasWidth),
        static_cast<float>(impl_->atlasHeight)
    );
    api.ActiveTexture(GL_TEXTURE0);
    api.BindTexture(GL_TEXTURE_2D, impl_->atlasTexture);
    api.Uniform1i(impl_->overlayAtlasUniform, 0);
    api.BindVertexArray(impl_->overlayVao);
    api.BindBuffer(GL_ARRAY_BUFFER, impl_->overlayBuffer);
    api.BufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(
            impl_->overlayVertices.size() * sizeof(float)
        ),
        impl_->overlayVertices.data(),
        GL_DYNAMIC_DRAW
    );
    api.DrawArrays(
        GL_TRIANGLES,
        0,
        static_cast<GLsizei>(impl_->overlayVertices.size() / 9)
    );
    api.BindVertexArray(0);
    api.Disable(GL_BLEND);
}

std::uint32_t MapRenderer::ColourAt(std::uint32_t x, std::uint32_t y)
{
    if (impl_ == nullptr
        || x >= impl_->windowWidth
        || y >= impl_->windowHeight)
    {
        return 0;
    }
    Api& api = impl_->api;
    api.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    // PickAt leaves the read buffer pointing at the id attachment. On the
    // default framebuffer that name is not even legal, so it has to be set
    // back every time rather than assumed.
    api.ReadBuffer(GL_BACK);
    std::uint8_t pixel[4] = {};
    const GLint row =
        static_cast<GLint>(impl_->windowHeight) - 1 - static_cast<GLint>(y);
    api.ReadPixels(
        static_cast<GLint>(x), row, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel
    );
    return static_cast<std::uint32_t>(pixel[0])
        | (static_cast<std::uint32_t>(pixel[1]) << 8)
        | (static_cast<std::uint32_t>(pixel[2]) << 16)
        | (static_cast<std::uint32_t>(pixel[3]) << 24);
}

MapInput MapRenderer::PollInput()
{
    MapInput input;
    if (window_ == nullptr)
    {
        input.quit = true;
        return input;
    }
    // Relative motion is accumulated from the events rather than differenced
    // from the cursor position: a difference misses everything that happened
    // between two frames, which is most of a fast drag.
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_EVENT_QUIT:
            input.quit = true;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            input.quit = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        {
            // The drawable size, not the window size: they differ on a
            // high-DPI display, and every pixel below -- the viewport, the
            // attachments, a pick -- is in drawable coordinates.
            const std::uint32_t width =
                static_cast<std::uint32_t>(event.window.data1);
            const std::uint32_t height =
                static_cast<std::uint32_t>(event.window.data2);
            Resize(width, height);
            input.resized = true;
            input.viewportWidth = width;
            input.viewportHeight = height;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                input.pressed = true;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if ((event.motion.state
                & (SDL_BUTTON_MMASK | SDL_BUTTON_RMASK)) != 0)
            {
                input.dragX += static_cast<std::int32_t>(event.motion.xrel);
                input.dragY += static_cast<std::int32_t>(event.motion.yrel);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            input.wheel += static_cast<double>(event.wheel.y);
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_ESCAPE)
            {
                input.quit = true;
            }
            else if (event.key.key == SDLK_SPACE)
            {
                input.step = true;
            }
            else if (event.key.key >= SDLK_0 && event.key.key <= SDLK_9)
            {
                // 1 is flat, 9 is very nearly a globe, 0 is the globe. A
                // continuous control still needs somewhere to jump to.
                const int digit = static_cast<int>(event.key.key - SDLK_0);
                input.bendPreset = digit == 0
                    ? 1.0
                    : static_cast<double>(digit - 1) / 8.0;
            }
            break;
        default:
            break;
        }
    }

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);
    input.mouseX = static_cast<std::int32_t>(mouseX);
    input.mouseY = static_cast<std::int32_t>(mouseY);

    // Held keys are read as state rather than collected as events: a viewer
    // wants "the camera is moving", not "a key repeated".
    if (!input.resized && impl_ != nullptr)
    {
        input.viewportWidth = impl_->windowWidth;
        input.viewportHeight = impl_->windowHeight;
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    if (keys != nullptr)
    {
        input.panX = (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D] ? 1 : 0)
            - (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A] ? 1 : 0);
        input.panY = (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S] ? 1 : 0)
            - (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W] ? 1 : 0);
        input.bend = (keys[SDL_SCANCODE_RIGHTBRACKET] ? 1 : 0)
            - (keys[SDL_SCANCODE_LEFTBRACKET] ? 1 : 0);
    }
    return input;
}

void MapRenderer::Present()
{
    if (window_ != nullptr)
    {
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(window_));
    }
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
