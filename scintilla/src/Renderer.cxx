// scalpel-editor OpenGL drawing implementation.

#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
	fragColour = vec4(uColour.rgb * uColour.a, uColour.a);
}
)";

const char *kTextureVert = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uTransform;
out vec2 vUV;
void main() {
	gl_Position = uTransform * vec4(aPos, 0.0, 1.0);
	vUV = aUV;
}
)";

const char *kTextureFrag = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform bool uStraightAlpha;
// Straight RGBA modulate (glyphs: white coverage texture * fore colour).
uniform vec4 uTexColour;
out vec4 fragColour;
void main() {
	fragColour = texture(uTex, vUV) * uTexColour;
	if (uStraightAlpha) {
		fragColour.rgb *= fragColour.a;
	}
}
)";

// Linear gradient along a line from uStart to uEnd in surface pixel space.
// Stop positions in [0,1]; colours are straight RGBA. Linear mix between stops.
const char *kGradientVert = R"(#version 330 core
layout(location = 0) in vec2 aPos;
uniform mat4 uTransform;
out vec2 vPos;
void main() {
	gl_Position = uTransform * vec4(aPos, 0.0, 1.0);
	vPos = aPos;
}
)";

const char *kGradientFrag = R"(#version 330 core
in vec2 vPos;
uniform vec2 uStart;
uniform vec2 uEnd;
uniform int uStopCount;
uniform float uStops[8];
uniform vec4 uColours[8];
out vec4 fragColour;
vec4 Premultiply(vec4 colour) {
	return vec4(colour.rgb * colour.a, colour.a);
}
void main() {
	vec2 d = uEnd - uStart;
	float len2 = dot(d, d);
	float t = 0.0;
	if (len2 > 1e-6) {
		t = clamp(dot(vPos - uStart, d) / len2, 0.0, 1.0);
	}
	if (uStopCount <= 0) {
		fragColour = vec4(0.0);
		return;
	}
	if (uStopCount == 1 || t <= uStops[0]) {
		fragColour = Premultiply(uColours[0]);
		return;
	}
	for (int i = 1; i < 8; ++i) {
		if (i >= uStopCount) {
			fragColour = Premultiply(uColours[uStopCount - 1]);
			return;
		}
		if (t <= uStops[i]) {
			float span = uStops[i] - uStops[i - 1];
			float u = span > 1e-6 ? (t - uStops[i - 1]) / span : 0.0;
			fragColour = Premultiply(mix(uColours[i - 1], uColours[i], u));
			return;
		}
	}
	fragColour = Premultiply(uColours[uStopCount - 1]);
}
)";

void UnpremultiplyPixel(uint8_t *pixel) noexcept {
	const unsigned alpha = pixel[3];
	if (alpha == 0) {
		pixel[0] = 0;
		pixel[1] = 0;
		pixel[2] = 0;
		return;
	}
	for (size_t channel = 0; channel < 3; channel++) {
		const unsigned straight = (static_cast<unsigned>(pixel[channel]) * 255u + alpha / 2u) / alpha;
		pixel[channel] = static_cast<uint8_t>(std::min(straight, 255u));
	}
}

[[nodiscard]] double Cross(Point a, Point b, Point c) noexcept {
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

[[nodiscard]] double PolygonAreaTwice(const std::vector<Point> &points) noexcept {
	double area = 0.0;
	for (size_t i = 0; i < points.size(); i++) {
		const Point a = points[i];
		const Point b = points[(i + 1) % points.size()];
		area += a.x * b.y - b.x * a.y;
	}
	return area;
}

[[nodiscard]] bool PointStrictlyInsideTriangle(Point p, Point a, Point b, Point c,
	double winding) noexcept {
	constexpr double epsilon = 1.0e-8;
	return winding * Cross(a, b, p) > epsilon &&
		winding * Cross(b, c, p) > epsilon &&
		winding * Cross(c, a, p) > epsilon;
}

[[nodiscard]] std::vector<float> TriangulatePolygon(const Point *pts, size_t npts) {
	std::vector<Point> points;
	points.reserve(npts);
	for (size_t i = 0; i < npts; i++) {
		if (points.empty() || pts[i].x != points.back().x || pts[i].y != points.back().y) {
			points.push_back(pts[i]);
		}
	}
	if (points.size() > 1 && points.front().x == points.back().x &&
		points.front().y == points.back().y) {
		points.pop_back();
	}
	if (points.size() < 3) {
		return {};
	}

	const double area = PolygonAreaTwice(points);
	if (std::abs(area) < 1.0e-8) {
		return {};
	}
	const double winding = area > 0.0 ? 1.0 : -1.0;
	std::vector<size_t> remaining(points.size());
	for (size_t i = 0; i < remaining.size(); i++) {
		remaining[i] = i;
	}

	std::vector<float> triangles;
	triangles.reserve((points.size() - 2) * 6);
	while (remaining.size() > 3) {
		bool clipped = false;
		for (size_t i = 0; i < remaining.size(); i++) {
			const size_t previous = remaining[(i + remaining.size() - 1) % remaining.size()];
			const size_t current = remaining[i];
			const size_t next = remaining[(i + 1) % remaining.size()];
			const double corner = Cross(points[previous], points[current], points[next]);
			if (std::abs(corner) <= 1.0e-8) {
				remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
				clipped = true;
				break;
			}
			if (winding * corner < 0.0) {
				continue;
			}

			bool containsPoint = false;
			for (const size_t candidate : remaining) {
				if (candidate != previous && candidate != current && candidate != next &&
					PointStrictlyInsideTriangle(points[candidate], points[previous],
						points[current], points[next], winding)) {
					containsPoint = true;
					break;
				}
			}
			if (containsPoint) {
				continue;
			}

			for (const size_t index : {previous, current, next}) {
				triangles.push_back(points[index].x);
				triangles.push_back(points[index].y);
			}
			remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
			clipped = true;
			break;
		}
		if (!clipped) {
			throw std::runtime_error("Polygon contains crossing or unusable edges");
		}
	}
	for (const size_t index : remaining) {
		triangles.push_back(points[index].x);
		triangles.push_back(points[index].y);
	}
	return triangles;
}

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
	for (size_t i = 0; i < total; i += 4) {
		UnpremultiplyPixel(bottomUp.data() + i);
	}

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
	UnpremultiplyPixel(px);
	return ColourRGBA(px[0], px[1], px[2], px[3]);
}

