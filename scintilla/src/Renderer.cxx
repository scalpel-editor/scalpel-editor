// scalpel-editor OpenGL drawing implementation.

#include "Renderer.h"

#include <algorithm>
#include <cmath>
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

[[nodiscard]] GLuint CompileShader(GLenum type, const char *source) {
	const GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		GLsizei len = 0;
		glGetShaderInfoLog(shader, sizeof(log), &len, log);
		glDeleteShader(shader);
		throw std::runtime_error(std::string("shader compile failed: ") + log);
	}
	return shader;
}

[[nodiscard]] GLuint LinkProgram(GLuint vs, GLuint fs) {
	const GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	GLint ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		GLsizei len = 0;
		glGetProgramInfoLog(program, sizeof(log), &len, log);
		glDeleteProgram(program);
		throw std::runtime_error(std::string("program link failed: ") + log);
	}
	return program;
}

// Top-left origin, y down → NDC (y up). Column-major mat4.
void OrthoTopLeft(float width, float height, float out[16]) {
	// x_ndc = 2*x/w - 1
	// y_ndc = 1 - 2*y/h
	std::memset(out, 0, 16 * sizeof(float));
	out[0] = 2.0f / width;
	out[5] = -2.0f / height;
	out[10] = 1.0f;
	out[12] = -1.0f;
	out[13] = 1.0f;
	out[15] = 1.0f;
}

const char *kSolidVert = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uTransform;
void main() {
	gl_Position = uTransform * vec4(aPos, 0.0, 1.0);
}
)";

const char *kSolidFrag = R"(#version 330 core
uniform vec4 uColour;
out vec4 fragColour;
void main() {
	fragColour = uColour;
}
)";

}  // namespace

PixelRect PixelRectFromPRectangle(PRectangle rc) noexcept {
	PixelRect pr;
	pr.left = static_cast<int>(std::floor(static_cast<double>(rc.left)));
	pr.top = static_cast<int>(std::floor(static_cast<double>(rc.top)));
	pr.right = static_cast<int>(std::ceil(static_cast<double>(rc.right)));
	pr.bottom = static_cast<int>(std::ceil(static_cast<double>(rc.bottom)));
	return pr;
}

PixelRect IntersectPixelRect(PixelRect a, PixelRect b) noexcept {
	PixelRect r;
	r.left = std::max(a.left, b.left);
	r.top = std::max(a.top, b.top);
	r.right = std::min(a.right, b.right);
	r.bottom = std::min(a.bottom, b.bottom);
	if (r.Empty()) {
		return {};
	}
	return r;
}

ColourBuffer::~ColourBuffer() noexcept {
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
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	EnsureSolidProgram();
}

Renderer::~Renderer() noexcept {
	try {
		MakeCurrent();
	} catch (...) {
		return;
	}
	DestroyGl();
}

void Renderer::DestroyGl() noexcept {
	if (vbo != 0) {
		GLuint name = vbo;
		glDeleteBuffers(1, &name);
		vbo = 0;
	}
	if (vao != 0) {
		GLuint name = vao;
		glDeleteVertexArrays(1, &name);
		vao = 0;
	}
	if (programSolid != 0) {
		glDeleteProgram(programSolid);
		programSolid = 0;
	}
	uniformTransform = -1;
	uniformColour = -1;
}

