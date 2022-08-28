#include "GPUParticleApp.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include <DirectXTex.h>
#include <fstream>
#include <stack>
#include <sstream>
#include <random>

#include <filesystem>

using namespace std;
using namespace DirectX;
namespace fs = std::filesystem;


GPUParticleApp::GPUParticleApp()
{
  m_camera.SetLookAt(
    XMFLOAT3( 0.0f, 25.0f, 50.0f),
    XMFLOAT3( 0.0f, 5.0f, 0.0f)
  );

  m_sceneParameters.lightDir = XMFLOAT4(0.5f, 0.25f, 0.1f, 0.0f);
  m_sceneParameters.forceCenter1 = XMFLOAT4(15.0f, 0.0f, 0.0f, 18.0f);
  m_sceneParameters.particleColors[0] = XMFLOAT4(1.0f, 0.1f, 0.1f, 0.0f);
  m_sceneParameters.particleColors[1] = XMFLOAT4(0.1f, 1.0f, 0.1f, 0.0f);
  m_sceneParameters.particleColors[2] = XMFLOAT4(0.1f, 0.1f, 1.0f, 0.0f);
  m_sceneParameters.particleColors[3] = XMFLOAT4(0.8f, 0.8f, 0.8f, 0.0f);

  m_sceneParameters.particleColors[4] = XMFLOAT4(1.0f, 1.0f, 0.1f, 0.0f);
  m_sceneParameters.particleColors[5] = XMFLOAT4(0.1f, 1.0f, 1.0f, 0.0f);
  m_sceneParameters.particleColors[6] = XMFLOAT4(1.0f, 1.0f, 0.0f, 0.0f);
  m_sceneParameters.particleColors[7] = XMFLOAT4(0.5f, 0.8f, 0.2f, 0.0f);

}

void GPUParticleApp::CreateRootSignatures()
{
  CD3DX12_STATIC_SAMPLER_DESC samplerDesc;
  samplerDesc.Init(
    0,
    D3D12_FILTER_MIN_MAG_MIP_LINEAR,
    D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    D3D12_TEXTURE_ADDRESS_MODE_WRAP);

  {
    CD3DX12_DESCRIPTOR_RANGE srvBaseColor;
    srvBaseColor.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

    // RootSignature
    array<CD3DX12_ROOT_PARAMETER, 3> rootParams;
    rootParams[RP_SCENE_CB].InitAsConstantBufferView(0);
    rootParams[RP_MATERIAL].InitAsConstantBufferView(1);
    rootParams[RP_BASE_COLOR].InitAsDescriptorTable(1, &srvBaseColor);

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Init(
      UINT(rootParams.size()), rootParams.data(),
      1, &samplerDesc,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);


    ComPtr<ID3DBlob> signature, errBlob;
    D3D12SerializeRootSignature(&rootSignatureDesc,
      D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &errBlob);
    m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));
  }

  {
    // カウンタ付き UAV はルートパラメータとして設定できない.
    CD3DX12_DESCRIPTOR_RANGE uavIndexList{};
    uavIndexList.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);

    array<CD3DX12_ROOT_PARAMETER, 3> rootParams;
    rootParams[RP_CS_SCENE_CB].InitAsConstantBufferView(0); // b0: Params
    rootParams[RP_CS_PARTICLE].InitAsUnorderedAccessView(0);// u0: Particles
    rootParams[RP_CS_PARTICLE_INDEXLIST].InitAsDescriptorTable(1, &uavIndexList); // u1: ParticleIndexList

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Init(
      UINT(rootParams.size()), rootParams.data(),
      1, &samplerDesc);

    ComPtr<ID3DBlob> signature, errBlob;
    D3D12SerializeRootSignature(&rootSignatureDesc,
      D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &errBlob);
    m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureCompute));
  }
  {
    //CD3DX12_DESCRIPTOR_RANGE srvAlbedo;
    //srvAlbedo.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

    // RootSignature
    array<CD3DX12_ROOT_PARAMETER, 2> rootParams;
    rootParams[RP_PARTICLE_DRAW_SCENE_CB].InitAsConstantBufferView(0);
    rootParams[RP_PARTICLE_DRAW_DATA].InitAsUnorderedAccessView(0);

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Init(
      UINT(rootParams.size()), rootParams.data(),
      1, &samplerDesc,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature, errBlob;
    D3D12SerializeRootSignature(&rootSignatureDesc,
      D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &errBlob);
    m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureParticleDraw));
  }

  {
    // RootSignature
    CD3DX12_DESCRIPTOR_RANGE rangeTex{};
    rangeTex.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    array<CD3DX12_ROOT_PARAMETER, 3> rootParams;
    rootParams[RP_PARTICLE_DRAW_TEX_SCENE_CB].InitAsConstantBufferView(0);
    rootParams[RP_PARTICLE_DRAW_TEX_DATA].InitAsUnorderedAccessView(0);
    rootParams[RP_PARTICLE_DRAW_TEX_TEXTURE].InitAsDescriptorTable(1, &rangeTex);

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Init(
      UINT(rootParams.size()), rootParams.data(),
      1, &samplerDesc,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature, errBlob;
    D3D12SerializeRootSignature(&rootSignatureDesc,
      D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &errBlob);
    m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureParticleTexDraw));
  }
}

