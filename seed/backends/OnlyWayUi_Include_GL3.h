#ifndef OWUI_INCLUDE_GL3_H
#define OWUI_INCLUDE_GL3_H

#include <EGL/egl.h>
#include <GL/glcorearb.h>

#include <cstdio>

#define OWUI_GL3_MAKE_VERSION(major, minor) ((major) * 10000 + (minor))
#define OWUI_GL3_VERSION_MAJOR(version) ((version) / 10000)
#define OWUI_GL3_VERSION_MINOR(version) ((version) % 10000)

namespace OwuiGL3Loader {

static constexpr int MinimumVersion = OWUI_GL3_MAKE_VERSION(3, 3);

int Load();
void Unload();

} // namespace OwuiGL3Loader

#define OWUI_GL3_FUNCTIONS \
	OWUI_GL3_FUNCTION(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
	OWUI_GL3_FUNCTION(PFNGLATTACHSHADERPROC, glAttachShader) \
	OWUI_GL3_FUNCTION(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) \
	OWUI_GL3_FUNCTION(PFNGLBINDBUFFERPROC, glBindBuffer) \
	OWUI_GL3_FUNCTION(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
	OWUI_GL3_FUNCTION(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) \
	OWUI_GL3_FUNCTION(PFNGLBINDTEXTUREPROC, glBindTexture) \
	OWUI_GL3_FUNCTION(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
	OWUI_GL3_FUNCTION(PFNGLBLENDCOLORPROC, glBlendColor) \
	OWUI_GL3_FUNCTION(PFNGLBLENDEQUATIONPROC, glBlendEquation) \
	OWUI_GL3_FUNCTION(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) \
	OWUI_GL3_FUNCTION(PFNGLBLENDFUNCPROC, glBlendFunc) \
	OWUI_GL3_FUNCTION(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
	OWUI_GL3_FUNCTION(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) \
	OWUI_GL3_FUNCTION(PFNGLBUFFERDATAPROC, glBufferData) \
	OWUI_GL3_FUNCTION(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
	OWUI_GL3_FUNCTION(PFNGLCLEARPROC, glClear) \
	OWUI_GL3_FUNCTION(PFNGLCLEARCOLORPROC, glClearColor) \
	OWUI_GL3_FUNCTION(PFNGLCLEARSTENCILPROC, glClearStencil) \
	OWUI_GL3_FUNCTION(PFNGLCOLORMASKPROC, glColorMask) \
	OWUI_GL3_FUNCTION(PFNGLCOMPILESHADERPROC, glCompileShader) \
	OWUI_GL3_FUNCTION(PFNGLCOPYTEXSUBIMAGE2DPROC, glCopyTexSubImage2D) \
	OWUI_GL3_FUNCTION(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
	OWUI_GL3_FUNCTION(PFNGLCREATESHADERPROC, glCreateShader) \
	OWUI_GL3_FUNCTION(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
	OWUI_GL3_FUNCTION(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
	OWUI_GL3_FUNCTION(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
	OWUI_GL3_FUNCTION(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
	OWUI_GL3_FUNCTION(PFNGLDELETESHADERPROC, glDeleteShader) \
	OWUI_GL3_FUNCTION(PFNGLDELETETEXTURESPROC, glDeleteTextures) \
	OWUI_GL3_FUNCTION(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) \
	OWUI_GL3_FUNCTION(PFNGLDETACHSHADERPROC, glDetachShader) \
	OWUI_GL3_FUNCTION(PFNGLDISABLEPROC, glDisable) \
	OWUI_GL3_FUNCTION(PFNGLDRAWELEMENTSPROC, glDrawElements) \
	OWUI_GL3_FUNCTION(PFNGLENABLEPROC, glEnable) \
	OWUI_GL3_FUNCTION(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
	OWUI_GL3_FUNCTION(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
	OWUI_GL3_FUNCTION(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
	OWUI_GL3_FUNCTION(PFNGLGENBUFFERSPROC, glGenBuffers) \
	OWUI_GL3_FUNCTION(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
	OWUI_GL3_FUNCTION(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) \
	OWUI_GL3_FUNCTION(PFNGLGENTEXTURESPROC, glGenTextures) \
	OWUI_GL3_FUNCTION(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
	OWUI_GL3_FUNCTION(PFNGLGETACTIVEUNIFORMPROC, glGetActiveUniform) \
	OWUI_GL3_FUNCTION(PFNGLGETBOOLEANVPROC, glGetBooleanv) \
	OWUI_GL3_FUNCTION(PFNGLGETERRORPROC, glGetError) \
	OWUI_GL3_FUNCTION(PFNGLGETFLOATVPROC, glGetFloatv) \
	OWUI_GL3_FUNCTION(PFNGLGETINTEGERVPROC, glGetIntegerv) \
	OWUI_GL3_FUNCTION(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
	OWUI_GL3_FUNCTION(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
	OWUI_GL3_FUNCTION(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
	OWUI_GL3_FUNCTION(PFNGLGETSHADERIVPROC, glGetShaderiv) \
	OWUI_GL3_FUNCTION(PFNGLGETSTRINGPROC, glGetString) \
	OWUI_GL3_FUNCTION(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
	OWUI_GL3_FUNCTION(PFNGLISENABLEDPROC, glIsEnabled) \
	OWUI_GL3_FUNCTION(PFNGLLINKPROGRAMPROC, glLinkProgram) \
	OWUI_GL3_FUNCTION(PFNGLREADPIXELSPROC, glReadPixels) \
	OWUI_GL3_FUNCTION(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
	OWUI_GL3_FUNCTION(PFNGLSCISSORPROC, glScissor) \
	OWUI_GL3_FUNCTION(PFNGLSHADERSOURCEPROC, glShaderSource) \
	OWUI_GL3_FUNCTION(PFNGLSTENCILFUNCPROC, glStencilFunc) \
	OWUI_GL3_FUNCTION(PFNGLSTENCILFUNCSEPARATEPROC, glStencilFuncSeparate) \
	OWUI_GL3_FUNCTION(PFNGLSTENCILMASKPROC, glStencilMask) \
	OWUI_GL3_FUNCTION(PFNGLSTENCILMASKSEPARATEPROC, glStencilMaskSeparate) \
	OWUI_GL3_FUNCTION(PFNGLSTENCILOPPROC, glStencilOp) \
	OWUI_GL3_FUNCTION(PFNGLSTENCILOPSEPARATEPROC, glStencilOpSeparate) \
	OWUI_GL3_FUNCTION(PFNGLTEXIMAGE2DPROC, glTexImage2D) \
	OWUI_GL3_FUNCTION(PFNGLTEXPARAMETERFVPROC, glTexParameterfv) \
	OWUI_GL3_FUNCTION(PFNGLTEXPARAMETERIPROC, glTexParameteri) \
	OWUI_GL3_FUNCTION(PFNGLUNIFORM1FPROC, glUniform1f) \
	OWUI_GL3_FUNCTION(PFNGLUNIFORM1FVPROC, glUniform1fv) \
	OWUI_GL3_FUNCTION(PFNGLUNIFORM1IPROC, glUniform1i) \
	OWUI_GL3_FUNCTION(PFNGLUNIFORM2FPROC, glUniform2f) \
	OWUI_GL3_FUNCTION(PFNGLUNIFORM2FVPROC, glUniform2fv) \
	OWUI_GL3_FUNCTION(PFNGLUNIFORM4FVPROC, glUniform4fv) \
	OWUI_GL3_FUNCTION(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv) \
	OWUI_GL3_FUNCTION(PFNGLUSEPROGRAMPROC, glUseProgram) \
	OWUI_GL3_FUNCTION(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
	OWUI_GL3_FUNCTION(PFNGLVIEWPORTPROC, glViewport)

#define OWUI_GL3_FUNCTION(type, name) extern type owui_##name;
OWUI_GL3_FUNCTIONS
#undef OWUI_GL3_FUNCTION

#define glActiveTexture owui_glActiveTexture
#define glAttachShader owui_glAttachShader
#define glBindAttribLocation owui_glBindAttribLocation
#define glBindBuffer owui_glBindBuffer
#define glBindFramebuffer owui_glBindFramebuffer
#define glBindRenderbuffer owui_glBindRenderbuffer
#define glBindTexture owui_glBindTexture
#define glBindVertexArray owui_glBindVertexArray
#define glBlendColor owui_glBlendColor
#define glBlendEquation owui_glBlendEquation
#define glBlendEquationSeparate owui_glBlendEquationSeparate
#define glBlendFunc owui_glBlendFunc
#define glBlendFuncSeparate owui_glBlendFuncSeparate
#define glBlitFramebuffer owui_glBlitFramebuffer
#define glBufferData owui_glBufferData
#define glCheckFramebufferStatus owui_glCheckFramebufferStatus
#define glClear owui_glClear
#define glClearColor owui_glClearColor
#define glClearStencil owui_glClearStencil
#define glColorMask owui_glColorMask
#define glCompileShader owui_glCompileShader
#define glCopyTexSubImage2D owui_glCopyTexSubImage2D
#define glCreateProgram owui_glCreateProgram
#define glCreateShader owui_glCreateShader
#define glDeleteBuffers owui_glDeleteBuffers
#define glDeleteFramebuffers owui_glDeleteFramebuffers
#define glDeleteProgram owui_glDeleteProgram
#define glDeleteRenderbuffers owui_glDeleteRenderbuffers
#define glDeleteShader owui_glDeleteShader
#define glDeleteTextures owui_glDeleteTextures
#define glDeleteVertexArrays owui_glDeleteVertexArrays
#define glDetachShader owui_glDetachShader
#define glDisable owui_glDisable
#define glDrawElements owui_glDrawElements
#define glEnable owui_glEnable
#define glEnableVertexAttribArray owui_glEnableVertexAttribArray
#define glFramebufferRenderbuffer owui_glFramebufferRenderbuffer
#define glFramebufferTexture2D owui_glFramebufferTexture2D
#define glGenBuffers owui_glGenBuffers
#define glGenFramebuffers owui_glGenFramebuffers
#define glGenRenderbuffers owui_glGenRenderbuffers
#define glGenTextures owui_glGenTextures
#define glGenVertexArrays owui_glGenVertexArrays
#define glGetActiveUniform owui_glGetActiveUniform
#define glGetBooleanv owui_glGetBooleanv
#define glGetError owui_glGetError
#define glGetFloatv owui_glGetFloatv
#define glGetIntegerv owui_glGetIntegerv
#define glGetProgramInfoLog owui_glGetProgramInfoLog
#define glGetProgramiv owui_glGetProgramiv
#define glGetShaderInfoLog owui_glGetShaderInfoLog
#define glGetShaderiv owui_glGetShaderiv
#define glGetString owui_glGetString
#define glGetUniformLocation owui_glGetUniformLocation
#define glIsEnabled owui_glIsEnabled
#define glLinkProgram owui_glLinkProgram
#define glReadPixels owui_glReadPixels
#define glRenderbufferStorageMultisample owui_glRenderbufferStorageMultisample
#define glScissor owui_glScissor
#define glShaderSource owui_glShaderSource
#define glStencilFunc owui_glStencilFunc
#define glStencilFuncSeparate owui_glStencilFuncSeparate
#define glStencilMask owui_glStencilMask
#define glStencilMaskSeparate owui_glStencilMaskSeparate
#define glStencilOp owui_glStencilOp
#define glStencilOpSeparate owui_glStencilOpSeparate
#define glTexImage2D owui_glTexImage2D
#define glTexParameterfv owui_glTexParameterfv
#define glTexParameteri owui_glTexParameteri
#define glUniform1f owui_glUniform1f
#define glUniform1fv owui_glUniform1fv
#define glUniform1i owui_glUniform1i
#define glUniform2f owui_glUniform2f
#define glUniform2fv owui_glUniform2fv
#define glUniform4fv owui_glUniform4fv
#define glUniformMatrix4fv owui_glUniformMatrix4fv
#define glUseProgram owui_glUseProgram
#define glVertexAttribPointer owui_glVertexAttribPointer
#define glViewport owui_glViewport

#ifdef OWUI_GL3_LOADER_IMPLEMENTATION

#define OWUI_GL3_FUNCTION(type, name) type owui_##name = nullptr;
OWUI_GL3_FUNCTIONS
#undef OWUI_GL3_FUNCTION

namespace OwuiGL3Loader {
namespace {

template <typename FunctionType>
bool LoadFunction(FunctionType& function, const char* name)
{
	function = reinterpret_cast<FunctionType>(eglGetProcAddress(name));
	return function != nullptr;
}

void ClearFunctions()
{
#define OWUI_GL3_FUNCTION(type, name) owui_##name = nullptr;
	OWUI_GL3_FUNCTIONS
#undef OWUI_GL3_FUNCTION
}

int ReadVersion()
{
	const auto* version_string = reinterpret_cast<const char*>(glGetString(GL_VERSION));
	if (!version_string)
		return 0;

	int major = 0;
	int minor = 0;
	if (std::sscanf(version_string, "%d.%d", &major, &minor) != 2)
		return 0;

	return OWUI_GL3_MAKE_VERSION(major, minor);
}

} // namespace

int Load()
{
	bool loaded = true;

#define OWUI_GL3_FUNCTION(type, name) loaded = LoadFunction(owui_##name, #name) && loaded;
	OWUI_GL3_FUNCTIONS
#undef OWUI_GL3_FUNCTION

	if (!loaded)
	{
		ClearFunctions();
		return 0;
	}

	const int version = ReadVersion();
	if (version < MinimumVersion)
	{
		ClearFunctions();
		return 0;
	}

	return version;
}

void Unload()
{
	ClearFunctions();
}

} // namespace OwuiGL3Loader

#endif

#endif
