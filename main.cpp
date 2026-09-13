#include <backend/window.hpp>
#include <backend/input/keys.hpp>
#include <backend/keyboard.hpp>
#include <backend/gpu.hpp>
#include <core/debug.hpp>
#include <string.h>

static const char *shaderSource = R"(
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
    output.position = float4( input.position, 1.0f );
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
     0.5f,  0.5f, 0.0f,      1.0f, 0.0f, 0.0f, 1.0f, // Top-Right
    -0.5f,  0.5f, 0.0f,      0.0f, 1.0f, 0.0f, 1.0f, // Top-Left
    -0.5f, -0.5f, 0.0f,      0.0f, 1.0f, 1.0f, 1.0f, // Bottom-Left

    // Triangle 2
    -0.5f, -0.5f, 0.0f,      0.0f, 1.0f, 1.0f, 1.0f, // Bottom-Left (Shared)
     0.5f, -0.5f, 0.0f,      0.0f, 0.0f, 1.0f, 1.0f, // Bottom-Right
     0.5f,  0.5f, 0.0f,      1.0f, 0.0f, 0.0f, 1.0f  // Top-Right (Shared)
};

// ==========================================
// RAII WRAPPERS FOR CLEAN AUTOMATIC CLEANUP
// ==========================================
struct ScopedGpuBackend {
    bool initialized = false;
    ScopedGpuBackend() { initialized = Gpu::gpu_backend_init(); }
    ~ScopedGpuBackend() { if (initialized) Gpu::gpu_backend_free(); }
};

struct ScopedBuffer {
    BufferHandle handle { HANDLE_INVALID };
    ScopedBuffer(const BufferDesc& desc) { handle = Gpu::buffer_create(desc); }
    ~ScopedBuffer() { if (handle.handle.generation != Handle{ HANDLE_INVALID }.generation) Gpu::buffer_destroy(handle); }
};

struct ScopedShader {
    ShaderHandle handle { HANDLE_INVALID };
    ScopedShader(const ShaderDesc& desc) { handle = Gpu::shader_create(desc); }
    ~ScopedShader() { if (handle.handle.generation != Handle{ HANDLE_INVALID }.generation) Gpu::shader_destroy(handle); }
};

struct ScopedCommandList {
    CommandList list;
    bool initialized = false;
    ScopedCommandList(isize size) { initialized = Gpu::command_list_init(&list, size); }
    ~ScopedCommandList() { if (initialized) Gpu::command_list_free(&list); }
};

struct ScopedRenderPass {
    RenderPass pass;
    bool initialized = false;
    ScopedRenderPass(u32 width, u32 height) {
        TextureHandle localColorAttachments[4] = { { HANDLE_INVALID }, { HANDLE_INVALID }, { HANDLE_INVALID }, { HANDLE_INVALID } };
        pass.colorAttachments[0] = localColorAttachments[0];
        pass.colorAttachments[1] = localColorAttachments[1];
        pass.colorAttachments[2] = localColorAttachments[2];
        pass.colorAttachments[3] = localColorAttachments[3];
        initialized = Gpu::render_pass_init(&pass, width, height, 1, 1);
    }
    ~ScopedRenderPass() { if (initialized) Gpu::render_pass_free(&pass); }
};

struct ScopedPipeline {
    PipelineHandle handle { HANDLE_INVALID };
    ScopedPipeline(const PipelineDesc& desc) { handle = Gpu::pipeline_create(desc); }
    ~ScopedPipeline() { if (handle.handle.generation != Handle{ HANDLE_INVALID }.generation) Gpu::pipeline_destroy(handle); }
};

struct ScopedMesh {
    MeshHandle handle { HANDLE_INVALID };
    ScopedMesh(const MeshDesc& desc) { handle = Gpu::mesh_create(desc); }
    ~ScopedMesh() { if (handle.handle.generation != Handle{ HANDLE_INVALID }.generation) Gpu::mesh_destroy(handle); }
};

