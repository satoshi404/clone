#include <backend/window.hpp>
#include <backend/input/keys.hpp>
#include <backend/keyboard.hpp>
#include <backend/gpu.hpp>

#include <core/debug.hpp>

#include <string.h>

static const char *shaderSource = R"(
struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

// Signed Distance Function (SDF) for a simple sphere
float map(float3 pos)
{
    return length(pos) - 0.5f;
}

// Raymarching Loop
float rayMarch(float3 ro, float3 rd)
{
    float t = 0.0f;
    for (int i = 0; i < 100; i++)
    {
        float3 p = ro + rd * t;
        float d = map(p);
        if (d < 0.001f || t > 100.0f) { break; }
        t += d;
    }
    return t;
}

// Alterado para ATTRIB0 e ATTRIB1 para casar com a convenção do seu Backend
PSInput VSMain(float3 position : ATTRIB0, float4 color : ATTRIB1)
{
    PSInput result;

    // Direct assignment for pipeline positioning
    result.position = float4(position, 1.0f);
    result.color = color;

    // Pass coordinates normalized to [-1, 1] as UVs for ray generation
    result.uv = position.xy;

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // Ray origin (camera position back on Z axis)
    float3 ro = float3(0.0f, 0.0f, -2.0f);

    // Ray direction (Perspective projection using screen UV coordinates)
    float3 rd = normalize(float3(input.uv, 1.0f));

    // March the ray through the scene
    float t = rayMarch(ro, rd);

    // If the ray hit the sphere (t < max distance limit)
    if (t < 100.0f)
    {
        // Calculate a simple diffuse lighting effect based on surface normals
        float3 p = ro + rd * t;

        // Fast numerical normal extraction
        float2 e = float2(0.001f, 0.0f);
        float3 normal = normalize(float3(
            map(p + e.xyy) - map(p - e.xyy),
            map(p + e.yxy) - map(p - e.yxy),
            map(p + e.yyx) - map(p - e.yyx)
        ));

        // Simple light setup from upper-right front
        float3 lightDir = normalize(float3(1.0f, 1.0f, -1.0f));
        float diffuse = max(dot(normal, lightDir), 0.2f); // 0.2 ambient floor

        return input.color * diffuse;
    }

    // Background color (Discard or return transparent black)
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}
)";

// Interleaved Vertices: Position(X, Y, Z) + Color(R, G, B, A)
static float data[] =
{
  -1.0f, -1.0f,  0.0f,   1.0f, 1.0f, 1.0f, 1.0f, // Bottom-left
   3.0f, -1.0f,  0.0f,   1.0f, 1.0f, 1.0f, 1.0f, // Bottom-right extra
  -1.0f,  3.0f,  0.0f,   1.0f, 1.0f, 1.0f, 1.0f  // Top-left extra
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

  renderPass.clearColor[0] = 0.0f;
  renderPass.clearColor[1] = 0.0f;
  renderPass.clearColor[2] = 0.0f;
  renderPass.clearColor[3] = 1.0f;

  // Configuração explícita do Vertex Layout exigida pelo seu D3D12 Backend
  PipelineDesc pipeline = {};
  pipeline.vertexShader = vsHandle;
  pipeline.fragmentShader = fsHandle;
  pipeline.blendEnable = false;
  pipeline.topology = PrimitiveTopology::TriangleList;

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
  canvasMeshDesc.vertexCount = 3;
  MeshHandle screenQuadMesh = Gpu::mesh_create(canvasMeshDesc);

  Window::show();

  while (WindowConfig::running())
  {
    Window::pool();
    if (Keyboard::check_pressed_repeat(Keys::VK_Space))
    {
      TerminalDebug::println(PrintColorType_Magenta, "Hello, World!");
    }
    Keyboard::update(0);

    Gpu::command_list_clear(&cmdList);

    // Grava os tokens na fila da CPU
    Gpu::command_list_set_pipeline(&cmdList, pHandle);
    Gpu::command_list_draw_mesh(&cmdList, screenQuadMesh, 1);

    // Executa o ciclo de vida nativo e desenha o frame
    Gpu::render_pass_begin(&renderPass);
    Gpu::render_pass_end(&renderPass);

    Gpu::command_list_execute(&cmdList);
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
