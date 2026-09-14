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
 * Headless creation selects the Mesa software EGL device directly so tests do
 * not open a compositor or probe host GPUs. The context is made current with
 * EGL_NO_SURFACE when the implementation allows it; otherwise a 1x1 pbuffer is
 * used only to satisfy MakeCurrent. The window constructor instead makes an
 * EGL window surface current so the renderer can target its default
 * framebuffer.
 *
 * Destroy order: release current, destroy the EGL surface (if any), destroy
 * the context, then terminate the EGL display.
 *
 * Requested drawing state lives on Renderer. This object keeps a small cache
 * of the framebuffer, viewport, and scissor last applied on this GL context
 * so editor and popup renderers share one record. Framebuffer 0 is a real
 * binding; unknown state is stored separately from that name.
 */
class GlContext {
public:
	/**
	 * Create and initialize a headless GL 3.3 core context.
	 * Throws std::runtime_error with a clear message if Mesa's software EGL
	 * device or the required GL version is unavailable. Never falls back to a
	 * native display or hardware device.
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

	/**
	 * Make this context current on the calling thread (editor surface).
	 * Queries the actual EGL context and draw/read surfaces when tracking
	 * cannot prove they already match, and calls eglMakeCurrent only then.
	 */
	void MakeCurrent();
	/**
	 * Make this context current on the named surface target.
	 * Destroyed contexts and missing popup surfaces are rejected before any
	 * fast return. Tracking is updated only after a successful bind.
	 */
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
	/**
	 * True when this context and the named draw/read surfaces are actually
	 * current. IsCurrent and CurrentTarget are not enough on their own.
	 */
	[[nodiscard]] bool SurfacesCurrent(SurfaceTarget target) const noexcept;
	[[nodiscard]] bool HasWindowSurface() const noexcept { return windowSurface; }
	[[nodiscard]] SurfaceTarget CurrentTarget() const noexcept {
		return currentTarget;
	}

	/**
	 * Context last successfully made current on this thread, or null.
	 * Colour-buffer helpers use this to update the applied-state cache.
	 */
	[[nodiscard]] static GlContext *CurrentOnThread() noexcept;

	struct DrawSetupCounts {
		size_t contextRequested = 0;
		size_t contextEmitted = 0;
		size_t framebufferRequested = 0;
		size_t framebufferEmitted = 0;
		size_t viewportRequested = 0;
		size_t viewportEmitted = 0;
		size_t scissorRequested = 0;
		size_t scissorEmitted = 0;
	};
	[[nodiscard]] const DrawSetupCounts &SetupCounts() const noexcept {
		return setupCounts;
	}
	void ResetSetupCounts() noexcept { setupCounts = {}; }

	/**
	 * Bind a draw/read framebuffer if it is not already the applied binding.
	 * The context must be current. Framebuffer 0 is the default framebuffer.
	 */
	void BindDrawFramebuffer(unsigned framebuffer);
	/**
	 * Set the 0,0,width,height viewport when the physical size changes.
	 * The context must be current.
	 */
	void SetDrawViewport(int width, int height);
	/**
	 * Enable or disable the scissor test and set its box when that applied
	 * state differs. The context must be current.
	 */
	void SetDrawScissor(bool enabled, int x, int y, int width, int height);
	/** Mark framebuffer, viewport, and scissor as unknown. */
	void InvalidateAppliedDrawState() noexcept;
	/**
	 * If the applied framebuffer is known and equals this name, forget it.
	 * Used when deleting a framebuffer that GL may unbind or reuse.
	 */
	void InvalidateAppliedFramebuffer(unsigned framebuffer) noexcept;
	/**
	 * Restore incoming draw/read bindings after a temporary framebuffer
	 * operation and record the result in the applied-state cache. Distinct
	 * draw and read names leave the framebuffer binding unknown.
	 */
	void RestoreFramebufferBindings(int draw, int read);

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
	void AdoptCurrent(SurfaceTarget target, bool invalidateViewport) noexcept;
	void DropCurrent() noexcept;

	struct AppliedDrawState {
		bool framebufferKnown = false;
		unsigned framebuffer = 0;
		bool viewportKnown = false;
		int viewportWidth = 0;
		int viewportHeight = 0;
		bool scissorKnown = false;
		bool scissorEnabled = false;
		int scissorX = 0;
		int scissorY = 0;
		int scissorWidth = 0;
		int scissorHeight = 0;
	};

	void *display = nullptr;   // EGLDisplay
	void *context = nullptr;   // EGLContext
	void *surface = nullptr;   // Editor EGLSurface or null for EGL_NO_SURFACE
	void *popupSurface = nullptr; // Optional second window surface
	void *eglConfig = nullptr; // EGLConfig retained for popup surfaces
	bool windowSurface = false;
	bool bufferAgeSupported = false;
	SurfaceTarget currentTarget = SurfaceTarget::Editor;
	AppliedDrawState applied;
	DrawSetupCounts setupCounts;
	void (*swapBuffersWithDamage)() = nullptr;
	int majorVersion = 0;
	int minorVersion = 0;
};

}

#endif
