// scalpel-editor OpenGL drawing implementation.

#include "Renderer.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

namespace Scintilla::Internal {

namespace {

[[nodiscard]] std::string GlErrorHex() {
	const GLenum err = glGetError();
	char buf[32];
	std::snprintf(buf, sizeof(buf), "0x%04x", static_cast<unsigned>(err));
	return buf;
}

}  // namespace

ColourBuffer::~ColourBuffer() noexcept {
	// Destructor cannot require a current context; callers must Destroy() while current.
	// If objects remain, leak avoidance best-effort only when a context is current.
	if (fbo != 0 || texture != 0) {
		Destroy();
	}
}

void ColourBuffer::Destroy() noexcept {
	if (fbo != 0) {
		GLuint name = fbo;
		glDeleteFramebuffers(1, &name);
		fbo = 0;
	}
	if (texture != 0) {
		GLuint name = texture;
		glDeleteTextures(1, &name);
		texture = 0;
	}
	width = 0;
	height = 0;
}

void ColourBuffer::Resize(int width_, int height_) {
	if (width_ <= 0 || height_ <= 0) {
		throw std::runtime_error("ColourBuffer::Resize requires positive size");
	}
	Destroy();

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	GLuint fb = 0;
	glGenFramebuffers(1, &fb);
	glBindFramebuffer(GL_FRAMEBUFFER, fb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (status != GL_FRAMEBUFFER_COMPLETE) {
		glDeleteFramebuffers(1, &fb);
		glDeleteTextures(1, &tex);
		throw std::runtime_error(
			"ColourBuffer FBO incomplete (status " + std::to_string(status) +
			", gl error " + GlErrorHex() + ")");
	}

	texture = tex;
	fbo = fb;
	width = width_;
	height = height_;
}

std::vector<uint8_t> ColourBuffer::ReadPixelsTopDown() const {
	if (!Valid()) {
		throw std::runtime_error("ColourBuffer::ReadPixelsTopDown on empty buffer");
	}
	const size_t rowBytes = static_cast<size_t>(width) * 4u;
	const size_t total = rowBytes * static_cast<size_t>(height);
	std::vector<uint8_t> bottomUp(total);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// glReadPixels origin is bottom-left; public buffers are top-to-bottom.
	std::vector<uint8_t> topDown(total);
	for (int y = 0; y < height; y++) {
		const uint8_t *src = bottomUp.data() + static_cast<size_t>(height - 1 - y) * rowBytes;
		uint8_t *dst = topDown.data() + static_cast<size_t>(y) * rowBytes;
		std::memcpy(dst, src, rowBytes);
	}
	return topDown;
}

ColourRGBA ColourBuffer::ReadPixel(int x, int y) const {
	if (!Valid() || x < 0 || y < 0 || x >= width || y >= height) {
		throw std::runtime_error("ColourBuffer::ReadPixel out of range");
	}
	// Convert top-down (x,y) to GL bottom-up row for a one-pixel read.
	const int glY = height - 1 - y;
	uint8_t px[4] = {};
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(x, glY, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return ColourRGBA(px[0], px[1], px[2], px[3]);
}

Renderer::Renderer(GlContext &context_) : context(context_) {
	context.MakeCurrent();
	// Default blend function documented in Renderer.h; disabled until alpha draws.
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
}

Renderer::~Renderer() noexcept = default;

void Renderer::MakeCurrent() {
	context.MakeCurrent();
}

void Renderer::SetDrawTarget(unsigned framebuffer, int width, int height) {
	if (width <= 0 || height <= 0) {
		throw std::runtime_error("Renderer::SetDrawTarget requires positive size");
	}
	MakeCurrent();
	targetFbo = framebuffer;
	targetWidth = width;
	targetHeight = height;
	glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
	glViewport(0, 0, targetWidth, targetHeight);
	// Top-left origin for Scintilla: orthographic projection is installed with
	// geometry draws. Clear does not need the projection.
}

void Renderer::Clear(ColourRGBA colour) {
	MakeCurrent();
	glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
	glViewport(0, 0, targetWidth, targetHeight);
	glDisable(GL_SCISSOR_TEST);
	glClearColor(
		colour.GetRedComponent(),
		colour.GetGreenComponent(),
		colour.GetBlueComponent(),
		colour.GetAlphaComponent());
	glClear(GL_COLOR_BUFFER_BIT);
}

}
