// scalpel-editor OpenGL context via EGL for headless and Wayland targets.

#include "GlContext.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

namespace Scintilla::Internal {

namespace {

[[nodiscard]] std::string EglErrorHex() {
	const EGLint err = eglGetError();
	char buf[32];
	std::snprintf(buf, sizeof(buf), "0x%04x", static_cast<unsigned>(err));
	return buf;
}

[[nodiscard]] bool ClientExtensionPresent(const char *extensions, const char *name) {
	if (!extensions || !name) {
		return false;
	}
	const std::string haystack(extensions);
	const std::string needle(name);
	size_t pos = 0;
	while (pos < haystack.size()) {
		const size_t next = haystack.find(' ', pos);
		const size_t end = (next == std::string::npos) ? haystack.size() : next;
		if (haystack.compare(pos, end - pos, needle) == 0) {
			return true;
		}
		if (next == std::string::npos) {
			break;
		}
		pos = next + 1;
	}
	return false;
}

}  // namespace

GlContext::GlContext() {
	const char *clientExt = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (!ClientExtensionPresent(clientExt, "EGL_MESA_platform_surfaceless")) {
		throw std::runtime_error(
			"headless GL requires EGL_MESA_platform_surfaceless; "
			"client extensions do not list it");
	}
	if (!ClientExtensionPresent(clientExt, "EGL_EXT_platform_base") &&
		!ClientExtensionPresent(clientExt, "EGL_KHR_platform_base")) {
		// eglGetPlatformDisplay is core in EGL 1.5; EXT entry point is the common path.
	}

	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplayEXT =
		reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
			eglGetProcAddress("eglGetPlatformDisplayEXT"));
	EGLDisplay dpy = EGL_NO_DISPLAY;
	if (getPlatformDisplayEXT) {
		dpy = getPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
	}
	if (dpy == EGL_NO_DISPLAY) {
		// EGL 1.5 core entry (same platform enum).
		dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
	}
	if (dpy == EGL_NO_DISPLAY) {
		throw std::runtime_error(
			"eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA) failed (egl error " +
			EglErrorHex() + ")");
	}
	display = dpy;

	EGLint eglMajor = 0;
	EGLint eglMinor = 0;
	if (!eglInitialize(dpy, &eglMajor, &eglMinor)) {
		display = nullptr;
		throw std::runtime_error("eglInitialize failed (egl error " + EglErrorHex() + ")");
	}

	if (!eglBindAPI(EGL_OPENGL_API)) {
		Destroy();
		throw std::runtime_error(
			"eglBindAPI(EGL_OPENGL_API) failed (egl error " + EglErrorHex() + ")");
	}

	// Prefer a config that can also create a pbuffer if NO_SURFACE MakeCurrent fails.
	const EGLint configWithPbuffer[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_NONE,
	};
	const EGLint configAnySurface[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_NONE,
	};

	EGLConfig cfg{};
	EGLint numConfigs = 0;
	if (!eglChooseConfig(dpy, configWithPbuffer, &cfg, 1, &numConfigs) || numConfigs < 1) {
		numConfigs = 0;
		if (!eglChooseConfig(dpy, configAnySurface, &cfg, 1, &numConfigs) || numConfigs < 1) {
			Destroy();
			throw std::runtime_error(
				"eglChooseConfig for OpenGL RGBA8 failed (egl error " + EglErrorHex() + ")");
		}
	}

	const EGLint contextAttribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 3,
		EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
		EGL_NONE,
	};
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, contextAttribs);
	if (ctx == EGL_NO_CONTEXT) {
		Destroy();
		throw std::runtime_error(
			"eglCreateContext(OpenGL 3.3 core) failed (egl error " + EglErrorHex() + ")");
	}
	context = ctx;
	majorVersion = 3;
	minorVersion = 3;

	// Make current: prefer no EGL surface (FBO-only drawing).
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		const EGLint pbufferAttribs[] = {
			EGL_WIDTH, 1,
			EGL_HEIGHT, 1,
			EGL_NONE,
		};
		EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbufferAttribs);
		if (surf == EGL_NO_SURFACE) {
			Destroy();
			throw std::runtime_error(
				"EGL_NO_SURFACE MakeCurrent failed and eglCreatePbufferSurface failed "
				"(egl error " + EglErrorHex() + ")");
		}
		surface = surf;
		if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
			Destroy();
			throw std::runtime_error(
				"eglMakeCurrent with pbuffer failed (egl error " + EglErrorHex() + ")");
		}
	}

	try {
		ConfigureCurrentContext();
	} catch (...) {
		Destroy();
		throw;
	}
}