void Renderer::EnsureSolidProgram() {
	if (programSolid != 0) {
		return;
	}
	const GLuint vs = CompileShader(GL_VERTEX_SHADER, kSolidVert);
	const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kSolidFrag);
	programSolid = LinkProgram(vs, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);
	uniformTransform = glGetUniformLocation(programSolid, "uTransform");
	uniformColour = glGetUniformLocation(programSolid, "uColour");

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	// 6 vertices (two triangles) × vec2; updated per draw.
	glBufferData(GL_ARRAY_BUFFER, 6 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Renderer::MakeCurrent() {
	context.MakeCurrent();
}

void Renderer::SetDrawTarget(unsigned framebuffer, int width, int height) {
	if (width <= 0 || height <= 0) {
		throw std::runtime_error("Renderer::SetDrawTarget requires positive size");
	}
	MakeCurrent();
	const bool changed = targetFbo != framebuffer || targetWidth != width || targetHeight != height;
	if (changed) {
		clipStack.clear();
	}
	targetFbo = framebuffer;
	targetWidth = width;
	targetHeight = height;
	BindCurrentTarget();
}

void Renderer::BindCurrentTarget() {
	MakeCurrent();
	glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
	glViewport(0, 0, targetWidth, targetHeight);
	ApplyScissor();
}

PixelRect Renderer::CurrentClip() const noexcept {
	if (clipStack.empty()) {
		return PixelRect{0, 0, targetWidth, targetHeight};
	}
	return clipStack.back();
}

void Renderer::ApplyScissor() const {
	const PixelRect clip = CurrentClip();
	if (clip.Empty() || targetWidth <= 0 || targetHeight <= 0) {
		glDisable(GL_SCISSOR_TEST);
		// Empty clip: leave scissor disabled and draws must no-op themselves.
		return;
	}
	// OpenGL scissor origin is bottom-left; our rect is top-down half-open.
	const GLint glX = clip.left;
	const GLint glY = targetHeight - clip.bottom;
	const GLsizei glW = clip.Width();
	const GLsizei glH = clip.Height();
	glEnable(GL_SCISSOR_TEST);
	glScissor(glX, glY, glW, glH);
}

void Renderer::SetClip(PRectangle rc) {
	MakeCurrent();
	glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
	PixelRect next = PixelRectFromPRectangle(rc);
	next = IntersectPixelRect(next, CurrentClip());
	// Also clamp to target.
	next = IntersectPixelRect(next, PixelRect{0, 0, targetWidth, targetHeight});
	clipStack.push_back(next);
	ApplyScissor();
}

void Renderer::PopClip() {
	if (clipStack.empty()) {
		return;
	}
	MakeCurrent();
	glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
	clipStack.pop_back();
	ApplyScissor();
}

void Renderer::Clear(ColourRGBA colour) {
	MakeCurrent();
	glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
	glViewport(0, 0, targetWidth, targetHeight);
	// Full-target clear ignores the clip stack.
	glDisable(GL_SCISSOR_TEST);
	glClearColor(
		colour.GetRedComponent(),
		colour.GetGreenComponent(),
		colour.GetBlueComponent(),
		colour.GetAlphaComponent());
	glClear(GL_COLOR_BUFFER_BIT);
	ApplyScissor();
}

void Renderer::UploadProjection() const {
	float mat[16];
	OrthoTopLeft(static_cast<float>(targetWidth), static_cast<float>(targetHeight), mat);
	glUniformMatrix4fv(uniformTransform, 1, GL_FALSE, mat);
}

void Renderer::DrawSolidQuad(float x0, float y0, float x1, float y1, ColourRGBA colour) {
	EnsureSolidProgram();
	const float verts[12] = {
		x0, y0, x1, y0, x1, y1,
		x0, y0, x1, y1, x0, y1,
	};
	glUseProgram(programSolid);
	UploadProjection();
	glUniform4f(uniformColour,
		colour.GetRedComponent(),
		colour.GetGreenComponent(),
		colour.GetBlueComponent(),
		colour.GetAlphaComponent());
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glUseProgram(0);
}

void Renderer::FillRectangleOpaque(PRectangle rc, ColourRGBA colour) {
	MakeCurrent();
	glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
	glViewport(0, 0, targetWidth, targetHeight);

	PixelRect pr = IntersectPixelRect(PixelRectFromPRectangle(rc), CurrentClip());
	if (pr.Empty()) {
		return;
	}
	// Exact opaque fill via scissored clear.
	glEnable(GL_SCISSOR_TEST);
	glScissor(pr.left, targetHeight - pr.bottom, pr.Width(), pr.Height());
	glClearColor(
		colour.GetRedComponent(),
		colour.GetGreenComponent(),
		colour.GetBlueComponent(),
		colour.GetAlphaComponent());
	glClear(GL_COLOR_BUFFER_BIT);
	ApplyScissor();
}

void Renderer::FillRectangle(PRectangle rc, ColourRGBA colour) {
	if (colour.IsOpaque()) {
		FillRectangleOpaque(rc, colour);
		return;
	}
	MakeCurrent();
	glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
	glViewport(0, 0, targetWidth, targetHeight);
	ApplyScissor();
	if (CurrentClip().Empty()) {
		return;
	}
	glEnable(GL_BLEND);
	DrawSolidQuad(rc.left, rc.top, rc.right, rc.bottom, colour);
	glDisable(GL_BLEND);
}

}