Renderer::Renderer(GlContext &context_) : context(context_) {
	context.MakeCurrent();
	// Premultiplied source-over: rgba = source + destination * (1-source alpha).
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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

void Renderer::ClearGlyphCache() noexcept {
	for (auto &entry : glyphCache) {
		if (entry.second.texture != 0) {
			GLuint name = entry.second.texture;
			glDeleteTextures(1, &name);
			entry.second.texture = 0;
		}
	}
	glyphCache.clear();
}

void Renderer::RetireScaleDependentGlyphCache() noexcept {
	for (auto it = glyphCache.begin(); it != glyphCache.end();) {
		if (it->first.face->UsesBitmapStrike()) {
			++it;
			continue;
		}
		if (it->second.texture != 0) {
			GLuint name = it->second.texture;
			glDeleteTextures(1, &name);
			it->second.texture = 0;
		}
		it = glyphCache.erase(it);
	}
}

void Renderer::DestroyGl() noexcept {
	ClearGlyphCache();
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
	if (programTexture != 0) {
		glDeleteProgram(programTexture);
		programTexture = 0;
	}
	if (programGradient != 0) {
		glDeleteProgram(programGradient);
		programGradient = 0;
	}
	uniformTransform = -1;
	uniformColour = -1;
	uniformTexTransform = -1;
	uniformTexSampler = -1;
	uniformTexStraightAlpha = -1;
	uniformTexModulate = -1;
	uniformGradTransform = -1;
	uniformGradStart = -1;
	uniformGradEnd = -1;
	uniformGradStopCount = -1;
	uniformGradStops = -1;
	uniformGradColours = -1;
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
	// Large enough for tessellated ellipses/stadiums or textured quads (pos+uv).
	glBufferData(GL_ARRAY_BUFFER, 512 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	// Location 1 enabled only for texture draws (stride set there).
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Renderer::EnsureTextureProgram() {
	if (programTexture != 0) {
		return;
	}
	const GLuint vs = CompileShader(GL_VERTEX_SHADER, kTextureVert);
	const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kTextureFrag);
	programTexture = LinkProgram(vs, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);
	uniformTexTransform = glGetUniformLocation(programTexture, "uTransform");
	uniformTexSampler = glGetUniformLocation(programTexture, "uTex");
	uniformTexStraightAlpha = glGetUniformLocation(programTexture, "uStraightAlpha");
	uniformTexModulate = glGetUniformLocation(programTexture, "uTexColour");
}

void Renderer::EnsureGradientProgram() {
	if (programGradient != 0) {
		return;
	}
	const GLuint vs = CompileShader(GL_VERTEX_SHADER, kGradientVert);
	const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kGradientFrag);
	programGradient = LinkProgram(vs, fs);
	glDeleteShader(vs);
	glDeleteShader(fs);
	uniformGradTransform = glGetUniformLocation(programGradient, "uTransform");
	uniformGradStart = glGetUniformLocation(programGradient, "uStart");
	uniformGradEnd = glGetUniformLocation(programGradient, "uEnd");
	uniformGradStopCount = glGetUniformLocation(programGradient, "uStopCount");
	uniformGradStops = glGetUniformLocation(programGradient, "uStops");
	uniformGradColours = glGetUniformLocation(programGradient, "uColours");
}

void Renderer::MakeCurrent() {
	context.MakeCurrent();
}

void Renderer::SetDrawTarget(unsigned framebuffer, int width, int height) {
	SetDrawTarget(framebuffer, width, height, width, height);
}

void Renderer::SetDrawTarget(unsigned framebuffer, int bufferWidth, int bufferHeight,
	int logicalWidth, int logicalHeight) {
	if (bufferWidth <= 0 || bufferHeight <= 0 ||
		logicalWidth <= 0 || logicalHeight <= 0) {
		throw std::runtime_error("Renderer::SetDrawTarget requires positive size");
	}
	MakeCurrent();
	const bool changed = targetFbo != framebuffer ||
		targetWidth != bufferWidth || targetHeight != bufferHeight ||
		targetLogicalWidth != logicalWidth || targetLogicalHeight != logicalHeight;
	if (changed) {
		clipStack.clear();
	}
	targetFbo = framebuffer;
	targetWidth = bufferWidth;
	targetHeight = bufferHeight;
	targetLogicalWidth = logicalWidth;
	targetLogicalHeight = logicalHeight;
	BindCurrentTarget();
}

void Renderer::SetOutputRasterScale(RasterScale rasterScale) {
	if (targetRasterScale == rasterScale) {
		return;
	}
	MakeCurrent();
	// Outline masks were rasterized for the previous nominal scale. Fixed
	// bitmap strikes use logical placement and remain valid across scales.
	RetireScaleDependentGlyphCache();
	targetRasterScale = rasterScale;
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

PixelRect Renderer::LogicalPixelRect(PRectangle rc) const noexcept {
	if (targetLogicalWidth <= 0 || targetLogicalHeight <= 0) {
		return {};
	}
	return {
		static_cast<int>(std::floor(
			rc.left * targetWidth / targetLogicalWidth)),
		static_cast<int>(std::floor(
			rc.top * targetHeight / targetLogicalHeight)),
		static_cast<int>(std::ceil(
			rc.right * targetWidth / targetLogicalWidth)),
		static_cast<int>(std::ceil(
			rc.bottom * targetHeight / targetLogicalHeight)),
	};
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
	PixelRect next = LogicalPixelRect(rc);
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
		colour.GetRedComponent() * colour.GetAlphaComponent(),
		colour.GetGreenComponent() * colour.GetAlphaComponent(),
		colour.GetBlueComponent() * colour.GetAlphaComponent(),
		colour.GetAlphaComponent());
	glClear(GL_COLOR_BUFFER_BIT);
	ApplyScissor();
}

void Renderer::UploadProjection() const {
	float mat[16];
	OrthoTopLeft(static_cast<float>(targetLogicalWidth),
		static_cast<float>(targetLogicalHeight), mat);
	glUniformMatrix4fv(uniformTransform, 1, GL_FALSE, mat);
}

void Renderer::BeginDraw() {
	MakeCurrent();
	glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
	glViewport(0, 0, targetWidth, targetHeight);
	ApplyScissor();
}

void Renderer::SetBlendForColour(ColourRGBA colour) {
	if (colour.IsOpaque()) {
		glDisable(GL_BLEND);
	} else {
		glEnable(GL_BLEND);
	}
}

void Renderer::DrawSolidTriangles(const float *xy, size_t vertexCount, ColourRGBA colour) {
	if (!xy || vertexCount < 3) {
		return;
	}
	EnsureSolidProgram();
	const size_t floats = vertexCount * 2;
	if (floats > 512 * 2) {
		throw std::runtime_error("DrawSolidTriangles vertex count exceeds buffer");
	}
	glUseProgram(programSolid);
	UploadProjection();
	glUniform4f(uniformColour,
		colour.GetRedComponent(),
		colour.GetGreenComponent(),
		colour.GetBlueComponent(),
		colour.GetAlphaComponent());
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, floats * sizeof(float), xy);
	glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCount));
	glBindVertexArray(0);
	glUseProgram(0);
}