GlContext::GlContext(void *nativeDisplay, void *nativeWindow) {
	if (!nativeDisplay || !nativeWindow) {
		throw std::invalid_argument("window GlContext requires native display and window handles");
	}

	EGLDisplay dpy = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(nativeDisplay));
	if (dpy == EGL_NO_DISPLAY) {
		throw std::runtime_error("eglGetDisplay for Wayland failed (egl error " + EglErrorHex() + ")");
	}
	display = dpy;
	if (!eglInitialize(dpy, nullptr, nullptr)) {
		Destroy();
		throw std::runtime_error("eglInitialize for Wayland failed (egl error " + EglErrorHex() + ")");
	}
	if (!eglBindAPI(EGL_OPENGL_API)) {
		Destroy();
		throw std::runtime_error("eglBindAPI(EGL_OPENGL_API) failed (egl error " + EglErrorHex() + ")");
	}

	const EGLint configAttributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_NONE,
	};
	EGLConfig cfg{};
	EGLint configCount = 0;
	if (!eglChooseConfig(dpy, configAttributes, &cfg, 1, &configCount) || configCount < 1) {
		Destroy();
		throw std::runtime_error("eglChooseConfig for Wayland RGBA8 failed (egl error " + EglErrorHex() + ")");
	}

	const EGLint contextAttributes[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 3,
		EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
		EGL_NONE,
	};
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, contextAttributes);
	if (ctx == EGL_NO_CONTEXT) {
		Destroy();
		throw std::runtime_error("eglCreateContext for Wayland failed (egl error " + EglErrorHex() + ")");
	}
	context = ctx;
	majorVersion = 3;
	minorVersion = 3;
	// Retain the config so a second window surface can share the same context.
	eglConfig = cfg;

	EGLSurface window = eglCreateWindowSurface(dpy, cfg,
		reinterpret_cast<EGLNativeWindowType>(nativeWindow), nullptr);
	if (window == EGL_NO_SURFACE) {
		Destroy();
		throw std::runtime_error("eglCreateWindowSurface failed (egl error " + EglErrorHex() + ")");
	}
	surface = window;
	windowSurface = true;
	currentTarget = SurfaceTarget::Editor;
	const char *displayExtensions = eglQueryString(dpy, EGL_EXTENSIONS);
	bufferAgeSupported = ClientExtensionPresent(
		displayExtensions, "EGL_EXT_buffer_age");
	const char *damageFunction = nullptr;
	if (ClientExtensionPresent(
		displayExtensions, "EGL_KHR_swap_buffers_with_damage")) {
		damageFunction = "eglSwapBuffersWithDamageKHR";
	} else if (ClientExtensionPresent(
		displayExtensions, "EGL_EXT_swap_buffers_with_damage")) {
		damageFunction = "eglSwapBuffersWithDamageEXT";
	}
	if (damageFunction) {
		swapBuffersWithDamage = eglGetProcAddress(damageFunction);
	}
	if (!eglMakeCurrent(dpy, window, window, ctx)) {
		Destroy();
		throw std::runtime_error("eglMakeCurrent for Wayland failed (egl error " + EglErrorHex() + ")");
	}
	// Disabling synchronization is optional; keep the usable window context
	// when the EGL implementation only supports a non-zero interval.
	eglSwapInterval(dpy, 0);
	try {
		ConfigureCurrentContext();
	} catch (...) {
		Destroy();
		throw;
	}
}

