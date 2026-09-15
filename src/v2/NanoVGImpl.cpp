// Single translation unit that instantiates NanoVG's OpenGL 3 backend.
// GL entry points come from the vendored glad loader (external/glad), fed by
// glfwGetProcAddress in main(), so Linux and Windows share one path.
#include <glad/gl.h>

#include <nanovg.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg_gl.h>
#include <nanovg_gl_utils.h>