void Renderer::DrawSolidQuad(float x0, float y0, float x1, float y1, ColourRGBA colour) {
	const float verts[12] = {
		x0, y0, x1, y0, x1, y1,
		x0, y0, x1, y1, x0, y1,
	};
	DrawSolidTriangles(verts, 6, colour);
}

void Renderer::DrawLineSegment(Point start, Point end, XYPOSITION width, ColourRGBA colour) {
	const double dx = end.x - start.x;
	const double dy = end.y - start.y;
	const double len = std::sqrt(dx * dx + dy * dy);
	if (len <= 0.0) {
		// Degenerate: draw a small square around the point.
		const double h = std::max(width, 1.0) * 0.5;
		DrawSolidQuad(static_cast<float>(start.x - h), static_cast<float>(start.y - h),
			static_cast<float>(start.x + h), static_cast<float>(start.y + h), colour);
		return;
	}
	const double half = width * 0.5;
	const double tx = dx / len * half;
	const double ty = dy / len * half;
	const double nx = -dy / len * half;
	const double ny = dx / len * half;
	const float verts[12] = {
		static_cast<float>(start.x - tx + nx), static_cast<float>(start.y - ty + ny),
		static_cast<float>(start.x - tx - nx), static_cast<float>(start.y - ty - ny),
		static_cast<float>(end.x + tx - nx), static_cast<float>(end.y + ty - ny),
		static_cast<float>(start.x - tx + nx), static_cast<float>(start.y - ty + ny),
		static_cast<float>(end.x + tx - nx), static_cast<float>(end.y + ty - ny),
		static_cast<float>(end.x + tx + nx), static_cast<float>(end.y + ty + ny),
	};
	DrawSolidTriangles(verts, 6, colour);
}

void Renderer::FillRectangleOpaque(PRectangle rc, ColourRGBA colour) {
	BeginDraw();

	PixelRect pr = IntersectPixelRect(LogicalPixelRect(rc), CurrentClip());
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
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	glEnable(GL_BLEND);
	DrawSolidQuad(rc.left, rc.top, rc.right, rc.bottom, colour);
	glDisable(GL_BLEND);
}

