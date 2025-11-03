/*
 * GLAD - OpenGL Loader-Generator
 * Implementation file
 */

#include <stddef.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOCOMM

// Suppress APIENTRY redefinition warning before including headers
#pragma warning(push)
#pragma warning(disable: 4005)

#include <glad/glad.h>
#include <Windows.h>

// Restore warning level
#pragma warning(pop)

/* Function pointers */
PFNGLCLEARCOLORPROC glClearColor = NULL;
PFNGLCLEARPROC glClear = NULL;
PFNGLVIEWPORTPROC glViewport = NULL;
PFNGLENABLEPROC glEnable = NULL;
PFNGLDISABLEPROC glDisable = NULL;
PFNGLBLENDFUNCPROC glBlendFunc = NULL;
PFNGLSCISSORPROC glScissor = NULL;
PFNGLDRAWELEMENTSPROC glDrawElements = NULL;

PFNGLCREATESHADERPROC glCreateShader = NULL;
PFNGLSHADERSOURCEPROC glShaderSource = NULL;
PFNGLCOMPILESHADERPROC glCompileShader = NULL;
PFNGLGETSHADERIVPROC glGetShaderiv = NULL;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = NULL;
PFNGLDELETESHADERPROC glDeleteShader = NULL;
PFNGLCREATEPROGRAMPROC glCreateProgram = NULL;
PFNGLATTACHSHADERPROC glAttachShader = NULL;
PFNGLLINKPROGRAMPROC glLinkProgram = NULL;
PFNGLGETPROGRAMIVPROC glGetProgramiv = NULL;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = NULL;
PFNGLDELETEPROGRAMPROC glDeleteProgram = NULL;
PFNGLUSEPROGRAMPROC glUseProgram = NULL;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
PFNGLUNIFORM1FPROC glUniform1f = NULL;
PFNGLUNIFORM1IPROC glUniform1i = NULL;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = NULL;

PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = NULL;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = NULL;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = NULL;
PFNGLGENBUFFERSPROC glGenBuffers = NULL;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = NULL;
PFNGLBINDBUFFERPROC glBindBuffer = NULL;
PFNGLBUFFERDATAPROC glBufferData = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = NULL;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;

PFNGLGENTEXTURESPROC glGenTextures = NULL;
PFNGLDELETETEXTURESPROC glDeleteTextures = NULL;
PFNGLBINDTEXTUREPROC glBindTexture = NULL;
PFNGLTEXIMAGE2DPROC glTexImage2D = NULL;
PFNGLTEXPARAMETERIPROC glTexParameteri = NULL;
PFNGLACTIVETEXTUREPROC glActiveTexture = NULL;
PFNGLPIXELSTOREIPROC glPixelStorei = NULL;

static void* get_proc(const char* name) {
    void* proc = (void*)wglGetProcAddress(name);
    if (!proc) {
        HMODULE module = LoadLibraryA("opengl32.dll");
        proc = (void*)GetProcAddress(module, name);
    }
    return proc;
}

int gladLoadGL(void) {
    /* Load OpenGL 1.1 functions */
    glClearColor = (PFNGLCLEARCOLORPROC)get_proc("glClearColor");
    glClear = (PFNGLCLEARPROC)get_proc("glClear");
    glViewport = (PFNGLVIEWPORTPROC)get_proc("glViewport");
    glEnable = (PFNGLENABLEPROC)get_proc("glEnable");
    glDisable = (PFNGLDISABLEPROC)get_proc("glDisable");
    glBlendFunc = (PFNGLBLENDFUNCPROC)get_proc("glBlendFunc");
    glScissor = (PFNGLSCISSORPROC)get_proc("glScissor");
    glDrawElements = (PFNGLDRAWELEMENTSPROC)get_proc("glDrawElements");
    
    /* Load OpenGL 2.0+ functions */
    glCreateShader = (PFNGLCREATESHADERPROC)get_proc("glCreateShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC)get_proc("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC)get_proc("glCompileShader");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)get_proc("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)get_proc("glGetShaderInfoLog");
    glDeleteShader = (PFNGLDELETESHADERPROC)get_proc("glDeleteShader");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC)get_proc("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)get_proc("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)get_proc("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)get_proc("glGetProgramiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)get_proc("glGetProgramInfoLog");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC)get_proc("glDeleteProgram");
    glUseProgram = (PFNGLUSEPROGRAMPROC)get_proc("glUseProgram");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)get_proc("glGetUniformLocation");
    glUniform1f = (PFNGLUNIFORM1FPROC)get_proc("glUniform1f");
    glUniform1i = (PFNGLUNIFORM1IPROC)get_proc("glUniform1i");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)get_proc("glUniformMatrix4fv");
    
    /* Load OpenGL 3.0+ functions */
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)get_proc("glGenVertexArrays");
    glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)get_proc("glDeleteVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)get_proc("glBindVertexArray");
    glGenBuffers = (PFNGLGENBUFFERSPROC)get_proc("glGenBuffers");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)get_proc("glDeleteBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC)get_proc("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC)get_proc("glBufferData");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)get_proc("glEnableVertexAttribArray");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)get_proc("glVertexAttribPointer");
    
    /* Load texture functions */
    glGenTextures = (PFNGLGENTEXTURESPROC)get_proc("glGenTextures");
    glDeleteTextures = (PFNGLDELETETEXTURESPROC)get_proc("glDeleteTextures");
    glBindTexture = (PFNGLBINDTEXTUREPROC)get_proc("glBindTexture");
    glTexImage2D = (PFNGLTEXIMAGE2DPROC)get_proc("glTexImage2D");
    glTexParameteri = (PFNGLTEXPARAMETERIPROC)get_proc("glTexParameteri");
    glActiveTexture = (PFNGLACTIVETEXTUREPROC)get_proc("glActiveTexture");
    glPixelStorei = (PFNGLPIXELSTOREIPROC)get_proc("glPixelStorei");
    
    /* Check if essential functions loaded */
    if (!glCreateShader || !glGenVertexArrays) {
        return 0; /* Failed */
    }
    
    return 1; /* Success */
}
