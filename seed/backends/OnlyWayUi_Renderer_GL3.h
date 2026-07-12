#pragma once

#include <OnlyWayUi/Config/Config.h>
#include <OnlyWayUi/Core/BasicTypes.h>
#include <OnlyWayUi/Core/CoreAliases.h>
#include <OnlyWayUi/Core/CoreHandles.h>
#include <OnlyWayUi/Core/Matrix4.h>
#include <OnlyWayUi/Core/Rectangle.h>
#include <OnlyWayUi/Core/RenderInterface.h>
#include <OnlyWayUi/Core/Span.h>
#include <OnlyWayUi/Core/Vector2.h>
#include <OnlyWayUi/Core/Vector4.h>
#include <bitset>
#include <stddef.h>

enum class ProgramId;
enum class UniformId;
namespace Gfx {
struct ProgramData;
struct FramebufferData;
} // namespace Gfx

class RenderInterface_GL3 : public OnlyWayUi::RenderInterface {
public:
	RenderInterface_GL3();
	~RenderInterface_GL3();

	// Returns true if the renderer was successfully constructed.
	explicit operator bool() const { return static_cast<bool>(program_data); }

	// The viewport should be updated whenever the window size changes.
	void SetViewport(int viewport_width, int viewport_height, int viewport_offset_x = 0, int viewport_offset_y = 0);

	// Sets up OpenGL states for taking rendering commands from OnlyWayUi.
	void BeginFrame();
	void BeginFrame(OnlyWayUi::Rectanglei damage_region);
	// Draws the result to the backbuffer and restores OpenGL state.
	void EndFrame();

	// -- Inherited from OnlyWayUi::RenderInterface --

	OnlyWayUi::CompiledGeometryHandle CompileGeometry(OnlyWayUi::Span<const OnlyWayUi::Vertex> vertices, OnlyWayUi::Span<const int> indices) override;
	void RenderGeometry(OnlyWayUi::CompiledGeometryHandle handle, OnlyWayUi::Vector2f translation, OnlyWayUi::TextureHandle texture) override;
	void ReleaseGeometry(OnlyWayUi::CompiledGeometryHandle handle) override;

	OnlyWayUi::TextureHandle LoadTexture(OnlyWayUi::Vector2i& texture_dimensions, const OnlyWayUi::String& source) override;
	OnlyWayUi::TextureHandle GenerateTexture(OnlyWayUi::Span<const OnlyWayUi::byte> source_data, OnlyWayUi::Vector2i source_dimensions) override;
	void ReleaseTexture(OnlyWayUi::TextureHandle texture_handle) override;

	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(OnlyWayUi::Rectanglei region) override;

	void EnableClipMask(bool enable) override;
	void RenderToClipMask(OnlyWayUi::ClipMaskOperation mask_operation, OnlyWayUi::CompiledGeometryHandle geometry, OnlyWayUi::Vector2f translation) override;

	void SetTransform(const OnlyWayUi::Matrix4f* transform) override;

	OnlyWayUi::LayerHandle PushLayer() override;
	void CompositeLayers(OnlyWayUi::LayerHandle source, OnlyWayUi::LayerHandle destination, OnlyWayUi::BlendMode blend_mode,
		OnlyWayUi::Span<const OnlyWayUi::CompiledFilterHandle> filters) override;
	void PopLayer() override;

	OnlyWayUi::TextureHandle SaveLayerAsTexture() override;

	OnlyWayUi::CompiledFilterHandle SaveLayerAsMaskImage() override;

	OnlyWayUi::CompiledFilterHandle CompileFilter(const OnlyWayUi::String& name, const OnlyWayUi::Dictionary& parameters) override;
	void ReleaseFilter(OnlyWayUi::CompiledFilterHandle filter) override;

	OnlyWayUi::CompiledShaderHandle CompileShader(const OnlyWayUi::String& name, const OnlyWayUi::Dictionary& parameters) override;
	void RenderShader(OnlyWayUi::CompiledShaderHandle shader_handle, OnlyWayUi::CompiledGeometryHandle geometry_handle, OnlyWayUi::Vector2f translation,
		OnlyWayUi::TextureHandle texture) override;
	void ReleaseShader(OnlyWayUi::CompiledShaderHandle effect_handle) override;

	// Can be passed to RenderGeometry() to enable texture rendering without changing the bound texture.
	static constexpr OnlyWayUi::TextureHandle TextureEnableWithoutBinding = OnlyWayUi::TextureHandle(-1);
	// Can be passed to RenderGeometry() to leave the bound texture and used program unchanged.
	static constexpr OnlyWayUi::TextureHandle TexturePostprocess = OnlyWayUi::TextureHandle(-2);

	// -- Utility functions for clients --