void Renderer::LineDraw(Point start, Point end, Stroke stroke) {
	if (stroke.width <= 0.0) {
		return;
	}
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	SetBlendForColour(stroke.colour);
	DrawLineSegment(start, end, stroke.width, stroke.colour);
	glDisable(GL_BLEND);
}

void Renderer::PolyLine(const Point *pts, size_t npts, Stroke stroke) {
	if (!pts || npts < 2 || stroke.width <= 0.0) {
		return;
	}
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	SetBlendForColour(stroke.colour);
	for (size_t i = 1; i < npts; i++) {
		DrawLineSegment(pts[i - 1], pts[i], stroke.width, stroke.colour);
	}
	glDisable(GL_BLEND);
}

void Renderer::Polygon(const Point *pts, size_t npts, FillStroke fillStroke) {
	if (!pts || npts < 3) {
		return;
	}
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	const std::vector<float> tris = TriangulatePolygon(pts, npts);
	SetBlendForColour(fillStroke.fill.colour);
	DrawSolidTriangles(tris.data(), tris.size() / 2, fillStroke.fill.colour);
	if (fillStroke.stroke.width > 0.0) {
		SetBlendForColour(fillStroke.stroke.colour);
		for (size_t i = 0; i < npts; i++) {
			DrawLineSegment(pts[i], pts[(i + 1) % npts], fillStroke.stroke.width, fillStroke.stroke.colour);
		}
	}
	glDisable(GL_BLEND);
}

void Renderer::RectangleDraw(PRectangle rc, FillStroke fillStroke) {
	FillRectangle(rc, fillStroke.fill.colour);
	RectangleFrame(rc, fillStroke.stroke);
}

void Renderer::RectangleFrame(PRectangle rc, Stroke stroke) {
	const Point pts[5] = {
		Point(rc.left, rc.top),
		Point(rc.right, rc.top),
		Point(rc.right, rc.bottom),
		Point(rc.left, rc.bottom),
		Point(rc.left, rc.top),
	};
	PolyLine(pts, 5, stroke);
}

void Renderer::RoundedRectangle(PRectangle rc, FillStroke fillStroke, XYPOSITION radius) {
	AlphaRectangle(rc, radius, fillStroke);
}

void Renderer::AlphaRectangle(PRectangle rc, XYPOSITION cornerSize, FillStroke fillStroke) {
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	const XYPOSITION w = rc.Width();
	const XYPOSITION h = rc.Height();
	if (w <= 0.0 || h <= 0.0) {
		return;
	}
	XYPOSITION r = std::max<XYPOSITION>(0.0, cornerSize);
	r = std::min<XYPOSITION>(r, std::min(w, h) * 0.5);

	if (r <= 0.5) {
		FillRectangle(rc, fillStroke.fill.colour);
		if (fillStroke.stroke.width > 0.0) {
			RectangleFrame(rc, fillStroke.stroke);
		}
		return;
	}

	// Tessellate rounded rect as centre box + side boxes + corner quarter-circles.
	constexpr int kSeg = 8;
	std::vector<float> tris;
	auto addTri = [&](double ax, double ay, double bx, double by, double cx, double cy) {
		tris.push_back(static_cast<float>(ax)); tris.push_back(static_cast<float>(ay));
		tris.push_back(static_cast<float>(bx)); tris.push_back(static_cast<float>(by));
		tris.push_back(static_cast<float>(cx)); tris.push_back(static_cast<float>(cy));
	};
	const double x0 = rc.left;
	const double y0 = rc.top;
	const double x1 = rc.right;
	const double y1 = rc.bottom;
	// Centre and sides (axis-aligned).
	addTri(x0 + r, y0, x1 - r, y0, x1 - r, y1);
	addTri(x0 + r, y0, x1 - r, y1, x0 + r, y1);
	addTri(x0, y0 + r, x0 + r, y0 + r, x0 + r, y1 - r);
	addTri(x0, y0 + r, x0 + r, y1 - r, x0, y1 - r);
	addTri(x1 - r, y0 + r, x1, y0 + r, x1, y1 - r);
	addTri(x1 - r, y0 + r, x1, y1 - r, x1 - r, y1 - r);

	auto corner = [&](double cx, double cy, double a0, double a1) {
		for (int i = 0; i < kSeg; i++) {
			const double t0 = a0 + (a1 - a0) * (static_cast<double>(i) / kSeg);
			const double t1 = a0 + (a1 - a0) * (static_cast<double>(i + 1) / kSeg);
			addTri(cx, cy,
				cx + std::cos(t0) * r, cy + std::sin(t0) * r,
				cx + std::cos(t1) * r, cy + std::sin(t1) * r);
		}
	};
	// Angles in surface space (y down): top-left, top-right, bottom-right, bottom-left.
	constexpr double pi = 3.141592653589793;
	corner(x0 + r, y0 + r, pi, pi * 1.5);
	corner(x1 - r, y0 + r, pi * 1.5, pi * 2.0);
	corner(x1 - r, y1 - r, 0.0, pi * 0.5);
	corner(x0 + r, y1 - r, pi * 0.5, pi);

	SetBlendForColour(fillStroke.fill.colour);
	DrawSolidTriangles(tris.data(), tris.size() / 2, fillStroke.fill.colour);
	if (fillStroke.stroke.width > 0.0) {
		// Approximate stroke with the outer polyline of the rounded rect.
		std::vector<Point> outline;
		outline.reserve(kSeg * 4 + 4);
		auto arc = [&](double cx, double cy, double a0, double a1) {
			for (int i = 0; i <= kSeg; i++) {
				const double t = a0 + (a1 - a0) * (static_cast<double>(i) / kSeg);
				outline.push_back(Point(cx + std::cos(t) * r, cy + std::sin(t) * r));
			}
		};
		arc(x0 + r, y0 + r, pi, pi * 1.5);
		arc(x1 - r, y0 + r, pi * 1.5, pi * 2.0);
		arc(x1 - r, y1 - r, 0.0, pi * 0.5);
		arc(x0 + r, y1 - r, pi * 0.5, pi);
		SetBlendForColour(fillStroke.stroke.colour);
		for (size_t i = 0; i < outline.size(); i++) {
			DrawLineSegment(outline[i], outline[(i + 1) % outline.size()],
				fillStroke.stroke.width, fillStroke.stroke.colour);
		}
	}
	glDisable(GL_BLEND);
}

