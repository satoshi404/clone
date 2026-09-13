#include <backend/window.hpp>
#include <backend/input/keys.hpp>
#include <backend/keyboard.hpp>
#include <backend/gpu.hpp>
#include <core/debug.hpp>
#include <string.h>

#include "mat4.hpp"
#include "obj_loader.hpp"

static const char *shaderSource = R"(
cbuffer FrameConstants : register(b0)
{
    row_major float4x4 mvp;
};

struct VSInput
{
    float3 position : ATTRIB0;
    float3 color    : ATTRIB1;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul( float4( input.position, 1.0f ), mvp );
    output.color = float4( input.color, 1.0f );
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
)";

// ==========================================
// RAII WRAPPERS FOR CLEAN AUTOMATIC CLEANUP
// ==========================================
struct ScopedGpuBackend
{
    bool initialized = false;
    ScopedGpuBackend() { initialized = Gpu::gpu_backend_init(); }
    ~ScopedGpuBackend()
    {
        if (initialized)
            Gpu::gpu_backend_free();
    }
};

struct ScopedBuffer
{
    BufferHandle handle{HANDLE_INVALID};
    ScopedBuffer(const BufferDesc &desc) { handle = Gpu::buffer_create(desc); }
    ~ScopedBuffer()
    {
        if (handle.handle.generation != Handle{HANDLE_INVALID}.generation)
            Gpu::buffer_destroy(handle);
    }
};

struct ScopedShader
{
    ShaderHandle handle{HANDLE_INVALID};
    ScopedShader(const ShaderDesc &desc) { handle = Gpu::shader_create(desc); }
    ~ScopedShader()
    {
        if (handle.handle.generation != Handle{HANDLE_INVALID}.generation)
            Gpu::shader_destroy(handle);
    }
};

struct ScopedCommandList
{
    CommandList list;
    bool initialized = false;
    ScopedCommandList(isize size) { initialized = Gpu::command_list_init(&list, size); }
    ~ScopedCommandList()
    {
        if (initialized)
            Gpu::command_list_free(&list);
    }
};

struct ScopedRenderPass
{
    RenderPass pass;
    bool initialized = false;
    ScopedRenderPass(u32 width, u32 height)
    {
        TextureHandle localColorAttachments[4] = {{HANDLE_INVALID}, {HANDLE_INVALID}, {HANDLE_INVALID}, {HANDLE_INVALID}};
        pass.colorAttachments[0] = localColorAttachments[0];
        pass.colorAttachments[1] = localColorAttachments[1];
        pass.colorAttachments[2] = localColorAttachments[2];
        pass.colorAttachments[3] = localColorAttachments[3];
        initialized = Gpu::render_pass_init(&pass, width, height, 1, 1);
    }
    ~ScopedRenderPass()
    {
        if (initialized)
            Gpu::render_pass_free(&pass);
    }
};

struct ScopedPipeline
{
    PipelineHandle handle{HANDLE_INVALID};
    ScopedPipeline(const PipelineDesc &desc) { handle = Gpu::pipeline_create(desc); }
    ~ScopedPipeline()
    {
        if (handle.handle.generation != Handle{HANDLE_INVALID}.generation)
            Gpu::pipeline_destroy(handle);
    }
};

struct ScopedMesh
{
    MeshHandle handle{HANDLE_INVALID};
    ScopedMesh(const MeshDesc &desc) { handle = Gpu::mesh_create(desc); }
    ~ScopedMesh()
    {
        if (handle.handle.generation != Handle{HANDLE_INVALID}.generation)
            Gpu::mesh_destroy(handle);
    }
};

struct ScopedDescriptorSet
{
    DescriptorSetHandle handle{HANDLE_INVALID};
    ScopedDescriptorSet(const DescriptorSetDesc &desc) { handle = Gpu::descriptor_set_create(desc); }
    ~ScopedDescriptorSet()
    {
        if (handle.handle.generation != Handle{HANDLE_INVALID}.generation)
            Gpu::descriptor_set_destroy(handle);
    }
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
    struct WindowGuard
    {
        ~WindowGuard() { Window::terminate(); }
    } windowGuard;

    ScopedGpuBackend gpuBackend;
    if (!gpuBackend.initialized)
    {
        TerminalDebug::println(PrintColorType_Red, "Failed init GPU backend");
        return EXIT_FAILED;
    }

    Window::show();

    ObjMeshData objMesh;
    if (!obj_load("domeka.obj", objMesh))
    {
        TerminalDebug::println(PrintColorType_Red, "Failed to load OBJ mesh");
        return EXIT_FAILED;
    }

    BufferDesc meshBufferDesc;
    meshBufferDesc.data = objMesh.vertices;
    meshBufferDesc.size = objMesh.vertexCount * objMesh.stride * sizeof(float);
    meshBufferDesc.type = BufferType::Vertex;
    meshBufferDesc.usage = BufferUsage::Static;
    ScopedBuffer buffer_vertex(meshBufferDesc);

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

    // Uniform buffer da MVP. BufferUsage::Dynamic porque é reescrito todo frame
    // via buffer_update (o backend já mapeia o upload heap persistentemente,
    // então isso é só um memcpy por frame, sem custo de map/unmap).
    BufferDesc mvpBufferDesc;
    mvpBufferDesc.data = nullptr;
    mvpBufferDesc.size = sizeof(Mat4);
    mvpBufferDesc.type = BufferType::Uniform;
    mvpBufferDesc.usage = BufferUsage::Dynamic;
    ScopedBuffer mvpBuffer(mvpBufferDesc);

