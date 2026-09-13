#pragma once

#include <core/types.hpp>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handles
//
// Every GPU resource (buffer, texture, shader, pipeline, mesh, descriptor set) is referred to by an
// opaque handle, never a raw pointer or an API-specific struct. This is the actual thing that makes
// this header portable across OpenGL / Vulkan / D3D12: a "buffer" is a GLuint in GL, a VkBuffer +
// VkDeviceMemory pair in Vulkan, and an ID3D12Resource + heap in D3D12. gpu.hpp must never know that —
// each backend keeps its own resource pool internally (an array indexed by `index`) and only ever
// hands the caller back a handle.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct Handle
{
	u32 index;      // slot in the backend's internal resource pool
	u32 generation; // bumped every time a slot is reused, so a stale handle to a freed resource is caught instead of silently aliasing whatever was created in its place
};

constexpr Handle HANDLE_INVALID = { 0xFFFFFFFFu, 0u };
inline bool handle_is_valid(Handle h) { return h.index != HANDLE_INVALID.index; }

// One distinct type per resource kind so the compiler catches "passed a TextureHandle where a
// BufferHandle was expected" — they're all just a Handle underneath.
#define GPU_DEFINE_HANDLE(Name) struct Name { Handle handle; }

GPU_DEFINE_HANDLE(BufferHandle);
GPU_DEFINE_HANDLE(TextureHandle);
GPU_DEFINE_HANDLE(ShaderHandle);
GPU_DEFINE_HANDLE(PipelineHandle);
GPU_DEFINE_HANDLE(MeshHandle);
GPU_DEFINE_HANDLE(DescriptorSetHandle);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic fixed-capacity slot pool with generation-counted handles.
//
// Shared by every resource pool in the engine — backend-side (buffers/shaders/pipelines/descriptor
// sets in d3d12_graphics.cpp, and the equivalent in a future Vulkan/OpenGL backend) and CPU-side
// (meshes and anything else that doesn't need a native GPU object, in gpu.cpp).
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename T, u32 Capacity>
struct HandlePool
{
	T slots[Capacity];
	u32 generations[Capacity];
	bool alive[Capacity];
	u32 freeList[Capacity];
	u32 freeCount;
	bool initialized = false;

	void init()
	{
		freeCount = Capacity;
		for (u32 i = 0; i < Capacity; ++i)
		{
			freeList[i] = Capacity - 1 - i;
			generations[i] = 1;
			alive[i] = false;
		}
		initialized = true;
	}

	Handle acquire()
	{
		if (!initialized) init();
		if (freeCount == 0) return HANDLE_INVALID;

		u32 index = freeList[--freeCount];
		alive[index] = true;
		return Handle{ index, generations[index] };
	}

	void release(Handle h)
	{
		if (!is_valid(h)) return;
		alive[h.index] = false;
		generations[h.index]++;
		freeList[freeCount++] = h.index;
	}

	bool is_valid(Handle h) const
	{
		return h.index < Capacity && alive[h.index] && generations[h.index] == h.generation;
	}