void GPUParticleApp::Prepare()
{
  SetTitle("GPUParticle");
  CreateRootSignatures();

  m_commandList->Reset(m_commandAllocators[0].Get(), nullptr);
  ID3D12DescriptorHeap* heaps[] = { m_heap->GetHeap().Get() };
  m_commandList->SetDescriptorHeaps(1, heaps);
  m_commandList->Close();

  auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ShaderParameters));
  m_sceneParameterCB = CreateConstantBuffers(cbDesc);

  m_texPlaneBase = LoadTexture("assets/texture/block.jpg");
  m_model = model::LoadModelData("assets/model/plane.obj", this);
  // このサンプルで使用するシェーダーパラメーター集合で定数バッファを作る.
  for (auto& batch : m_model.DrawBatches) {
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ShaderDrawMeshParameter));
    batch.materialParameterCB = CreateConstantBuffers(desc);
  }

  UINT64 bufferSize;
  bufferSize = sizeof(GpuParticleElement) * MaxParticleCount;
  auto resDescParticleElement = CD3DX12_RESOURCE_DESC::Buffer(
    bufferSize,
    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
  );
  m_gpuParticleElement = CreateResource(resDescParticleElement, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, D3D12_HEAP_TYPE_DEFAULT);
  m_gpuParticleElement->SetName(L"ParticleElement");

  bufferSize = sizeof(UINT) * MaxParticleCount; 
  UINT uavCounterAlign = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT - 1;
  bufferSize = UINT64(bufferSize + uavCounterAlign) & ~uavCounterAlign;
  bufferSize += sizeof(XMFLOAT4);   // カウンタをこの場所先頭に配置.

  auto resDescParticleIndexList = CD3DX12_RESOURCE_DESC::Buffer(
    bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
  m_gpuParticleIndexList = CreateResource(resDescParticleIndexList, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, D3D12_HEAP_TYPE_DEFAULT);
  m_gpuParticleIndexList->SetName(L"ParticleIndexList");

  UINT64 offsetToCounter = bufferSize - sizeof(XMFLOAT4);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
  uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uavDesc.Format = DXGI_FORMAT_UNKNOWN;
  uavDesc.Buffer.NumElements = MaxParticleCount;
  // インデックス用バッファの後方でカウンタを設置する.
  uavDesc.Buffer.CounterOffsetInBytes = offsetToCounter;
  uavDesc.Buffer.StructureByteStride = sizeof(UINT);

  m_uavParticleIndexList = m_heap->Alloc();
  m_device->CreateUnorderedAccessView(
    m_gpuParticleIndexList.Get(),
    m_gpuParticleIndexList.Get(),
    &uavDesc, m_uavParticleIndexList
  );

  // テクスチャ付きパーティクル描画用.
  std::vector<ParticleVertex> vbParticle = {
    { XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
    { XMFLOAT3(-1.0f,-1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
    { XMFLOAT3( 1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
    { XMFLOAT3( 1.0f,-1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
  };
  std::vector<uint32_t> ibParticle = {
    0, 1, 2,  2, 1, 3,
  };
  m_modelParticleBoard = CreateSimpleModel(
    vbParticle, ibParticle
  );
  m_texParticle = LoadTexture("assets/texture/particle.png");

  PreparePipeline();

}

void GPUParticleApp::Cleanup()
{
  WaitForIdleGPU();

  m_model.Release();
}

void GPUParticleApp::OnMouseButtonDown(UINT msg)
{
  auto io = ImGui::GetIO();
  if (io.WantCaptureMouse)
  {
    return;
  }
  m_camera.OnMouseButtonDown((int)msg);
}
void GPUParticleApp::OnMouseButtonUp(UINT msg)
{
  m_camera.OnMouseButtonUp();
}

void GPUParticleApp::OnMouseMove(UINT msg, int dx, int dy)
{
  float fdx = float(-dx) / m_width;
  float fdy = float(dy) / m_height;

  auto io = ImGui::GetIO();
  if (io.WantCaptureMouse)
  {
    return;
  }
  m_camera.OnMouseMove(fdx, fdy);
}


void GPUParticleApp::PreparePipeline()
{
  ComPtr<ID3DBlob> errBlob;

  auto rasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  rasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  rasterizerState.FrontCounterClockwise = true;


  std::vector<D3D12_INPUT_ELEMENT_DESC> inputElementDesc;
  inputElementDesc = {
    { "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,    2, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  HRESULT hr;

  {
    ComPtr<ID3D12PipelineState> pipelineState;
    Shader shaderVS, shaderPS;
    std::vector<wstring> flags;
    std::vector<Shader::DefineMacro> defines;

    shaderVS.load(L"shader.hlsl", Shader::Vertex, L"mainVS", flags, defines);
    shaderPS.load(L"shader.hlsl", Shader::Pixel, L"mainPS", flags, defines);

    auto psoDesc = book_util::CreateDefaultPsoDesc(
      DXGI_FORMAT_R8G8B8A8_UNORM,
      rasterizerState,
      inputElementDesc.data(), UINT(inputElementDesc.size()),
      m_rootSignature,
      shaderVS.getCode(), shaderPS.getCode()
    );

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_DEFAULT] = pipelineState;
  }

  {
    std::vector<wstring> flags;
    std::vector<Shader::DefineMacro> defines;

    Shader shaderInitCS, shaderEmitCS, shaderUpdateCS;
    shaderInitCS.load(L"shaderGpuParticle.hlsl", Shader::Compute, L"initParticle", flags, defines);
    shaderEmitCS.load(L"shaderGpuParticle.hlsl", Shader::Compute, L"emitParticle", flags, defines);
    shaderUpdateCS.load(L"shaderGpuParticle.hlsl", Shader::Compute, L"updateParticle", flags, defines);

    ComPtr<ID3D12PipelineState> pipelineState;
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_rootSignatureCompute.Get();
    
    psoDesc.CS = shaderInitCS.get();
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateComputePipelineState Failed.");
    m_pipelines[PSO_CS_INIT] = pipelineState;

    psoDesc.CS = shaderEmitCS.get();
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateComputePipelineState Failed.");
    m_pipelines[PSO_CS_EMIT] = pipelineState;

    psoDesc.CS = shaderUpdateCS.get();
    hr = m_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateComputePipelineState Failed.");
    m_pipelines[PSO_CS_UPDATE] = pipelineState;
  }
  {
    ComPtr<ID3D12PipelineState> pipelineState;
    Shader shaderVS, shaderPS;
    std::vector<wstring> flags;
    std::vector<Shader::DefineMacro> defines;

    shaderVS.load(L"shaderGpuParticleDraw.hlsl", Shader::Vertex, L"mainVS", flags, defines);
    shaderPS.load(L"shaderGpuParticleDraw.hlsl", Shader::Pixel, L"mainPS", flags, defines);

    auto psoDesc = book_util::CreateDefaultPsoDesc(
      DXGI_FORMAT_R8G8B8A8_UNORM,
      rasterizerState,
      nullptr, 0,
      m_rootSignatureParticleDraw,
      shaderVS.getCode(), shaderPS.getCode()
    );
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_DRAW_PARTICLE] = pipelineState;
  }


  //m_rootSignatureParticleTexDraw
  {
    ComPtr<ID3D12PipelineState> pipelineState;
    Shader shaderVS, shaderPS;
    std::vector<wstring> flags;
    std::vector<Shader::DefineMacro> defines;

    shaderVS.load(L"shaderGpuParticleDraw.hlsl", Shader::Vertex, L"mainVSEx", flags, defines);
    shaderPS.load(L"shaderGpuParticleDraw.hlsl", Shader::Pixel, L"mainPSEx", flags, defines);

    inputElementDesc = {
      { "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
      { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    auto psoDesc = book_util::CreateDefaultPsoDesc(
      DXGI_FORMAT_R8G8B8A8_UNORM,
      rasterizerState,
      nullptr, 0,
      m_rootSignatureParticleTexDraw,
      shaderVS.getCode(), shaderPS.getCode()
    );
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    auto& renderTarget = psoDesc.BlendState.RenderTarget[0];
    renderTarget.BlendEnable = TRUE;
    renderTarget.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    renderTarget.DestBlend = D3D12_BLEND_ONE;

    psoDesc.InputLayout.NumElements = uint32_t(inputElementDesc.size());
    psoDesc.InputLayout.pInputElementDescs = inputElementDesc.data();

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_DRAW_PARTICLE_USE_TEX] = pipelineState;
  }


}


void GPUParticleApp::Render()
{
  m_frameIndex = m_swapchain->GetCurrentBackBufferIndex();
  m_commandAllocators[m_frameIndex]->Reset();
  m_commandList->Reset(
    m_commandAllocators[m_frameIndex].Get(), nullptr
  );

  // スワップチェイン表示可能からレンダーターゲット描画可能へ
  auto barrierToRT = m_swapchain->GetBarrierToRenderTarget();
  m_commandList->ResourceBarrier(1, &barrierToRT);

  ID3D12DescriptorHeap* heaps[] = { m_heap->GetHeap().Get() };
  m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);

  //m_scenePatameters.lightDir = XMFLOAT4(-600.0f, 650.0f, 100.0f, 0.0f);
  auto mtxProj = XMMatrixPerspectiveFovRH(XMConvertToRadians(45.0f), float(m_width) / float(m_height), 1.0f, 5000.0f);
  XMStoreFloat4x4(&m_sceneParameters.view, XMMatrixTranspose(m_camera.GetViewMatrix()));
  XMStoreFloat4x4(&m_sceneParameters.proj, XMMatrixTranspose(mtxProj));
  XMStoreFloat4(&m_sceneParameters.cameraPosition, m_camera.GetPosition());
  m_sceneParameters.MaxParticleCount = MaxParticleCount;

  auto mtxBillboard = m_camera.GetViewMatrix();
  mtxBillboard.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
  XMStoreFloat4x4(&m_sceneParameters.matBillboard, XMMatrixTranspose(mtxBillboard));

  auto rtv = m_swapchain->GetCurrentRTV();
  auto dsv = m_defaultDepthDSV;

  // カラーバッファ(レンダーターゲットビュー)のクリア
  float zeroFloat[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
  m_commandList->ClearRenderTargetView(rtv, zeroFloat, 0, nullptr);

  // デプスバッファ(デプスステンシルビュー)のクリア
  m_commandList->ClearDepthStencilView(
    dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

  // ビューポートとシザーのセット
  auto viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, float(m_width), float(m_height));
  auto scissorRect = CD3DX12_RECT(0, 0, LONG(m_width), LONG(m_height));
  m_commandList->RSSetViewports(1, &viewport);
  m_commandList->RSSetScissorRects(1, &scissorRect);



  // Material/Batch's Parameter Update
  for (auto& batch : m_model.DrawBatches) {
    auto materialCB = batch.materialParameterCB[m_frameIndex];
    const auto& material = m_model.materials[batch.materialIndex];
    ShaderDrawMeshParameter params{};
    params.mtxWorld = XMMatrixTranspose(XMMatrixIdentity());
    params.diffuse.x = material.diffuse.x;
    params.diffuse.y = material.diffuse.y;
    params.diffuse.z = material.diffuse.z;
    params.diffuse.w = material.shininess;
    params.ambient.x = 0.2f;
    params.ambient.y = 0.2f;
    params.ambient.z = 0.2f;

    WriteToUploadHeapMemory(materialCB.Get(), sizeof(ShaderDrawMeshParameter), &params);
  }

  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  WriteToUploadHeapMemory(m_sceneParameterCB[m_frameIndex].Get(), sizeof(ShaderParameters), &m_sceneParameters);
  m_commandList->SetGraphicsRootConstantBufferView(RP_SCENE_CB, m_sceneParameterCB[m_frameIndex]->GetGPUVirtualAddress());

  if (m_frameCount == 0) {
    // Particle の初期化コード.
    m_commandList->SetComputeRootSignature(m_rootSignatureCompute.Get());
    m_commandList->SetComputeRootConstantBufferView(RP_CS_SCENE_CB, m_sceneParameterCB[m_frameIndex]->GetGPUVirtualAddress());
    m_commandList->SetComputeRootUnorderedAccessView(RP_CS_PARTICLE, m_gpuParticleElement->GetGPUVirtualAddress());
    m_commandList->SetComputeRootDescriptorTable(RP_CS_PARTICLE_INDEXLIST, m_uavParticleIndexList);
    m_commandList->SetPipelineState(m_pipelines[PSO_CS_INIT].Get());

    UINT invokeCount = MaxParticleCount / 32 + 1;
    m_commandList->Dispatch(invokeCount, 1, 1);
  }

  {
    // Particle の発生.
    m_commandList->SetComputeRootSignature(m_rootSignatureCompute.Get());
    m_commandList->SetComputeRootConstantBufferView(RP_CS_SCENE_CB, m_sceneParameterCB[m_frameIndex]->GetGPUVirtualAddress());
    m_commandList->SetComputeRootUnorderedAccessView(RP_CS_PARTICLE, m_gpuParticleElement->GetGPUVirtualAddress());
    m_commandList->SetComputeRootDescriptorTable(RP_CS_PARTICLE_INDEXLIST, m_uavParticleIndexList);
    m_commandList->SetPipelineState(m_pipelines[PSO_CS_EMIT].Get());

    UINT invokeCount = MaxParticleCount / 32 + 1;
    {
      m_commandList->Dispatch(2, 1, 1);
    }

    CD3DX12_RESOURCE_BARRIER barriers[] = {
      CD3DX12_RESOURCE_BARRIER::UAV(m_gpuParticleElement.Get()),
      CD3DX12_RESOURCE_BARRIER::UAV(m_gpuParticleIndexList.Get()),
    };
    m_commandList->ResourceBarrier(_countof(barriers), barriers);

    // Particle の更新処理.
    m_commandList->SetPipelineState(m_pipelines[PSO_CS_UPDATE].Get());
    m_commandList->Dispatch(invokeCount, 1, 1);
    m_commandList->ResourceBarrier(_countof(barriers), barriers);
  }

  DrawModelWithNormalMap();

#if 0
  {
    m_commandList->SetGraphicsRootSignature(m_rootSignatureParticleDraw.Get());
    m_commandList->SetPipelineState(m_pipelines[PSO_DRAW_PARTICLE].Get());
    
    m_commandList->SetGraphicsRootConstantBufferView(RP_PARTICLE_DRAW_SCENE_CB, m_sceneParameterCB[m_frameIndex]->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootUnorderedAccessView(RP_PARTICLE_DRAW_DATA, m_gpuParticleElement->GetGPUVirtualAddress());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    m_commandList->DrawInstanced(MaxParticleCount, 1, 0, 0);
  }
#else
  {
    m_commandList->SetGraphicsRootSignature(m_rootSignatureParticleTexDraw.Get());
    m_commandList->SetPipelineState(m_pipelines[PSO_DRAW_PARTICLE_USE_TEX].Get());

    m_commandList->SetGraphicsRootConstantBufferView(RP_PARTICLE_DRAW_TEX_SCENE_CB, m_sceneParameterCB[m_frameIndex]->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootUnorderedAccessView(RP_PARTICLE_DRAW_TEX_DATA, m_gpuParticleElement->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootDescriptorTable(RP_PARTICLE_DRAW_TEX_TEXTURE, m_texParticle.srv);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->IASetIndexBuffer(&m_modelParticleBoard.ibView);
    m_commandList->IASetVertexBuffers(0, 1, &m_modelParticleBoard.vbView);

    m_commandList->DrawIndexedInstanced(6, MaxParticleCount, 0, 0, 0);
  }
#endif

  RenderHUD();

  // Barrier (テクスチャからレンダーテクスチャ, スワップチェイン表示可能)
  {
    auto stateSR = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    auto stateRT = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_BARRIER barriers[] = {
      m_swapchain->GetBarrierToPresent(),
    };

    m_commandList->ResourceBarrier(_countof(barriers), barriers);
  }
  m_commandList->Close();
  ID3D12CommandList* lists[] = { m_commandList.Get() };
  m_commandQueue->ExecuteCommandLists(1, lists);

  m_swapchain->Present(1, 0);
  m_swapchain->WaitPreviousFrame(m_commandQueue, m_frameIndex, GpuWaitTimeout);
  ++m_frameCount;
}

void GPUParticleApp::RenderHUD()
{
  // ImGui
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  // ImGui ウィジェットを描画する.
  auto framerate = ImGui::GetIO().Framerate;
  ImGui::Begin("Information");
  ImGui::Text("Framerate %.3f ms", 1000.0f / framerate);

  auto eye = m_camera.GetPosition();
  ImGui::Text("EyePos (%.2f, %.2f, %.2f)", eye.m128_f32[0], eye.m128_f32[1], eye.m128_f32[2]);
  float* lightDir = reinterpret_cast<float*>(&m_sceneParameters.lightDir);
  ImGui::InputFloat3("Light", lightDir, "%.2f");

  float* center1 = reinterpret_cast<float*>(&m_sceneParameters.forceCenter1);
  ImGui::InputFloat4("Sphere(COL)", center1, "%.2f");

  ImGui::End();

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
}

void GPUParticleApp::DrawModelWithNormalMap()
{
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commandList->SetPipelineState(m_pipelines[PSO_DEFAULT].Get());

  // 描画先をセット
  D3D12_CPU_DESCRIPTOR_HANDLE handleRtvs[] = { m_swapchain->GetCurrentRTV() };
  D3D12_CPU_DESCRIPTOR_HANDLE handleDsv = m_defaultDepthDSV;
  m_commandList->OMSetRenderTargets(_countof(handleRtvs), handleRtvs, FALSE, &handleDsv);

  for (auto& batch : m_model.DrawBatches) {
    std::vector<D3D12_VERTEX_BUFFER_VIEW> vbViews = {
      m_model.vertexBufferViews[model::ModelAsset::VBV_Position],
      m_model.vertexBufferViews[model::ModelAsset::VBV_Normal],
      m_model.vertexBufferViews[model::ModelAsset::VBV_UV0],
    };
    m_commandList->IASetVertexBuffers(0, UINT(vbViews.size()), vbViews.data());
    m_commandList->IASetIndexBuffer(&m_model.indexBufferView);

    const auto& material = m_model.materials[batch.materialIndex];
    auto& materialCB = batch.materialParameterCB[m_frameIndex];
    m_commandList->SetGraphicsRootConstantBufferView(RP_MATERIAL, materialCB->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootDescriptorTable(RP_BASE_COLOR, m_texPlaneBase.srv);

    m_commandList->DrawIndexedInstanced(batch.indexCount, 1, batch.indexOffsetCount, batch.vertexOffsetCount, 0);
  }
}