void Renderer::DrawEllipse(PRectangle rc, ColourRGBA fill, ColourRGBA stroke, XYPOSITION strokeWidth,
	bool doFill, bool doStroke) {
	const double cx = (rc.left + rc.right) * 0.5;
	const double cy = (rc.top + rc.bottom) * 0.5;
	const double rx = rc.Width() * 0.5;
	const double ry = rc.Height() * 0.5;
	if (rx <= 0.0 || ry <= 0.0) {
		return;
	}
	constexpr int kSeg = 48;
	constexpr double pi2 = 6.283185307179586;
	if (doFill) {
		std::vector<float> tris;
		tris.reserve(static_cast<size_t>(kSeg) * 6);
		for (int i = 0; i < kSeg; i++) {
			const double t0 = pi2 * (static_cast<double>(i) / kSeg);
			const double t1 = pi2 * (static_cast<double>(i + 1) / kSeg);
			tris.push_back(static_cast<float>(cx));
			tris.push_back(static_cast<float>(cy));
			tris.push_back(static_cast<float>(cx + std::cos(t0) * rx));
			tris.push_back(static_cast<float>(cy + std::sin(t0) * ry));
			tris.push_back(static_cast<float>(cx + std::cos(t1) * rx));
			tris.push_back(static_cast<float>(cy + std::sin(t1) * ry));
		}
		SetBlendForColour(fill);
		DrawSolidTriangles(tris.data(), tris.size() / 2, fill);
	}
	if (doStroke && strokeWidth > 0.0) {
		SetBlendForColour(stroke);
		Point prev(cx + rx, cy);
		for (int i = 1; i <= kSeg; i++) {
			const double t = pi2 * (static_cast<double>(i) / kSeg);
			Point cur(cx + std::cos(t) * rx, cy + std::sin(t) * ry);
			DrawLineSegment(prev, cur, strokeWidth, stroke);
			prev = cur;
		}
	}
	glDisable(GL_BLEND);
}

void Renderer::Ellipse(PRectangle rc, FillStroke fillStroke) {
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	DrawEllipse(rc, fillStroke.fill.colour, fillStroke.stroke.colour, fillStroke.stroke.width, true, true);
}

void Renderer::Stadium(PRectangle rc, FillStroke fillStroke, int ends) {
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	if (rc.Width() <= 0.0 || rc.Height() <= 0.0) {
		return;
	}
	// Surface::Ends: low nibble left, high nibble right. semiCircles = 0.
	const int leftEnd = ends & 0xf;
	const int rightEnd = (ends >> 4) & 0xf;
	const XYPOSITION r = std::min(rc.Height(), rc.Width()) * 0.5;
	const XYPOSITION midY = (rc.top + rc.bottom) * 0.5;
	const bool leftRound = leftEnd == 0;
	const bool rightRound = rightEnd == 0;
	const bool leftAngle = leftEnd == 2;
	const bool rightAngle = rightEnd == 2;
	const XYPOSITION bodyLeft = (leftRound || leftAngle) ? rc.left + r : rc.left;
	const XYPOSITION bodyRight = (rightRound || rightAngle) ? rc.right - r : rc.right;

	std::vector<Point> outline;
	outline.emplace_back(bodyLeft, rc.top);
	outline.emplace_back(bodyRight, rc.top);
	constexpr int kSeg = 16;
	constexpr double pi = 3.141592653589793;
	if (rightRound) {
		for (int i = 1; i <= kSeg; i++) {
			const double angle = -pi * 0.5 + pi * static_cast<double>(i) / kSeg;
			outline.emplace_back(bodyRight + std::cos(angle) * r,
				midY + std::sin(angle) * r);
		}
	} else if (rightAngle) {
		outline.emplace_back(rc.right, midY);
		outline.emplace_back(bodyRight, rc.bottom);
	} else {
		outline.emplace_back(bodyRight, rc.bottom);
	}
	outline.emplace_back(bodyLeft, rc.bottom);
	if (leftRound) {
		for (int i = 1; i <= kSeg; i++) {
			const double angle = pi * 0.5 + pi * static_cast<double>(i) / kSeg;
			outline.emplace_back(bodyLeft + std::cos(angle) * r,
				midY + std::sin(angle) * r);
		}
	} else if (leftAngle) {
		outline.emplace_back(rc.left, midY);
	}
	Polygon(outline.data(), outline.size(), fillStroke);
}