	const OnlyWayUi::Matrix4f& GetTransform() const;
	void ResetProgram();

private:
	void UseProgram(ProgramId program_id);
	int GetUniformLocation(UniformId uniform_id) const;
	void SubmitTransformUniform(OnlyWayUi::Vector2f translation);

	void BlitLayerToPostprocessPrimary(OnlyWayUi::LayerHandle layer_handle);
	void RenderFilters(OnlyWayUi::Span<const OnlyWayUi::CompiledFilterHandle> filter_handles);

	void SetScissor(OnlyWayUi::Rectanglei region, bool vertically_flip = false);

	void DrawFullscreenQuad();
	void DrawFullscreenQuad(OnlyWayUi::Vector2f uv_offset, OnlyWayUi::Vector2f uv_scaling = OnlyWayUi::Vector2f(1.f));

	void RenderBlur(float sigma, const Gfx::FramebufferData& source_destination, const Gfx::FramebufferData& temp, OnlyWayUi::Rectanglei window_flipped);

	static constexpr size_t MaxNumPrograms = 32;
	std::bitset<MaxNumPrograms> program_transform_dirty;

	OnlyWayUi::Matrix4f transform;
	OnlyWayUi::Matrix4f projection;

	ProgramId active_program = {};
	OnlyWayUi::Rectanglei scissor_state;
	OnlyWayUi::Rectanglei frame_damage_region;

	int viewport_width = 0;
	int viewport_height = 0;
	int viewport_offset_x = 0;
	int viewport_offset_y = 0;

	OnlyWayUi::CompiledGeometryHandle fullscreen_quad_geometry = {};

	OnlyWayUi::UniquePtr<const Gfx::ProgramData> program_data;

	/*
	    Manages render targets, including the layer stack and postprocessing framebuffers.

	    Layers can be pushed and popped, creating new framebuffers as needed. Typically, geometry is rendered to the top
	    layer. The layer framebuffers may have MSAA enabled.

	    Postprocessing framebuffers are separate from the layers, and are commonly used to apply texture-wide effects
	    such as filters. They are used both as input and output during rendering, and do not use MSAA.
	*/
	class RenderLayerStack {
	public:
		RenderLayerStack();
		~RenderLayerStack();

		// Push a new layer. All references to previously retrieved layers are invalidated.
		OnlyWayUi::LayerHandle PushLayer();

		// Pop the top layer. All references to previously retrieved layers are invalidated.
		void PopLayer();

		const Gfx::FramebufferData& GetLayer(OnlyWayUi::LayerHandle layer) const;
		const Gfx::FramebufferData& GetTopLayer() const;
		OnlyWayUi::LayerHandle GetTopLayerHandle() const;

		const Gfx::FramebufferData& GetPostprocessPrimary() { return EnsureFramebufferPostprocess(0); }
		const Gfx::FramebufferData& GetPostprocessSecondary() { return EnsureFramebufferPostprocess(1); }
		const Gfx::FramebufferData& GetPostprocessTertiary() { return EnsureFramebufferPostprocess(2); }
		const Gfx::FramebufferData& GetBlendMask() { return EnsureFramebufferPostprocess(3); }

		void SwapPostprocessPrimarySecondary();

		void BeginFrame(int new_width, int new_height);
		void EndFrame();

	private:
		void DestroyFramebuffers();
		const Gfx::FramebufferData& EnsureFramebufferPostprocess(int index);

		int width = 0, height = 0;

		// The number of active layers is manually tracked since we re-use the framebuffers stored in the fb_layers stack.
		int layers_size = 0;

		OnlyWayUi::Vector<Gfx::FramebufferData> fb_layers;
		OnlyWayUi::Vector<Gfx::FramebufferData> fb_postprocess;
	};

	RenderLayerStack render_layers;

	struct GLStateBackup {
		bool enable_cull_face;
		bool enable_blend;
		bool enable_stencil_test;
		bool enable_scissor_test;
		bool enable_depth_test;

		int viewport[4];
		int scissor[4];

		int active_texture;

		int stencil_clear_value;
		float color_clear_value[4];
		unsigned char color_writemask[4];

		int blend_equation_rgb;
		int blend_equation_alpha;
		int blend_src_rgb;
		int blend_dst_rgb;
		int blend_src_alpha;
		int blend_dst_alpha;

		struct Stencil {
			int func;
			int ref;
			int value_mask;
			int writemask;
			int fail;
			int pass_depth_fail;
			int pass_depth_pass;
		};
		Stencil stencil_front;
		Stencil stencil_back;
	};
	GLStateBackup glstate_backup = {};
};

/**
    Helper functions for the OpenGL 3 renderer.
 */
namespace OwuiGL3 {

// Loads OpenGL functions. Optionally, the out message describes the loaded GL version or an error message on failure.
bool Initialize(OnlyWayUi::String* out_message = nullptr);

// Unloads OpenGL functions.
void Shutdown();

} // namespace OwuiGL3
