// scalpel-editor headless OpenGL context via EGL (no Wayland/X11 display).
//
// Offscreen tests and application hosts each own their own GlContext. The same
// Renderer implementation is constructed per context; GL objects are never
// shared across contexts.

#ifndef GLCONTEXT_H
#define GLCONTEXT_H

#include <string>

namespace Scintilla::Internal {

/**
 * Desktop OpenGL context for drawing without a window system surface.
 *
 * Creation prefers the Mesa surfaceless EGL platform so tests do not open a
 * compositor. The context is made current with EGL_NO_SURFACE when the
 * implementation allows it; otherwise a 1x1 pbuffer is used only to satisfy
 * MakeCurrent. Drawing targets are FBOs owned by the renderer/surfaces, not
 * the EGL surface.
 *
 * Destroy order: release current, destroy pbuffer (if any), destroy context,
 * terminate display.
 */
class GlContext {
public:
	/**
	 * Create and initialize a headless GL 3.3 core context.
	 * Throws std::runtime_error with a clear message if headless EGL or the
	 * required GL version is unavailable. Never falls back to
	 * eglGetDisplay(EGL_DEFAULT_DISPLAY).
	 */
	GlContext();
	~GlContext() noexcept;

	GlContext(const GlContext &) = delete;
	GlContext(GlContext &&) = delete;
	GlContext &operator=(const GlContext &) = delete;
	GlContext &operator=(GlContext &&) = delete;

	/** Make this context current on the calling thread. */
	void MakeCurrent();

	/** Detach the current context if it is this one. */
	void ReleaseCurrent() noexcept;

	[[nodiscard]] bool IsCurrent() const noexcept;

	/** GL_VERSION string while current; empty if not current. */
	[[nodiscard]] std::string VersionString() const;

	/** GL_RENDERER string while current; empty if not current. */
	[[nodiscard]] std::string RendererString() const;

	/** Major/minor from the context attributes (requested 3.3 core). */
	[[nodiscard]] int MajorVersion() const noexcept { return majorVersion; }
	[[nodiscard]] int MinorVersion() const noexcept { return minorVersion; }

private:
	void Destroy() noexcept;

	void *display = nullptr;   // EGLDisplay
	void *context = nullptr;   // EGLContext
	void *pbuffer = nullptr;   // EGLSurface or null for EGL_NO_SURFACE path
	int majorVersion = 0;
	int minorVersion = 0;
};

}

#endif
