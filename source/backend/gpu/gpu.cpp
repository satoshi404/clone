#include <backend/gpu.hpp>
#include <backend/gpu/backend.hpp>

#include <core/types.hpp>
#include <core/debug.hpp>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backend lifecycle
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Gpu::gpu_backend_init()
{
	if ( !Backend::init() )
	{
		TerminalDebug::println(PrintColorType_Red, "Failed to initialize GPU backend");
		return false;
	}

	Backend::info();

	return true;
}

void Gpu::gpu_backend_free()
{
	Backend::free();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render pass
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Gpu::render_pass_init(RenderPass *renderPass, u32 width, u32 height, u32 colorAttachmentCount, u32 depthAttachmentCount)
{
	renderPass->width = width;
	renderPass->height = height;
	renderPass->colorAttachmentCount = colorAttachmentCount;
	renderPass->depthAttachmentCount = depthAttachmentCount;

	// Test // RED
	renderPass->clearColor[0] = 1.0f;
	renderPass->clearColor[1] = 0.0f;
	renderPass->clearColor[2] = 0.0f;
	renderPass->clearColor[3] = 1.0f;

	for (u32 i = 0; i < 4; ++i) renderPass->colorAttachments[i] = { HANDLE_INVALID };
	renderPass->depthAttachment = { HANDLE_INVALID };

	return true;
}

void Gpu::render_pass_begin(RenderPass *renderPass)
{
	Backend::render_pass_begin(renderPass);
}

void Gpu::render_pass_end(RenderPass *renderPass)
{
	Backend::render_pass_end(renderPass);
}

void Gpu::render_pass_free(RenderPass *renderPass)
{
	// Nothing owned directly by RenderPass itself yet — color/depth attachments are regular
	// TextureHandles the caller creates and destroys independently. Once render-to-texture is
	// wired up, this is where you'd release anything the render pass allocated on its own.
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Buffers / Shaders / Pipelines / Descriptor sets
//
// These all need a real native GPU object, so gpu.cpp doesn't do any bookkeeping of its own here —
// it just forwards to whichever Backend is compiled in (D3D12 today, Vulkan/OpenGL later).
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

BufferHandle Gpu::buffer_create(const BufferDesc &desc)   { return Backend::buffer_create(desc); }
void Gpu::buffer_update(BufferHandle buffer, u32 offset, u32 size, const void *data) { Backend::buffer_update(buffer, offset, size, data); }
void Gpu::buffer_destroy(BufferHandle buffer)             { Backend::buffer_destroy(buffer); }

TextureHandle Gpu::texture_create(const TextureDesc &desc) { return Backend::texture_create(desc); }
void Gpu::texture_destroy(TextureHandle texture)          { Backend::texture_destroy(texture); }

ShaderHandle Gpu::shader_create(const ShaderDesc &desc)   { return Backend::shader_create(desc); }
void Gpu::shader_destroy(ShaderHandle shader)             { Backend::shader_destroy(shader); }

PipelineHandle Gpu::pipeline_create(const PipelineDesc &desc) { return Backend::pipeline_create(desc); }
void Gpu::pipeline_destroy(PipelineHandle pipeline)       { Backend::pipeline_destroy(pipeline); }

DescriptorSetHandle Gpu::descriptor_set_create(const DescriptorSetDesc &desc) { return Backend::descriptor_set_create(desc); }
void Gpu::descriptor_set_update(DescriptorSetHandle set, const DescriptorSetDesc &desc) { Backend::descriptor_set_update(set, desc); }
void Gpu::descriptor_set_destroy(DescriptorSetHandle set) { Backend::descriptor_set_destroy(set); }

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Meshes
//
// A Mesh is pure bookkeeping — which buffers, how many vertices/indices, what index type — with no
// native GPU object of its own. That means it works identically no matter which backend is compiled
// in, so it lives entirely here instead of behind Backend::.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static HandlePool<MeshDesc, 1024> g_meshPool;

MeshHandle Gpu::mesh_create(const MeshDesc &desc)
{
	Handle h = g_meshPool.acquire();
	if (!handle_is_valid(h))
	{
		TerminalDebug::println(PrintColorType_Red, "Mesh pool exhausted");
		return { HANDLE_INVALID };
	}

	*g_meshPool.get(h) = desc;
	return { h };
}

void Gpu::mesh_destroy(MeshHandle mesh)
{
	g_meshPool.release(mesh.handle);
}

const MeshDesc *Gpu::mesh_get(MeshHandle mesh)
{
	return g_meshPool.get(mesh.handle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command lists
//
// Recording (init/clear/set_pipeline/set_descriptor_set/draw_mesh/dispatch) is pure CPU-side array
// manipulation — no backend involved, so the same scene code that records a CommandList works
// unmodified under D3D12, Vulkan or OpenGL. Only command_list_execute crosses into Backend, where the
// recorded commands are translated into whatever the active graphics API actually needs.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Gpu::command_list_init(CommandList *commandList, u32 commandCapacity)
{
	commandList->commands = new Command[commandCapacity];
	commandList->commandCount = 0;
	commandList->commandCapacity = commandCapacity;
	return commandList->commands != nullptr;
}

void Gpu::command_list_free(CommandList *commandList)
{
	delete[] commandList->commands;
	commandList->commands = nullptr;
	commandList->commandCount = 0;
	commandList->commandCapacity = 0;
}

void Gpu::command_list_clear(CommandList *commandList)
{
	commandList->commandCount = 0;
}

void Gpu::command_list_execute(CommandList *commandList)
{
	Backend::command_list_execute(commandList);
}

static Command *command_list_push(CommandList *commandList, CommandType type)
{
	if (commandList->commandCount >= commandList->commandCapacity)
	{
		TerminalDebug::println(PrintColorType_Red, "CommandList is full, dropping command");
		return nullptr;
	}

	Command *cmd = &commandList->commands[commandList->commandCount++];
	cmd->type = type;
	return cmd;
}

void Gpu::command_list_set_pipeline(CommandList *commandList, PipelineHandle pipeline)
{
	if (Command *cmd = command_list_push(commandList, CommandType::SetPipeline))
		cmd->setPipeline = CommandSetPipeline{ pipeline };
}

void Gpu::command_list_set_descriptor_set(CommandList *commandList, DescriptorSetHandle set, u32 slot)
{
	if (Command *cmd = command_list_push(commandList, CommandType::SetDescriptorSet))
		cmd->setDescriptorSet = CommandSetDescriptorSet{ set, slot };
}

void Gpu::command_list_draw_mesh(CommandList *commandList, MeshHandle mesh, u32 instanceCount)
{
	if (Command *cmd = command_list_push(commandList, CommandType::DrawMesh))
		cmd->drawMesh = CommandDrawMesh{ mesh, instanceCount };
}

void Gpu::command_list_dispatch(CommandList *commandList, u32 groupsX, u32 groupsY, u32 groupsZ)
{
	if (Command *cmd = command_list_push(commandList, CommandType::Dispatch))
		cmd->dispatch = CommandDispatch{ groupsX, groupsY, groupsZ };
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////