// ==========================================
// MAIN APPLICATION
// ==========================================
int main()
{
  if (!Window::init())
  {
    TerminalDebug::println(PrintColorType_Red, "Failed init window");
    return EXIT_FAILED;
  }
  // Ensure window is always terminated safely on return
  struct WindowGuard { ~WindowGuard() { Window::terminate(); } } windowGuard;

  ScopedGpuBackend gpuBackend;
  if (!gpuBackend.initialized)
  {
    TerminalDebug::println(PrintColorType_Red, "Failed init GPU backend");
    return EXIT_FAILED;
  }

  BufferDesc triangle;
  triangle.data = data;
  triangle.size = sizeof(data);
  triangle.type = BufferType::Vertex;
  triangle.usage = BufferUsage::Static;
  ScopedBuffer buffer_vertex(triangle);

  ShaderDesc vertexShaderDesc;
  vertexShaderDesc.stage = ShaderStage::Vertex;
  vertexShaderDesc.codeSize = strlen(shaderSource);
  vertexShaderDesc.code = shaderSource;
  ScopedShader vsHandle(vertexShaderDesc);

  ShaderDesc fragmentShaderDesc;
  fragmentShaderDesc.stage = ShaderStage::Fragment;
  fragmentShaderDesc.codeSize = strlen(shaderSource);
  fragmentShaderDesc.code = shaderSource;
  ScopedShader fsHandle(fragmentShaderDesc);

  ScopedCommandList cmdList(32);
  if (!cmdList.initialized)
  {
    TerminalDebug::println(PrintColorType_Red, "Failed to initialize CommandList");
    return EXIT_FAILED;
  }

  ScopedRenderPass renderPass(WindowConfig::get_width(), WindowConfig::get_height());
  if (!renderPass.initialized)
  {
    TerminalDebug::println(PrintColorType_Red, "Failed to initialize render pass");
    return EXIT_FAILED;
  }

  renderPass.pass.clearColor[0] = 0.3f;
  renderPass.pass.clearColor[1] = 0.3f;
  renderPass.pass.clearColor[2] = 0.3f;
  renderPass.pass.clearColor[3] = 1.0f;

  PipelineDesc pipeline = {};
  pipeline.vertexShader = vsHandle.handle;
  pipeline.fragmentShader = fsHandle.handle;
  pipeline.blendEnable = false;
  pipeline.topology = PrimitiveTopology::TriangleList;
  pipeline.cullMode = CullMode::None;

  pipeline.vertexLayout.attributes[0].location = 0;
  pipeline.vertexLayout.attributes[0].format = VertexFormat::Float3;
  pipeline.vertexLayout.attributes[0].offset = 0;

  pipeline.vertexLayout.attributes[1].location = 1;
  pipeline.vertexLayout.attributes[1].format = VertexFormat::Float4;
  pipeline.vertexLayout.attributes[1].offset = 3 * sizeof(float);

  pipeline.vertexLayout.attributeCount = 2;
  pipeline.vertexLayout.stride = 7 * sizeof(float);

  ScopedPipeline pHandle(pipeline);
  if (pHandle.handle.handle.generation == Handle{ HANDLE_INVALID }.generation)
  {
    return EXIT_FAILED;
  }

  MeshDesc canvasMeshDesc = {};
  canvasMeshDesc.vertexBuffer = buffer_vertex.handle;
  canvasMeshDesc.vertexCount = 6;
  ScopedMesh screenQuadMesh(canvasMeshDesc);

  Window::show();

  while (WindowConfig::running())
  {
    Window::pool();
    if (Keyboard::check_pressed_repeat(Keys::VK_Space))
    {
      TerminalDebug::println(PrintColorType_Magenta, "Hello, World!");
    }
    Keyboard::update(0);

    Gpu::command_list_clear(&cmdList.list);

    Gpu::render_pass_begin(&renderPass.pass);
    Gpu::command_list_set_pipeline(&cmdList.list, pHandle.handle);
    Gpu::command_list_draw_mesh(&cmdList.list, screenQuadMesh.handle, 1);
    Gpu::command_list_execute(&cmdList.list);
    Gpu::render_pass_end(&renderPass.pass);
  }

  return EXIT_SUCCESS;
}
