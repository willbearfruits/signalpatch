// Single translation unit that instantiates NanoVG's OpenGL 3 backend.
// Mesa/glvnd export the core profile entry points directly, so with
// GL_GLEXT_PROTOTYPES no loader library is needed on Linux.
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#include <nanovg.h>
#define NANOVG_GL3_IMPLEMENTATION
#include <nanovg_gl.h>
#include <nanovg_gl_utils.h>