namespace {

/** Split a logical coordinate through exact scale into floor device pixel + 26.6 phase. */
void LogicalToDeviceOriginAndPhase(XYPOSITION logical, RasterScale scale,
	int &originOut, int32_t &phaseOut) noexcept {
	const double device =
		logical * static_cast<double>(scale.Numerator()) /
		static_cast<double>(scale.Denominator());
	const double floorDevice = std::floor(device);
	originOut = static_cast<int>(floorDevice);
	double frac = device - floorDevice;
	if (frac < 0.0) {
		frac = 0.0;
	}
	int32_t phase = static_cast<int32_t>(std::lround(frac * 64.0));
	if (phase >= 64) {
		phase = 0;
		originOut += 1;
	}
	phaseOut = phase;
}

void DrawTexturedQuadCommon(float x0, float y0, float x1, float y1,
	float u0, float v0, float u1, float v1, unsigned texture, bool flipV,
	bool sourceStraightAlpha, ColourRGBA modulate,
	int orthoWidth, int orthoHeight,
	unsigned programTexture, int uniformTexTransform, int uniformTexSampler,
	int uniformTexStraightAlpha, int uniformTexModulate,
	unsigned vao, unsigned vbo) {
	if (flipV) {
		std::swap(v0, v1);
	}
	const float verts[24] = {
		x0, y0, u0, v0,
		x1, y0, u1, v0,
		x1, y1, u1, v1,
		x0, y0, u0, v0,
		x1, y1, u1, v1,
		x0, y1, u0, v1,
	};
	glUseProgram(programTexture);
	float mat[16];
	OrthoTopLeft(static_cast<float>(orthoWidth), static_cast<float>(orthoHeight), mat);
	glUniformMatrix4fv(uniformTexTransform, 1, GL_FALSE, mat);
	glUniform1i(uniformTexSampler, 0);
	glUniform1i(uniformTexStraightAlpha, sourceStraightAlpha ? 1 : 0);
	glUniform4f(uniformTexModulate,
		modulate.GetRedComponent(), modulate.GetGreenComponent(),
		modulate.GetBlueComponent(), modulate.GetAlphaComponent());
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
		reinterpret_cast<void *>(2 * sizeof(float)));
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}

}

void Renderer::DrawTexturedQuad(float x0, float y0, float x1, float y1,
	float u0, float v0, float u1, float v1, unsigned texture, bool flipV,
	bool sourceStraightAlpha, ColourRGBA modulate) {
	EnsureTextureProgram();
	// Logical surface space: same as solid fills. Buffer is the viewport only.
	DrawTexturedQuadCommon(x0, y0, x1, y1, u0, v0, u1, v1, texture, flipV,
		sourceStraightAlpha, modulate, targetLogicalWidth, targetLogicalHeight,
		programTexture, uniformTexTransform, uniformTexSampler,
		uniformTexStraightAlpha, uniformTexModulate, vao, vbo);
}

void Renderer::DrawTexturedQuadBuffer(float x0, float y0, float x1, float y1,
	float u0, float v0, float u1, float v1, unsigned texture, bool flipV,
	bool sourceStraightAlpha, ColourRGBA modulate) {
	EnsureTextureProgram();
	// Buffer-pixel space: one texture texel maps to one buffer pixel under NEAREST.
	DrawTexturedQuadCommon(x0, y0, x1, y1, u0, v0, u1, v1, texture, flipV,
		sourceStraightAlpha, modulate, targetWidth, targetHeight,
		programTexture, uniformTexTransform, uniformTexSampler,
		uniformTexStraightAlpha, uniformTexModulate, vao, vbo);
}

const Renderer::CachedGlyph &Renderer::GetOrCreateGlyph(
	const std::shared_ptr<FontFace> &face, const GlyphRasterRequest &request) {
	const GlyphKey key{face, request.glyphId, request.scale, request.phase};
	if (const auto found = glyphCache.find(key); found != glyphCache.end()) {
		return found->second;
	}
	CachedGlyph cached;
	const GlyphImage image = face->RasterizeGlyph(request);
	cached.left = image.left;
	cached.top = image.top;
	cached.width = image.width;
	cached.height = image.height;
	cached.scale = image.scale > 0.0 ? image.scale : 1.0;
	cached.colour = image.kind == GlyphImageKind::Colour;
	const bool hasGray = image.kind == GlyphImageKind::Gray && !image.gray.empty();
	const bool hasColour = image.kind == GlyphImageKind::Colour && !image.rgba.empty();
	if (image.width > 0 && image.height > 0 && (hasGray || hasColour)) {
		std::vector<uint8_t> rgba;
		if (hasGray) {
			// White RGB + coverage alpha (straight). DrawGlyph multiplies by fore.
			rgba.resize(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4u);
			for (size_t i = 0; i < image.gray.size(); i++) {
				rgba[i * 4u + 0u] = 255;
				rgba[i * 4u + 1u] = 255;
				rgba[i * 4u + 2u] = 255;
				rgba[i * 4u + 3u] = image.gray[i];
			}
		} else {
			// Already premultiplied RGBA from FreeType BGRA conversion.
			rgba = image.rgba;
		}
		GLuint tex = 0;
		glGenTextures(1, &tex);
		glBindTexture(GL_TEXTURE_2D, tex);
		// Colour bitmap strikes are often downscaled; linear filter reduces blockiness.
		// Ordinary gray glyphs keep nearest so buffer placement is a 1:1 copy.
		const GLint filter = hasColour ? GL_LINEAR : GL_NEAREST;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		glBindTexture(GL_TEXTURE_2D, 0);
		cached.texture = tex;
	}
	const auto [inserted, _] = glyphCache.emplace(key, cached);
	return inserted->second;
}