void GlContext::ConfigureCurrentContext() {
	const char *glVersion = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	if (!glVersion) {
		throw std::runtime_error("glGetString(GL_VERSION) returned null after MakeCurrent");
	}
	// Require at least 3.3 for core shaders used by the renderer.
	int reportedMajor = 0;
	int reportedMinor = 0;
	if (std::sscanf(glVersion, "%d.%d", &reportedMajor, &reportedMinor) < 2 ||
		reportedMajor < 3 || (reportedMajor == 3 && reportedMinor < 3)) {
		throw std::runtime_error(
			std::string("OpenGL 3.3 required, got GL_VERSION=") + glVersion);
	}

	// Deterministic pixel contract for all drawing through this context.
	glDisable(GL_DITHER);
	glDisable(GL_MULTISAMPLE);
	// Framebuffer sRGB would change blend results; keep linear 8-bit storage.
#ifdef GL_FRAMEBUFFER_SRGB
	glDisable(GL_FRAMEBUFFER_SRGB);
#endif
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

GlContext::~GlContext() noexcept {
	Destroy();
}

void GlContext::Destroy() noexcept {
	EGLDisplay dpy = static_cast<EGLDisplay>(display);
	EGLContext ctx = static_cast<EGLContext>(context);
	EGLSurface surf = static_cast<EGLSurface>(surface);
	EGLSurface popup = static_cast<EGLSurface>(popupSurface);

	if (dpy != nullptr && dpy != EGL_NO_DISPLAY) {
		if (eglGetCurrentContext() == ctx) {
			eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		}
		if (popup != nullptr && popup != EGL_NO_SURFACE) {
			eglDestroySurface(dpy, popup);
		}
		if (surf != nullptr && surf != EGL_NO_SURFACE) {
			eglDestroySurface(dpy, surf);
		}
		if (ctx != nullptr && ctx != EGL_NO_CONTEXT) {
			eglDestroyContext(dpy, ctx);
		}
		eglTerminate(dpy);
	}
	display = nullptr;
	context = nullptr;
	surface = nullptr;
	popupSurface = nullptr;
	eglConfig = nullptr;
	windowSurface = false;
	bufferAgeSupported = false;
	currentTarget = SurfaceTarget::Editor;
	swapBuffersWithDamage = nullptr;
	majorVersion = 0;
	minorVersion = 0;
}

void *GlContext::SurfaceFor(SurfaceTarget target) const noexcept {
	switch (target) {
	case SurfaceTarget::Editor:
		return surface;
	case SurfaceTarget::Popup:
		return popupSurface;
	}
	return nullptr;
}

void GlContext::MakeCurrent() {
	MakeCurrent(SurfaceTarget::Editor);
}

void GlContext::MakeCurrent(SurfaceTarget target) {
	if (!display || !context) {
		throw std::runtime_error("GlContext::MakeCurrent on destroyed context");
	}
	void *chosen = SurfaceFor(target);
	if (target == SurfaceTarget::Popup && !chosen) {
		throw std::runtime_error("GlContext::MakeCurrent popup surface missing");
	}
	EGLDisplay dpy = static_cast<EGLDisplay>(display);
	EGLContext ctx = static_cast<EGLContext>(context);
	EGLSurface surf = chosen ? static_cast<EGLSurface>(chosen) : EGL_NO_SURFACE;
	if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
		throw std::runtime_error(
			"GlContext::MakeCurrent failed (egl error " + EglErrorHex() + ")");
	}
	currentTarget = target;
}

void GlContext::CreatePopupSurface(void *nativeWindow) {
	if (!windowSurface || !display || !context || !eglConfig) {
		throw std::runtime_error(
			"GlContext::CreatePopupSurface requires a window context");
	}
	if (!nativeWindow) {
		throw std::invalid_argument(
			"GlContext::CreatePopupSurface requires a native window");
	}
	if (popupSurface) {
		throw std::runtime_error("GlContext popup surface already exists");
	}
	EGLDisplay dpy = static_cast<EGLDisplay>(display);
	EGLConfig cfg = static_cast<EGLConfig>(eglConfig);
	EGLSurface popup = eglCreateWindowSurface(dpy, cfg,
		reinterpret_cast<EGLNativeWindowType>(nativeWindow), nullptr);
	if (popup == EGL_NO_SURFACE) {
		throw std::runtime_error(
			"eglCreateWindowSurface for popup failed (egl error " +
			EglErrorHex() + ")");
	}
	popupSurface = popup;
}

