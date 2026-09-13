#include <backend/window.hpp>
#include <backend/input/keys.hpp>
#include <backend/keyboard.hpp>
#include <backend/gpu.hpp>

#include <core/debug.hpp>

#include <string.h>

static const char *shaderSource = R"(
// Equivalent to an OpenGL Uniform Block
cbuffer MyUniforms : register(b0)
{
    float3 u_offset; // 12 bytes
    float  u_time;   // 4 bytes -> (Total block size: 16 bytes)
};

struct VSInput
{
    float3 position : ATTRIB0;
    float4 color    : ATTRIB1;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    // Using the uniform variables just like in OpenGL!
    float3 finalPosition = input.position + u_offset;
    finalPosition.y += sin(u_time) * 0.2f; // simple bounce animation

    output.position = float4(finalPosition, 1.0f);
    output.color = input.color;

    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
)";


static float data[] =
{
    // Triangle 1
    // position              // color
     0.5f,  0.5f, 0.0f,      1.0f, 0.0f, 0.0f, 1.0f, // Top-Right
    -0.5f,  0.5f, 0.0f,      0.0f, 1.0f, 0.0f, 1.0f, // Top-Left
    -0.5f, -0.5f, 0.0f,      0.0f, 1.0f, 1.0f, 1.0f, // Bottom-Left

    // Triangle 2
    -0.5f, -0.5f, 0.0f,      0.0f, 1.0f, 1.0f, 1.0f, // Bottom-Left (Shared)
     0.5f, -0.5f, 0.0f,      0.0f, 0.0f, 1.0f, 1.0f, // Bottom-Right
     0.5f,  0.5f, 0.0f,      1.0f, 0.0f, 0.0f, 1.0f  // Top-Right (Shared)
};