	T* get(Handle h)
	{
		return is_valid(h) ? &slots[h.index] : nullptr;
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffers
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum class BufferType : u32
{
	Vertex,
	Index,
	Uniform,
	Storage,
};

enum class BufferUsage : u32
{
	Static,  // written once (or rarely) by the CPU, read many times by the GPU -> backend should prefer a DEFAULT/DEVICE_LOCAL heap + upload-and-copy
	Dynamic, // written every frame by the CPU (e.g. a per-frame uniform buffer) -> backend should prefer a persistently-mapped UPLOAD/HOST_VISIBLE heap
};

struct BufferDesc
{
	BufferType type;
	BufferUsage usage;
	u32 size;          // bytes
	const void *data;  // optional initial contents, may be nullptr for a buffer that's written later via buffer_update
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Textures
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct TextureDesc
{
	u32 width;
	u32 height;
	const void *data; // RGBA8, tightly packed, width*height*4 bytes. May be nullptr for a texture
	                   // filled later (not currently supported by any backend — texture_update
	                   // doesn't exist yet, unlike buffer_update).
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vertex layout (shared by Mesh + Pipeline, so both agree on how vertex data is interpreted)
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum class VertexFormat : u32
{
	Float1, Float2, Float3, Float4,
	UByte4Norm, // packed 8-bit-per-channel color, normalized to [0,1] in the shader
};

struct VertexAttribute
{
	u32 location;       // shader input location/semantic index (GLSL `layout(location=N)`, SPIR-V location, or D3D semantic index)
	VertexFormat format;
	u32 offset;         // byte offset within one vertex
};

struct VertexLayout
{
	VertexAttribute attributes[8];
	u32 attributeCount;
	u32 stride; // byte size of one vertex
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shaders + Pipelines
//
// A Shader is one compiled stage (vertex or fragment/pixel). A Pipeline bakes a vertex+fragment shader
// pair together with the vertex layout and fixed-function state (topology, culling, depth, blending)
// into one immutable object — this maps directly onto a D3D12 PSO / VkPipeline, and onto a cached
// (program, VAO, GL state) tuple in OpenGL.
//
// NOTE ON SHADER SOURCE: HLSL, GLSL and SPIR-V are not interchangeable, so `source`/`bytecode` below
// has to hold whatever the active backend expects for that platform (raw HLSL for D3D12, GLSL for GL,
// SPIR-V bytecode for Vulkan). The straightforward way to do this without hand-writing 3 shaders per
// pipeline is to author in HLSL and cross-compile at build time (e.g. DXC to SPIR-V for Vulkan, or
// SPIRV-Cross from SPIR-V to GLSL for OpenGL) so gpu.hpp's callers still only deal with one source file.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ShaderStage : u32
{
	Vertex,
	Fragment,
	Compute,
};

struct ShaderDesc
{
	ShaderStage stage;
	const void *code;    // GLSL/HLSL source text, or SPIR-V/DXBC/DXIL bytecode, depending on backend
	u32 codeSize;         // in bytes; for null-terminated text sources this can be 0 and the backend uses strlen
	const char *entryPoint = nullptr; // e.g. "main" for GLSL/SPIR-V, "VSMain"/"PSMain" for HLSL
};

enum class PrimitiveTopology : u32
{
	TriangleList,
	TriangleStrip,
	LineList,
	PointList,
};

enum class CullMode : u32
{
	None,
	Front,
	Back,
};

enum class CompareFunc : u32
{
	Never, Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual, Always,
};

struct PipelineDesc
{
	ShaderHandle vertexShader;
	ShaderHandle fragmentShader;
	VertexLayout vertexLayout;

	PrimitiveTopology topology = PrimitiveTopology::TriangleList;
	CullMode cullMode = CullMode::Back;

	bool depthTestEnable = false;
	bool depthWriteEnable = false;
	CompareFunc depthCompare = CompareFunc::Less;

	bool blendEnable = false; // only straight alpha blending is exposed for now; extend with explicit factors if a pipeline needs more
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mesh
//
// A Mesh is just a named pairing of a vertex buffer (+ optional index buffer) with the counts needed
// to draw it, so command submission can pass around one handle instead of four separate parameters.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum class IndexType : u32
{
	None,   // non-indexed draw
	U16,
	U32,
};

struct MeshDesc
{
	BufferHandle vertexBuffer;
	BufferHandle indexBuffer; // leave as {HANDLE_INVALID} for a non-indexed mesh
	u32 vertexCount;
	u32 indexCount;   // 0 if non-indexed
	IndexType indexType = IndexType::None;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Descriptors (resource bindings visible to shaders: uniform buffers, textures, samplers)
//
// This unifies D3D12 root parameters/descriptor tables, Vulkan descriptor sets, and GL uniform/texture
// binding points behind one "slot number -> resource" concept. `slot` must match the binding the
// shader was compiled with (HLSL register, GLSL binding, SPIR-V binding).
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum class DescriptorType : u32
{
	UniformBuffer,
	Texture,
	Sampler,
};

struct DescriptorBinding
{
	u32 slot;
	DescriptorType type;
	union
	{
		BufferHandle buffer;
		TextureHandle texture;
	};
};

struct DescriptorSetDesc
{
	DescriptorBinding *bindings;
	u32 bindingCount;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render pass
//
// Describes *where* rendering goes and how it's cleared. Per-object data (transform matrices, material
// params) does NOT belong here — that's exactly what uniform buffers + descriptor sets above are for;
// baking it into the render pass would force one render pass per object.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct RenderPass
{
	u32 width;
	u32 height;
	u32 colorAttachmentCount;
	u32 depthAttachmentCount;
	f32 clearColor[4];

	TextureHandle colorAttachments[4]; // {HANDLE_INVALID} entries mean "render straight to the swap chain back buffer"
	TextureHandle depthAttachment;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command buffer
//
// A CommandList is a flat array of small tagged commands, recorded independently of any backend and
// translated into native calls (glDrawElements / vkCmdDrawIndexed / ID3D12GraphicsCommandList::
// DrawIndexedInstanced, etc.) only when `command_list_execute` runs. This is what makes it possible to
// record draw calls from gameplay/scene code that never sees a single backend-specific type — and,
// later, to record command lists on worker threads if that's ever needed.
//
// If that flexibility isn't needed yet, Gpu::render_pass_begin/end can keep issuing draws immediately
// (as they do today) and CommandList can be adopted later without changing any of the descs above —
// but a CommandList that's part of the public API and unused by any backend (as it was before) is
// worse than not having one, so don't reintroduce that gap once a backend starts consuming this.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum class CommandType : u32
{
	SetPipeline,
	SetDescriptorSet,
	DrawMesh,
	Dispatch, // compute
};

struct CommandSetPipeline    { PipelineHandle pipeline; };
struct CommandSetDescriptorSet { DescriptorSetHandle set; u32 slot; };
struct CommandDrawMesh       { MeshHandle mesh; u32 instanceCount; };
struct CommandDispatch       { u32 groupsX, groupsY, groupsZ; };

struct Command
{
	CommandType type;
	union
	{
		CommandSetPipeline setPipeline;
		CommandSetDescriptorSet setDescriptorSet;
		CommandDrawMesh drawMesh;
		CommandDispatch dispatch;
	};
};

struct CommandList
{
	Command *commands;
	u32 commandCount;
	u32 commandCapacity;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Gpu
{
	extern bool gpu_backend_init();
	extern void gpu_backend_info();
	extern void gpu_backend_free();

	// Render pass
	extern bool render_pass_init(RenderPass *renderPass, u32 width, u32 height, u32 colorAttachmentCount, u32 depthAttachmentCount);
	extern void render_pass_begin(RenderPass *renderPass);
	extern void render_pass_end(RenderPass *renderPass);
	extern void render_pass_free(RenderPass *renderPass);

	// Buffers
	extern BufferHandle buffer_create(const BufferDesc &desc);
	extern void buffer_update(BufferHandle buffer, u32 offset, u32 size, const void *data);
	extern void buffer_destroy(BufferHandle buffer);

	// Textures
	extern TextureHandle texture_create(const TextureDesc &desc);
	extern void texture_destroy(TextureHandle texture);

	// Shaders
	extern ShaderHandle shader_create(const ShaderDesc &desc);
	extern void shader_destroy(ShaderHandle shader);

	// Pipelines
	extern PipelineHandle pipeline_create(const PipelineDesc &desc);
	extern void pipeline_destroy(PipelineHandle pipeline);

	// Meshes
	extern MeshHandle mesh_create(const MeshDesc &desc);
	extern void mesh_destroy(MeshHandle mesh);
	extern const MeshDesc *mesh_get(MeshHandle mesh); // for backends to resolve a mesh's buffers/counts during command_list_execute

	// Descriptor sets
	extern DescriptorSetHandle descriptor_set_create(const DescriptorSetDesc &desc);
	extern void descriptor_set_update(DescriptorSetHandle set, const DescriptorSetDesc &desc);
	extern void descriptor_set_destroy(DescriptorSetHandle set);

	// Command lists
	extern bool command_list_init(CommandList *commandList, u32 commandCapacity);
	extern void command_list_free(CommandList *commandList);
	extern void command_list_clear(CommandList *commandList);
	extern void command_list_execute(CommandList *commandList);

	extern void command_list_set_pipeline(CommandList *commandList, PipelineHandle pipeline);
	extern void command_list_set_descriptor_set(CommandList *commandList, DescriptorSetHandle set, u32 slot);
	extern void command_list_draw_mesh(CommandList *commandList, MeshHandle mesh, u32 instanceCount = 1);
	extern void command_list_dispatch(CommandList *commandList, u32 groupsX, u32 groupsY, u32 groupsZ);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////