void GlContext::DestroyPopupSurface() noexcept {
	if (!display || !popupSurface) {
		return;
	}
	EGLDisplay dpy = static_cast<EGLDisplay>(display);
	EGLContext ctx = static_cast<EGLContext>(context);
	// Leave the popup current only after restoring the editor surface.
	if (eglGetCurrentContext() == ctx &&
		currentTarget == SurfaceTarget::Popup) {
		EGLSurface editor = surface ? static_cast<EGLSurface>(surface) :
			EGL_NO_SURFACE;
		if (editor != EGL_NO_SURFACE) {
			eglMakeCurrent(dpy, editor, editor, ctx);
			currentTarget = SurfaceTarget::Editor;
		} else {
			eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			currentTarget = SurfaceTarget::Editor;
		}
	}
	eglDestroySurface(dpy, static_cast<EGLSurface>(popupSurface));
	popupSurface = nullptr;
}

void GlContext::SwapBuffers() {
	void *chosen = SurfaceFor(currentTarget);
	if (!windowSurface || !display || !chosen) {
		throw std::runtime_error("GlContext::SwapBuffers requires a window surface");
	}
	if (!eglSwapBuffers(static_cast<EGLDisplay>(display),
		static_cast<EGLSurface>(chosen))) {
		throw std::runtime_error("eglSwapBuffers failed (egl error " + EglErrorHex() + ")");
	}
}

void GlContext::SwapBuffersWithDamage(
	const int *rectangles, std::size_t rectangleCount) {
	void *chosen = SurfaceFor(currentTarget);
	if (!windowSurface || !display || !chosen) {
		throw std::runtime_error(
			"GlContext::SwapBuffersWithDamage requires a window surface");
	}
	if (!swapBuffersWithDamage) {
		SwapBuffers();
		return;
	}
	if ((!rectangles && rectangleCount != 0) ||
		rectangleCount > static_cast<std::size_t>(
			std::numeric_limits<EGLint>::max() / 4)) {
		throw std::invalid_argument("invalid EGL damage rectangle list");
	}
	const auto damageSwap =
		reinterpret_cast<PFNEGLSWAPBUFFERSWITHDAMAGEKHRPROC>(
			swapBuffersWithDamage);
	if (!damageSwap(
		static_cast<EGLDisplay>(display), static_cast<EGLSurface>(chosen),
		rectangles,
		static_cast<EGLint>(rectangleCount))) {
		throw std::runtime_error(
			"eglSwapBuffersWithDamage failed (egl error " + EglErrorHex() + ")");
	}
}

int GlContext::BufferAge() const noexcept {
	return BufferAge(currentTarget);
}

int GlContext::BufferAge(SurfaceTarget target) const noexcept {
	void *chosen = SurfaceFor(target);
	if (!windowSurface || !display || !chosen || !bufferAgeSupported) {
		return 0;
	}
	EGLint age = 0;
	if (!eglQuerySurface(static_cast<EGLDisplay>(display),
		static_cast<EGLSurface>(chosen), EGL_BUFFER_AGE_EXT, &age)) {
		return 0;
	}
	return age;
}

void GlContext::ReleaseCurrent() noexcept {
	if (!display) {
		return;
	}
	EGLDisplay dpy = static_cast<EGLDisplay>(display);
	EGLContext ctx = static_cast<EGLContext>(context);
	if (eglGetCurrentContext() == ctx) {
		eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	}
}

bool GlContext::IsCurrent() const noexcept {
	if (!context) {
		return false;
	}
	return eglGetCurrentContext() == static_cast<EGLContext>(context);
}

std::string GlContext::VersionString() const {
	if (!IsCurrent()) {
		return {};
	}
	const char *s = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	return s ? std::string(s) : std::string{};
}

std::string GlContext::RendererString() const {
	if (!IsCurrent()) {
		return {};
	}
	const char *s = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
	return s ? std::string(s) : std::string{};
}

}
