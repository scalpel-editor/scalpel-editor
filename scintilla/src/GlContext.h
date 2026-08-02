// scalpel-editor OpenGL context via EGL for headless and Wayland targets.
//
// Offscreen tests and application hosts each own their own GlContext. The same
// Renderer implementation is constructed per context; GL objects are never
// shared across contexts.

#ifndef GLCONTEXT_H
#define GLCONTEXT_H

#include <cstddef>
#include <string>

namespace Scintilla::Internal {

/**
 * Desktop OpenGL context for the offscreen test path or a native window.
 *
 * Creation prefers the Mesa surfaceless EGL platform so tests do not open a
 * compositor. The context is made current with EGL_NO_SURFACE when the
 * implementation allows it; otherwise a 1x1 pbuffer is used only to satisfy
 * MakeCurrent. The window constructor instead makes an EGL window surface
 * current so the renderer can target its default framebuffer.
 *
 * Destroy order: release current, destroy the EGL surface (if any), destroy
 * the context, then terminate the EGL display.
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
	/** Create a GL 3.3 context and EGL window surface for native handles. */
	GlContext(void *nativeDisplay, void *nativeWindow);
	~GlContext() noexcept;

	GlContext(const GlContext &) = delete;
	GlContext(GlContext &&) = delete;
	GlContext &operator=(const GlContext &) = delete;
	GlContext &operator=(GlContext &&) = delete;

	/**
	 * Which window surface MakeCurrent / SwapBuffers / BufferAge target.
	 * Editor is the surface created with the window constructor; Popup is an
	 * optional second surface created with CreatePopupSurface.
	 */
	enum class SurfaceTarget {
		Editor,
		Popup,
	};

	/** Make this context current on the calling thread (editor surface). */
	void MakeCurrent();
	/** Make this context current on the named surface target. */
	void MakeCurrent(SurfaceTarget target);

	/** Detach the current context if it is this one. */
	void ReleaseCurrent() noexcept;

	/**
	 * Create a second EGL window surface for a popup wl_egl_window. Fails if
	 * a popup surface already exists or the context is headless. Does not
	 * change which surface is current.
	 */
	void CreatePopupSurface(void *nativeWindow);
	/** Destroy the popup EGL surface if present; restores editor as current. */
	void DestroyPopupSurface() noexcept;
	[[nodiscard]] bool HasPopupSurface() const noexcept {
		return popupSurface != nullptr;
	}

	/** Submit the currently selected window surface. Throws for headless. */
	void SwapBuffers();
	/** Submit only the supplied bottom-left-origin EGL damage rectangles. */
	void SwapBuffersWithDamage(const int *rectangles, std::size_t rectangleCount);
	[[nodiscard]] bool BufferAgeSupported() const noexcept {
		return bufferAgeSupported;
	}
	[[nodiscard]] bool DamageSwapSupported() const noexcept {
		return swapBuffersWithDamage != nullptr;
	}
	/** Return zero when buffer age is unavailable or cannot be queried. */
	[[nodiscard]] int BufferAge() const noexcept;
	/**
	 * Buffer age for a specific surface without changing the current target.
	 * Returns zero when unsupported or the surface is missing.
	 */
	[[nodiscard]] int BufferAge(SurfaceTarget target) const noexcept;

	[[nodiscard]] bool IsCurrent() const noexcept;
	[[nodiscard]] bool HasWindowSurface() const noexcept { return windowSurface; }
	[[nodiscard]] SurfaceTarget CurrentTarget() const noexcept {
		return currentTarget;
	}

	/** GL_VERSION string while current; empty if not current. */
	[[nodiscard]] std::string VersionString() const;

	/** GL_RENDERER string while current; empty if not current. */
	[[nodiscard]] std::string RendererString() const;

	/** Major/minor from the context attributes (requested 3.3 core). */
	[[nodiscard]] int MajorVersion() const noexcept { return majorVersion; }
	[[nodiscard]] int MinorVersion() const noexcept { return minorVersion; }

private:
	void ConfigureCurrentContext();
	void Destroy() noexcept;
	[[nodiscard]] void *SurfaceFor(SurfaceTarget target) const noexcept;

	void *display = nullptr;   // EGLDisplay
	void *context = nullptr;   // EGLContext
	void *surface = nullptr;   // Editor EGLSurface or null for EGL_NO_SURFACE
	void *popupSurface = nullptr; // Optional second window surface
	void *eglConfig = nullptr; // EGLConfig retained for popup surfaces
	bool windowSurface = false;
	bool bufferAgeSupported = false;
	SurfaceTarget currentTarget = SurfaceTarget::Editor;
	void (*swapBuffersWithDamage)() = nullptr;
	int majorVersion = 0;
	int minorVersion = 0;
};

}

#endif