void Renderer::DrawGlyph(XYPOSITION penX, XYPOSITION penY,
	const std::shared_ptr<FontFace> &face,
	uint32_t glyphId, ColourRGBA fore) {
	if (!face || fore.GetAlpha() == 0) {
		return;
	}
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}

	// Fixed bitmap / colour strikes keep the logical strike path: no outline
	// phase, linear filtering when downscaled, cache identity without phase.
	if (face->UsesBitmapStrike()) {
		const GlyphRasterRequest request = GlyphRasterRequest::Identity(glyphId);
		const CachedGlyph &glyph = GetOrCreateGlyph(face, request);
		if (glyph.texture == 0 || glyph.width <= 0 || glyph.height <= 0) {
			return;
		}
		const float scale = static_cast<float>(glyph.scale > 0.0 ? glyph.scale : 1.0);
		const float x0 = static_cast<float>(penX) + static_cast<float>(glyph.left);
		const float y0 = static_cast<float>(penY) - static_cast<float>(glyph.top);
		const float x1 = x0 + static_cast<float>(glyph.width) * scale;
		const float y1 = y0 + static_cast<float>(glyph.height) * scale;
		glEnable(GL_BLEND);
		if (glyph.colour) {
			const unsigned int alpha = fore.GetAlpha();
			const ColourRGBA modulate(alpha, alpha, alpha, alpha);
			DrawTexturedQuad(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, glyph.texture,
				false, false, modulate);
		} else {
			DrawTexturedQuad(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, glyph.texture,
				false, true, fore);
		}
		glDisable(GL_BLEND);
		return;
	}

	// Outline path: device-size raster + integer buffer placement.
	int originX = 0;
	int originY = 0;
	int32_t phaseX = 0;
	int32_t phaseY = 0;
	LogicalToDeviceOriginAndPhase(penX, targetRasterScale, originX, phaseX);
	LogicalToDeviceOriginAndPhase(penY, targetRasterScale, originY, phaseY);
	GlyphRasterRequest request;
	request.glyphId = glyphId;
	request.scale = targetRasterScale;
	request.phase = GlyphRasterPhase::Normalize(phaseX, phaseY);
	const CachedGlyph &glyph = GetOrCreateGlyph(face, request);
	if (glyph.texture == 0 || glyph.width <= 0 || glyph.height <= 0) {
		return;
	}
	// Device bearings from FreeType; place on integer buffer pixels.
	const float x0 = static_cast<float>(originX + glyph.left);
	const float y0 = static_cast<float>(originY - glyph.top);
	const float x1 = x0 + static_cast<float>(glyph.width);
	const float y1 = y0 + static_cast<float>(glyph.height);
	glEnable(GL_BLEND);
	if (glyph.colour) {
		// Scalable colour outlines (rare): still 1:1 buffer copy, no RGB tint.
		const unsigned int alpha = fore.GetAlpha();
		const ColourRGBA modulate(alpha, alpha, alpha, alpha);
		DrawTexturedQuadBuffer(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, glyph.texture,
			false, false, modulate);
	} else {
		DrawTexturedQuadBuffer(x0, y0, x1, y1, 0.0f, 0.0f, 1.0f, 1.0f, glyph.texture,
			false, true, fore);
	}
	glDisable(GL_BLEND);
}

