#pragma once

#include <core/types.hpp>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace Backend
{
    extern bool init();
	extern void info();
    extern void free();

	extern void render_pass_begin(RenderPass *renderPass);
	extern void render_pass_end(RenderPass *renderPass);
    extern BufferHandle buffer_create(const BufferDesc &desc);
	extern void buffer_update(BufferHandle buffer, u32 offset, u32 size, const void *data);
	extern void buffer_destroy(BufferHandle buffer);
	extern ShaderHandle shader_create(const ShaderDesc &desc);
	extern void shader_destroy(ShaderHandle shader);
	extern PipelineHandle pipeline_create(const PipelineDesc &desc);
	extern void pipeline_destroy(PipelineHandle pipeline);
	extern DescriptorSetHandle descriptor_set_create(const DescriptorSetDesc &desc);
	extern void descriptor_set_update(DescriptorSetHandle set, const DescriptorSetDesc &desc);
	extern void descriptor_set_destroy(DescriptorSetHandle set);
	extern void command_list_execute(::CommandList *cmdList);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////