int main()
{
  if (!Window::init())
  {
    TerminalDebug::println(PrintColorType_Red, "Failed init window");
    return EXIT_FAILED;
  }

  if ( !Gpu::gpu_backend_init() )
  {
    TerminalDebug::println(PrintColorType_Red, "Failed init GPU backend");
    Window::terminate();
    return EXIT_FAILED;
  }

  BufferDesc triangle;
  triangle.data = data;
  triangle.size = sizeof(data);
  triangle.type = BufferType::Vertex;
  triangle.usage = BufferUsage::Static;
  BufferHandle buffer_vertex = Gpu::buffer_create( triangle );

  ShaderDesc vertexShaderDesc;
  vertexShaderDesc.stage = ShaderStage::Vertex;
  vertexShaderDesc.codeSize = strlen( shaderSource );
  vertexShaderDesc.code = shaderSource;
  vertexShaderDesc.entryPoint = "VSMain";
  ShaderHandle vsHandle = Gpu::shader_create( vertexShaderDesc );

  ShaderDesc fragmentShaderDesc;
  fragmentShaderDesc.stage = ShaderStage::Fragment;
  fragmentShaderDesc.codeSize = strlen(shaderSource);
  fragmentShaderDesc.code = shaderSource;
  fragmentShaderDesc.entryPoint = "PSMain";
  ShaderHandle fsHandle = Gpu::shader_create(fragmentShaderDesc);

  CommandList cmdList;
  if (!Gpu::command_list_init(&cmdList, 32))
  {
    TerminalDebug::println(PrintColorType_Red, "Failed to initialize CommandList");
    Gpu::shader_destroy(vsHandle);
    Gpu::shader_destroy(fsHandle);
    Gpu::buffer_destroy(buffer_vertex);
    Gpu::gpu_backend_free();
    Window::terminate();
    return EXIT_FAILED;
  }

  TextureHandle localColorAttachments[4] = { { HANDLE_INVALID }, { HANDLE_INVALID }, { HANDLE_INVALID }, { HANDLE_INVALID } };
  RenderPass renderPass;
  renderPass.colorAttachments[0] = localColorAttachments[0];
  renderPass.colorAttachments[1] = localColorAttachments[1];
  renderPass.colorAttachments[2] = localColorAttachments[2];
  renderPass.colorAttachments[3] = localColorAttachments[3];

  if (!Gpu::render_pass_init( &renderPass, WindowConfig::get_width(), WindowConfig::get_height(), 1, 1))
  {
    TerminalDebug::println(PrintColorType_Red, "Failed to initialize render pass");
    Gpu::command_list_free(&cmdList);
    Gpu::shader_destroy(vsHandle);
    Gpu::shader_destroy(fsHandle);
    Gpu::buffer_destroy(buffer_vertex);
    Gpu::gpu_backend_free();
    Window::terminate();
    return EXIT_FAILED;
  }

  renderPass.clearColor[0] = 0.3f;
  renderPass.clearColor[1] = 0.3f;
  renderPass.clearColor[2] = 0.3f;
  renderPass.clearColor[3] = 1.0f;

  // Configuração explícita do Vertex Layout exigida pelo seu D3D12 Backend
 PipelineDesc pipeline = {};

pipeline.vertexShader = vsHandle;
pipeline.fragmentShader = fsHandle;

pipeline.blendEnable = false;

pipeline.topology = PrimitiveTopology::TriangleList;
pipeline.cullMode = CullMode::None;

  // Atributo 0: Posição (float3) -> Localização 0, Offset 0
  pipeline.vertexLayout.attributes[0].location = 0;
  pipeline.vertexLayout.attributes[0].format = VertexFormat::Float3; // Assumindo enum compatível com float3
  pipeline.vertexLayout.attributes[0].offset = 0;

  // Atributo 1: Cor (float4) -> Localização 1, Offset 12 bytes (após os 3 floats de posição)
  pipeline.vertexLayout.attributes[1].location = 1;
  pipeline.vertexLayout.attributes[1].format = VertexFormat::Float4; // Assumindo enum compatível com float4
  pipeline.vertexLayout.attributes[1].offset = 3 * sizeof(float);

  pipeline.vertexLayout.attributeCount = 2;
  pipeline.vertexLayout.stride = 7 * sizeof(float); // Total de bytes por vértice (3 pos + 4 cor = 28)

  PipelineHandle pHandle = Gpu::pipeline_create( pipeline );

  MeshDesc canvasMeshDesc = {};
  canvasMeshDesc.vertexBuffer = buffer_vertex;
  canvasMeshDesc.vertexCount = 6;
  MeshHandle screenQuadMesh = Gpu::mesh_create(canvasMeshDesc);

  float totalElapsedTime = 0.;

  struct EngineUniforms {
    float offset[3];
    float time;
  };

  EngineUniforms cpuData;
  cpuData.offset[0] = 0.0f;
  cpuData.offset[1] = 0.1f; // Shift up slightly
  cpuData.offset[2] = 0.0f;
  cpuData.time = totalElapsedTime;


  DescriptorBinding binding = {};
  binding.slot = 0;
  binding.type = DescriptorType::UniformBuffer;

  DescriptorSetDesc desc = {};
  desc.bindings = &binding;
  desc.bindingCount = 1;
  DescriptorSetHandle desHandle = Gpu::descriptor_set_create(  desc );

  Window::show();

  while (WindowConfig::running())
  {
    Window::pool();
    if (Keyboard::check_pressed_repeat(Keys::VK_Space))
    {
      TerminalDebug::println(PrintColorType_Magenta, "Hello, World!");
    }
    Keyboard::update(0);

    cpuData.time += 0.016f;
    Gpu::descriptor_set_update( desHandle , desc);

    Gpu::command_list_clear(&cmdList);

    Gpu::render_pass_begin(&renderPass);
    // Grava os tokens na fila da CPU
    Gpu::command_list_set_pipeline(&cmdList, pHandle);
    Gpu::command_list_set_descriptor_set(&cmdList, desHandle, 0);
    Gpu::command_list_draw_mesh(&cmdList, screenQuadMesh, 1);
    Gpu::command_list_execute(&cmdList);
    // Executa o ciclo de vida nativo e desenha o frame
    Gpu::render_pass_end(&renderPass);
  }

  Gpu::mesh_destroy(screenQuadMesh);
  Gpu::pipeline_destroy(pHandle);
  Gpu::command_list_free(&cmdList);
  Gpu::render_pass_free(&renderPass);
  Gpu::shader_destroy(vsHandle);
  Gpu::shader_destroy(fsHandle);
  Gpu::buffer_destroy(buffer_vertex);

  Gpu::gpu_backend_free();
  Window::terminate();

  return EXIT_SUCCESS;
}