    TerminalDebug::println(PrintColorType_Yellow, "Chega aqui");

    DescriptorBinding mvpBinding = {};
mvpBinding.slot   = 0; // b0, bate com o cbuffer do shader
mvpBinding.type   = DescriptorType::UniformBuffer;
mvpBinding.buffer = mvpBuffer.handle;

DescriptorSetDesc descSetDesc = {};
descSetDesc.bindingCount = 1;
descSetDesc.bindings = &mvpBinding;
ScopedDescriptorSet mvpDescSet(descSetDesc);

    // --- Auto-enquadrar câmera a partir do bounding box do obj carregado ---
    float centerX = (objMesh.minBounds[0] + objMesh.maxBounds[0]) * 0.5f;
    float centerY = (objMesh.minBounds[1] + objMesh.maxBounds[1]) * 0.5f;
    float centerZ = (objMesh.minBounds[2] + objMesh.maxBounds[2]) * 0.5f;

    float extentX = objMesh.maxBounds[0] - objMesh.minBounds[0];
    float extentY = objMesh.maxBounds[1] - objMesh.minBounds[1];
    float extentZ = objMesh.maxBounds[2] - objMesh.minBounds[2];
    float radius = 0.5f * std::sqrt(extentX * extentX + extentY * extentY + extentZ * extentZ);
    if (radius < 0.0001f)
        radius = 1.0f; // evita divisão por zero em modelos degenerados

    float eyeDistance = radius * 2.5f;
    float nearZ = radius * 0.01f;
    float farZ = radius * 10.0f;

    float aspect = (float)WindowConfig::get_width() / (float)WindowConfig::get_height();

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
    pipeline.depthTestEnable = true;
    pipeline.depthWriteEnable = true;
    pipeline.depthCompare = CompareFunc::Less;
    pipeline.topology = PrimitiveTopology::TriangleList;
    pipeline.cullMode = CullMode::None;

    pipeline.vertexLayout.attributes[0].location = 0;
    pipeline.vertexLayout.attributes[0].format = VertexFormat::Float3; // position
    pipeline.vertexLayout.attributes[0].offset = 0;

    pipeline.vertexLayout.attributes[1].location = 1;
    pipeline.vertexLayout.attributes[1].format = VertexFormat::Float3; // normal (era Float4 color)
    pipeline.vertexLayout.attributes[1].offset = 3 * sizeof(float);

    pipeline.vertexLayout.attributeCount = 2;
    pipeline.vertexLayout.stride = 6 * sizeof(float); // era 7 (3 pos + 4 color), agora 3 pos + 3 normal

    // Nao chega aqui
    TerminalDebug::println(PrintColorType_Cyan, "Before pipeline_create");
    ScopedPipeline pHandle(pipeline);
    TerminalDebug::println(PrintColorType_Cyan, "After pipeline_create, valid=%d", pHandle.handle.handle.generation != Handle{HANDLE_INVALID}.generation);
    if (pHandle.handle.handle.generation == Handle{HANDLE_INVALID}.generation)
    {
        TerminalDebug::println(PrintColorType_Red, "ERRRO AQUI??");
        return EXIT_FAILED;
    }

    TerminalDebug::println(PrintColorType_Green, "float %ld", 3 * sizeof(float));

    MeshDesc canvasMeshDesc = {};
    canvasMeshDesc.vertexBuffer = buffer_vertex.handle;
    canvasMeshDesc.vertexCount = objMesh.vertexCount; // era hardcoded em 6
    ScopedMesh screenQuadMesh(canvasMeshDesc);

    while (WindowConfig::running())
    {
        Window::pool();

        Keyboard::update(0);

        // Câmera fixa olhando pro centro do modelo, um pouco deslocada nos 3 eixos
        // (evita o caso degenerado de eye == target quando o modelo é "achatado"
        // no eixo Z, por exemplo).
        Mat4 view = mat4_look_at(
            centerX + eyeDistance * 0.5f, centerY + eyeDistance * 0.3f, centerZ - eyeDistance,
            centerX, centerY, centerZ,
            0.0f, 1.0f, 0.0f);
        Mat4 proj = mat4_perspective_fov(3.14159265f / 4.0f, aspect, nearZ, farZ);
        Mat4 model = mat4_identity();
        Mat4 mvp = mat4_multiply(mat4_multiply(model, view), proj);

        Gpu::buffer_update(mvpBuffer.handle, 0, sizeof(Mat4), &mvp);

        Gpu::command_list_clear(&cmdList.list);

        Gpu::render_pass_begin(&renderPass.pass);
        Gpu::command_list_set_pipeline(&cmdList.list, pHandle.handle);
        Gpu::command_list_set_descriptor_set(&cmdList.list, mvpDescSet.handle, 0); // <- novo
        Gpu::command_list_draw_mesh(&cmdList.list, screenQuadMesh.handle, 1);
        Gpu::command_list_execute(&cmdList.list);
        Gpu::render_pass_end(&renderPass.pass);
    }
    return EXIT_SUCCESS;
}