void Renderer::GradientRectangle(PRectangle rc, const std::vector<ColourStop> &stops, int options) {
	if (stops.empty() || rc.Width() <= 0.0 || rc.Height() <= 0.0) {
		return;
	}
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	EnsureGradientProgram();

	// Sort stops by position for stable linear segments.
	std::vector<ColourStop> ordered = stops;
	std::sort(ordered.begin(), ordered.end(),
		[](const ColourStop &a, const ColourStop &b) { return a.position < b.position; });
	const int count = static_cast<int>(std::min<size_t>(ordered.size(), 8));

	float stopPos[8] = {};
	float stopCol[8 * 4] = {};
	for (int i = 0; i < count; i++) {
		stopPos[i] = static_cast<float>(ordered[static_cast<size_t>(i)].position);
		const ColourRGBA c = ordered[static_cast<size_t>(i)].colour;
		stopCol[i * 4 + 0] = c.GetRedComponent();
		stopCol[i * 4 + 1] = c.GetGreenComponent();
		stopCol[i * 4 + 2] = c.GetBlueComponent();
		stopCol[i * 4 + 3] = c.GetAlphaComponent();
	}

	float sx = static_cast<float>(rc.left);
	float sy = static_cast<float>(rc.top);
	float ex = static_cast<float>(rc.right);
	float ey = static_cast<float>(rc.top);
	if (options == kGradientTopToBottom) {
		ex = static_cast<float>(rc.left);
		ey = static_cast<float>(rc.bottom);
	}

	const float verts[12] = {
		static_cast<float>(rc.left), static_cast<float>(rc.top),
		static_cast<float>(rc.right), static_cast<float>(rc.top),
		static_cast<float>(rc.right), static_cast<float>(rc.bottom),
		static_cast<float>(rc.left), static_cast<float>(rc.top),
		static_cast<float>(rc.right), static_cast<float>(rc.bottom),
		static_cast<float>(rc.left), static_cast<float>(rc.bottom),
	};

	// Use blend if any stop is translucent.
	bool anyTranslucent = false;
	for (int i = 0; i < count; i++) {
		if (!ordered[static_cast<size_t>(i)].colour.IsOpaque()) {
			anyTranslucent = true;
			break;
		}
	}
	if (anyTranslucent) {
		glEnable(GL_BLEND);
	} else {
		glDisable(GL_BLEND);
	}

	glUseProgram(programGradient);
	float mat[16];
	OrthoTopLeft(static_cast<float>(targetLogicalWidth),
		static_cast<float>(targetLogicalHeight), mat);
	glUniformMatrix4fv(uniformGradTransform, 1, GL_FALSE, mat);
	glUniform2f(uniformGradStart, sx, sy);
	glUniform2f(uniformGradEnd, ex, ey);
	glUniform1i(uniformGradStopCount, count);
	glUniform1fv(uniformGradStops, 8, stopPos);
	glUniform4fv(uniformGradColours, 8, stopCol);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glUseProgram(0);
	glDisable(GL_BLEND);
}

void Renderer::DrawRGBAImage(PRectangle rc, int width, int height, const unsigned char *pixels) {
	if (!pixels || width <= 0 || height <= 0 || rc.Width() <= 0.0 || rc.Height() <= 0.0) {
		return;
	}
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	// pixels are top-down. glTexImage2D treats the first row as the texture's
	// bottom (v=0). Sampling with v=0 at the top of the dest quad therefore
	// shows the first image row at the top without an extra V flip.
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glBindTexture(GL_TEXTURE_2D, 0);

	glEnable(GL_BLEND);
	DrawTexturedQuad(
		static_cast<float>(rc.left), static_cast<float>(rc.top),
		static_cast<float>(rc.right), static_cast<float>(rc.bottom),
		0.0f, 0.0f, 1.0f, 1.0f, tex, false, true);
	glDisable(GL_BLEND);

	glDeleteTextures(1, &tex);
}

void Renderer::Copy(PRectangle rc, Point from, const ColourBuffer &source) {
	if (!source.Valid() || rc.Width() <= 0.0 || rc.Height() <= 0.0) {
		return;
	}
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	const float sw = static_cast<float>(source.Width());
	const float sh = static_cast<float>(source.Height());
	if (sw <= 0.0f || sh <= 0.0f) {
		return;
	}
	// Source UVs in OpenGL space (origin bottom-left on the texture).
	// `from` and destination size are top-down surface coords on the source buffer.
	const float srcLeft = static_cast<float>(from.x);
	const float srcTop = static_cast<float>(from.y);
	const float srcRight = srcLeft + static_cast<float>(rc.Width());
	const float srcBottom = srcTop + static_cast<float>(rc.Height());
	const float u0 = srcLeft / sw;
	const float u1 = srcRight / sw;
	// FBO colour attachments use OpenGL's bottom-left origin. Top-down source
	// y maps to V = 1 - y/height. Dest top (y0) samples source top.
	const float vTop = 1.0f - srcTop / sh;
	const float vBottom = 1.0f - srcBottom / sh;

	glDisable(GL_BLEND);
	DrawTexturedQuad(
		static_cast<float>(rc.left), static_cast<float>(rc.top),
		static_cast<float>(rc.right), static_cast<float>(rc.bottom),
		u0, vTop, u1, vBottom, source.TextureName(), false, false);
}

void Renderer::FillRectanglePattern(PRectangle rc, const ColourBuffer &pattern) {
	if (!pattern.Valid() || rc.Width() <= 0.0 || rc.Height() <= 0.0) {
		return;
	}
	BeginDraw();
	if (CurrentClip().Empty()) {
		return;
	}
	const float pw = static_cast<float>(pattern.Width());
	const float ph = static_cast<float>(pattern.Height());
	if (pw <= 0.0f || ph <= 0.0f) {
		return;
	}
	// Tile count in pattern texels. FBO textures are bottom-up; match Copy.
	const float u1 = static_cast<float>(rc.Width()) / pw;
	const float tilesY = static_cast<float>(rc.Height()) / ph;
	glBindTexture(GL_TEXTURE_2D, pattern.TextureName());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glBindTexture(GL_TEXTURE_2D, 0);

	glDisable(GL_BLEND);
	// Dest top samples high V within each tile so pattern top-down matches.
	DrawTexturedQuad(
		static_cast<float>(rc.left), static_cast<float>(rc.top),
		static_cast<float>(rc.right), static_cast<float>(rc.bottom),
		0.0f, tilesY, u1, 0.0f, pattern.TextureName(), false, false);

	glBindTexture(GL_TEXTURE_2D, pattern.TextureName());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

}
