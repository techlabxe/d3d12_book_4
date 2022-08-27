#include "WaitableSwapchainApp.h"

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

WaitableSwapchainApp::WaitableSwapchainApp()
{
  m_camera.SetLookAt(
    XMFLOAT3(-830.0f, 370.0f, 76.0f),
    XMFLOAT3(  30.0f, -60.0f, -230.0f)
  );

  m_sceneParameters.lightDir = XMFLOAT4(0.5f, 0.25f, 0.1f, 0.0f);

  std::mt19937 mt(uint32_t(12356));
  std::uniform_real_distribution randRegionXZ(-650.0f, +650.0f);
  std::uniform_real_distribution randRegionY(0.0f, +300.0f);
  std::uniform_real_distribution randRadius(50.0f, 250.0f);
  for (int i = 0; i < _countof(m_sceneParameters.pointLights); ++i) {
    XMFLOAT4 p{};
    p.x = randRegionXZ(mt);
    p.y = randRegionY(mt);
    p.z = randRegionXZ(mt);
    p.w = randRadius(mt);
    m_sceneParameters.pointLights[i] = p;
  }

  m_sceneParameters.pointLightColors[0] = XMFLOAT4(1.0f, 0.1f, 0.1f, 0.0f);
  m_sceneParameters.pointLightColors[1] = XMFLOAT4(0.1f, 1.0f, 0.1f, 0.0f);
  m_sceneParameters.pointLightColors[2] = XMFLOAT4(0.1f, 0.1f, 1.0f, 0.0f);
  m_sceneParameters.pointLightColors[3] = XMFLOAT4(0.8f, 0.8f, 0.8f, 0.0f);

  m_sceneParameters.pointLightColors[4] = XMFLOAT4(1.0f, 1.0f, 0.1f, 0.0f);
  m_sceneParameters.pointLightColors[5] = XMFLOAT4(0.1f, 1.0f, 1.0f, 0.0f);
  m_sceneParameters.pointLightColors[6] = XMFLOAT4(1.0f, 1.0f, 0.0f, 0.0f);
  m_sceneParameters.pointLightColors[7] = XMFLOAT4(0.5f, 0.8f, 0.2f, 0.0f);

  m_sceneParameters.pointLights[0].x = 1000.0f;
  m_sceneParameters.pointLights[0].y = 200.0f;
  m_sceneParameters.pointLights[0].z = 0;
  m_sceneParameters.pointLights[0].w = 500.f;
}

void WaitableSwapchainApp::CreateRootSignatures()
{
  CD3DX12_STATIC_SAMPLER_DESC samplerDesc;
  samplerDesc.Init(
    0,
    D3D12_FILTER_MIN_MAG_MIP_LINEAR,
    D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    D3D12_TEXTURE_ADDRESS_MODE_WRAP);

  {
    CD3DX12_DESCRIPTOR_RANGE srvAlbedo;
    srvAlbedo.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

    CD3DX12_DESCRIPTOR_RANGE srvSpecular;
    srvSpecular.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1

    // RootSignature
    array<CD3DX12_ROOT_PARAMETER, 4> rootParams;
    rootParams[RP_SCENE_CB].InitAsConstantBufferView(0);
    rootParams[RP_MATERIAL].InitAsConstantBufferView(1);
    rootParams[RP_ALBEDO].InitAsDescriptorTable(1, &srvAlbedo);
    rootParams[RP_SPECULAR].InitAsDescriptorTable(1, &srvSpecular);

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Init(
      UINT(rootParams.size()), rootParams.data(),
      1, &samplerDesc,
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);


    ComPtr<ID3DBlob> signature, errBlob;
    D3D12SerializeRootSignature(&rootSignatureDesc,
      D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &errBlob);
    m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureZPrePass));
  }

  {
    CD3DX12_DESCRIPTOR_RANGE srvAlbedo;
    srvAlbedo.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

    CD3DX12_DESCRIPTOR_RANGE srvSpecular;
    srvSpecular.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1

    // RootSignature
    array<CD3DX12_ROOT_PARAMETER, 6> rootParams;
    rootParams[RP_SCENE_CB].InitAsConstantBufferView(0);
    rootParams[RP_MATERIAL].InitAsConstantBufferView(1);
    rootParams[RP_ALBEDO].InitAsDescriptorTable(1, &srvAlbedo);
    rootParams[RP_SPECULAR].InitAsDescriptorTable(1, &srvSpecular);

    rootParams[4].InitAsUnorderedAccessView(0); // u0
    rootParams[5].InitAsUnorderedAccessView(1); // u1

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
    CD3DX12_DESCRIPTOR_RANGE srvAlbedo, srvWorld, srvNormal, srvDepth;
    srvWorld.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
    srvNormal.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1
    srvAlbedo.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // t2
    srvDepth.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3); // t2

    array<CD3DX12_ROOT_PARAMETER, 5> rootParams;
    rootParams[RP_LIGHTING_SCENE_CB].InitAsConstantBufferView(0);
    rootParams[RP_LIGHTING_WPOS].InitAsDescriptorTable(1, &srvWorld);
    rootParams[RP_LIGHTING_NORMAL].InitAsDescriptorTable(1, &srvNormal);
    rootParams[RP_LIGHTING_ALBEDO].InitAsDescriptorTable(1, &srvAlbedo);
    rootParams[4].InitAsDescriptorTable(1, &srvDepth);

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Init(
      UINT(rootParams.size()), rootParams.data(),
      1, &samplerDesc);

    ComPtr<ID3DBlob> signature, errBlob;
    D3D12SerializeRootSignature(&rootSignatureDesc,
      D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &errBlob);
    m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureLighting));
  }
}

void WaitableSwapchainApp::Prepare()
{
  SetTitle("Waitable Swapchain Sample");
  CreateRootSignatures();

  D3D12_CLEAR_VALUE clearZero{}, clearBlack{};
  clearZero.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  clearBlack.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

  auto rtDescFloat4Tex = CD3DX12_RESOURCE_DESC::Tex2D(
    DXGI_FORMAT_R32G32B32A32_FLOAT, m_width, m_height, 1, 1
  );
  rtDescFloat4Tex.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  auto rtDescColorTex = CD3DX12_RESOURCE_DESC::Tex2D(
    DXGI_FORMAT_R8G8B8A8_UNORM, m_width, m_height, 1, 1
  );
  rtDescColorTex.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  auto stateRT = D3D12_RESOURCE_STATE_RENDER_TARGET;
  auto heapType = D3D12_HEAP_TYPE_DEFAULT;
  m_gbuffer.worldPosition = CreateResource(rtDescFloat4Tex, stateRT, &clearZero, heapType);
  m_gbuffer.worldNormal = CreateResource(rtDescFloat4Tex, stateRT, &clearZero, heapType);
  m_gbuffer.albedo = CreateResource(rtDescColorTex, stateRT, &clearBlack, heapType);
  

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDescFloat4{};
  srvDescFloat4.Format = rtDescFloat4Tex.Format;
  srvDescFloat4.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDescFloat4.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDescFloat4.Texture2D.MipLevels = rtDescFloat4Tex.MipLevels;
  srvDescFloat4.Texture2D.MostDetailedMip = 0;

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDescColor{};
  srvDescColor.Format = rtDescColorTex.Format;
  srvDescColor.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDescColor.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDescColor.Texture2D.MipLevels = rtDescColorTex.MipLevels;
  srvDescColor.Texture2D.MostDetailedMip = 0;

  // SRV生成.
  m_gbuffer.srvWorldPosition = GetDescriptorManager()->Alloc();
  m_gbuffer.srvWorldNormal = GetDescriptorManager()->Alloc();
  m_gbuffer.srvAlbedo = GetDescriptorManager()->Alloc();

  m_device->CreateShaderResourceView(
    m_gbuffer.worldPosition.Get(), &srvDescFloat4, m_gbuffer.srvWorldPosition);
  m_device->CreateShaderResourceView(
    m_gbuffer.worldNormal.Get(), &srvDescFloat4, m_gbuffer.srvWorldNormal);
  m_device->CreateShaderResourceView(
    m_gbuffer.albedo.Get(), &srvDescColor, m_gbuffer.srvAlbedo);

  // RTV生成.
  D3D12_RENDER_TARGET_VIEW_DESC rtvDescFloat4{}, rtvDescColor{};
  rtvDescFloat4.Format = srvDescFloat4.Format;
  rtvDescFloat4.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  rtvDescColor.Format = srvDescColor.Format;
  rtvDescColor.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
  m_gbuffer.rtvWorldPosition = m_heapRTV->Alloc();
  m_gbuffer.rtvWorldNormal = m_heapRTV->Alloc();
  m_gbuffer.rtvAlbedo = m_heapRTV->Alloc();
  m_device->CreateRenderTargetView(m_gbuffer.worldPosition.Get(), &rtvDescFloat4, m_gbuffer.rtvWorldPosition);
  m_device->CreateRenderTargetView(m_gbuffer.worldNormal.Get(), &rtvDescFloat4, m_gbuffer.rtvWorldNormal);
  m_device->CreateRenderTargetView(m_gbuffer.albedo.Get(), &rtvDescColor, m_gbuffer.rtvAlbedo);

  m_gbuffer.worldPosition->SetName(L"GBufferPosition");
  m_gbuffer.worldNormal->SetName(L"GBufferNormal");
  m_gbuffer.albedo->SetName(L"GBufferAlbedo");

  m_commandList->Reset(m_commandAllocators[0].Get(), nullptr);
  ID3D12DescriptorHeap* heaps[] = { m_heap->GetHeap().Get() };
  m_commandList->SetDescriptorHeaps(1, heaps);
  m_commandList->Close();

  auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ShaderParameters));
  m_sceneParameterCB = CreateConstantBuffers(cbDesc);

  m_model = model::LoadModelData("assets\\model\\sponza\\sponza.obj", this, model::ModelLoadFlag_Flip_UV);
  // このサンプルで使用するシェーダーパラメーター集合で定数バッファを作る.
  for (auto& batch : m_model.DrawBatches) {
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ShaderDrawMeshParameter));
    batch.materialParameterCB = CreateConstantBuffers(desc);
  }

  PreparePipeline();
  m_device->SetStablePowerState(TRUE);
}

void WaitableSwapchainApp::Cleanup()
{
  WaitForIdleGPU();

  m_model.Release();
}

void WaitableSwapchainApp::OnMouseButtonDown(UINT msg)
{
  auto io = ImGui::GetIO();
  if (io.WantCaptureMouse)
  {
    return;
  }
  m_camera.OnMouseButtonDown((int)msg);
}
void WaitableSwapchainApp::OnMouseButtonUp(UINT msg)
{
  m_camera.OnMouseButtonUp();
}

void WaitableSwapchainApp::OnMouseMove(UINT msg, int dx, int dy)
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

void WaitableSwapchainApp::PreparePipeline()
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
    shaderPS.load(L"shader.hlsl", Shader::Pixel, L"mainPS_zprepass", flags, defines);

    auto psoDesc = book_util::CreateDefaultPsoDesc(
      DXGI_FORMAT_UNKNOWN,
      rasterizerState,
      inputElementDesc.data(), UINT(inputElementDesc.size()),
      m_rootSignature,
      shaderVS.getCode(), shaderPS.getCode()
    );
    psoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    psoDesc.NumRenderTargets = 0;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_ZPREPASS] = pipelineState;
  }

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

    psoDesc.NumRenderTargets = 3;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_DEFAULT] = pipelineState;
  }


  {
    ComPtr<ID3D12PipelineState> pipelineState;
    Shader shaderVS, shaderPS;
    std::vector<wstring> flags;
    std::vector<Shader::DefineMacro> defines;

    shaderVS.load(L"shaderLighting.hlsl", Shader::Vertex, L"mainVS", flags, defines);
    shaderPS.load(L"shaderLighting.hlsl", Shader::Pixel, L"mainPS", flags, defines);

    auto psoDesc = book_util::CreateDefaultPsoDesc(
      DXGI_FORMAT_R8G8B8A8_UNORM,
      rasterizerState,
      nullptr, 0,
      m_rootSignatureLighting,
      shaderVS.getCode(), shaderPS.getCode()
    );
    psoDesc.DepthStencilState.DepthEnable = FALSE;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_DRAW_LIGHTING] = pipelineState;
  }

}


void WaitableSwapchainApp::Render()
{
  m_swapchain->WaitOnSwapchain();
  m_swapchain->WaitPreviousFrame(m_commandQueue, m_frameIndex, GpuWaitTimeout);

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

  auto mtxProj = XMMatrixPerspectiveFovRH(XMConvertToRadians(45.0f), float(m_width) / float(m_height), 1.0f, 5000.0f);
  XMStoreFloat4x4(&m_sceneParameters.view, XMMatrixTranspose(m_camera.GetViewMatrix()));
  XMStoreFloat4x4(&m_sceneParameters.proj, XMMatrixTranspose(mtxProj));
  XMStoreFloat4(&m_sceneParameters.cameraPosition, m_camera.GetPosition());

  auto rtv = m_swapchain->GetCurrentRTV();
  auto dsv = m_defaultDepthDSV;

  // カラーバッファ(レンダーターゲットビュー)のクリア
  float m_clearColor[4] = { 0.5f,0.25f,0.15f,0 };

  // デプスバッファ(デプスステンシルビュー)のクリア
  m_commandList->ClearDepthStencilView(
    dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

  // ビューポートとシザーのセット
  auto viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, float(m_width), float(m_height));
  auto scissorRect = CD3DX12_RECT(0, 0, LONG(m_width), LONG(m_height));
  m_commandList->RSSetViewports(1, &viewport);
  m_commandList->RSSetScissorRects(1, &scissorRect);


  float zeroFloat[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
  m_commandList->ClearRenderTargetView(m_gbuffer.rtvWorldPosition, zeroFloat, 0, nullptr);
  m_commandList->ClearRenderTargetView(m_gbuffer.rtvWorldNormal, zeroFloat, 0, nullptr);
  m_commandList->ClearRenderTargetView(m_gbuffer.rtvAlbedo, zeroFloat, 0, nullptr);
  m_commandList->ClearRenderTargetView(rtv, zeroFloat, 0, nullptr);

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

  // ZPrePass
  DrawModelInZPrePass();

  // Draw G-Buffer
  DrawModelInGBuffer();

  // Deferred Lighting.
  DeferredLightingPass();

  RenderHUD();

  // Barrier (テクスチャからレンダーテクスチャ, スワップチェイン表示可能)
  {
    auto stateSR = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    auto stateRT = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_RESOURCE_BARRIER barriers[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(m_gbuffer.worldPosition.Get(), stateSR, stateRT),
      CD3DX12_RESOURCE_BARRIER::Transition(m_gbuffer.worldNormal.Get(), stateSR, stateRT),
      CD3DX12_RESOURCE_BARRIER::Transition(m_gbuffer.albedo.Get(), stateSR, stateRT),
      m_swapchain->GetBarrierToPresent(),
    };

    m_commandList->ResourceBarrier(_countof(barriers), barriers);
  }

  m_commandList->Close();
  ID3D12CommandList* lists[] = { m_commandList.Get() };
  m_commandQueue->ExecuteCommandLists(1, lists);
  m_swapchain->Present(1, 0);
}

void WaitableSwapchainApp::RenderHUD()
{
  // ImGui
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  // ImGui ウィジェットを描画する.
  auto framerate = ImGui::GetIO().Framerate;
  ImGui::Begin("Information");
  ImGui::Text("Framerate %.3f ms", 1000.0f / framerate);
  float* lightDir = reinterpret_cast<float*>(&m_sceneParameters.lightDir);
  ImGui::InputFloat3("Light", lightDir, "%.2f");
  ImGui::End();

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
}

void WaitableSwapchainApp::DrawModelInZPrePass()
{
  D3D12_CPU_DESCRIPTOR_HANDLE handleDsv = m_defaultDepthDSV;
  m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &handleDsv);
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  m_commandList->SetPipelineState(m_pipelines[PSO_ZPREPASS].Get());
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
    m_commandList->SetGraphicsRootDescriptorTable(RP_ALBEDO, material.albedoSRV);
    m_commandList->SetGraphicsRootDescriptorTable(RP_SPECULAR, material.specularSRV);

    m_commandList->DrawIndexedInstanced(batch.indexCount, 1, batch.indexOffsetCount, batch.vertexOffsetCount, 0);
  }
}

void WaitableSwapchainApp::DrawModelInGBuffer()
{
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commandList->SetPipelineState(m_pipelines[PSO_DEFAULT].Get());

  // 描画先をセット
  D3D12_CPU_DESCRIPTOR_HANDLE handleRtvs[] = {
    m_gbuffer.rtvWorldPosition, m_gbuffer.rtvWorldNormal, m_gbuffer.rtvAlbedo };
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
    m_commandList->SetGraphicsRootDescriptorTable(RP_ALBEDO, material.albedoSRV);
    m_commandList->SetGraphicsRootDescriptorTable(RP_SPECULAR, material.specularSRV);

    m_commandList->DrawIndexedInstanced(batch.indexCount, 1, batch.indexOffsetCount, batch.vertexOffsetCount, 0);
  }

  // Barrier (レンダーテクスチャからテクスチャ)
  auto stateRT = D3D12_RESOURCE_STATE_RENDER_TARGET;
  auto stateSR = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  D3D12_RESOURCE_BARRIER barriers[] = {
    CD3DX12_RESOURCE_BARRIER::Transition(m_gbuffer.worldPosition.Get(), stateRT, stateSR),
    CD3DX12_RESOURCE_BARRIER::Transition(m_gbuffer.worldNormal.Get(), stateRT, stateSR),
    CD3DX12_RESOURCE_BARRIER::Transition(m_gbuffer.albedo.Get(), stateRT, stateSR),

  };
  m_commandList->ResourceBarrier(_countof(barriers), barriers);
}

void WaitableSwapchainApp::DeferredLightingPass()
{
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  m_commandList->SetGraphicsRootSignature(m_rootSignatureLighting.Get());
  m_commandList->SetPipelineState(m_pipelines[PSO_DRAW_LIGHTING].Get());

  D3D12_CPU_DESCRIPTOR_HANDLE handleRtv[] = { m_swapchain->GetCurrentRTV() };
  m_commandList->OMSetRenderTargets(1, handleRtv, FALSE, nullptr);
  m_commandList->SetGraphicsRootConstantBufferView(RP_LIGHTING_SCENE_CB, m_sceneParameterCB[m_frameIndex]->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootDescriptorTable(RP_LIGHTING_WPOS, m_gbuffer.srvWorldPosition);
  m_commandList->SetGraphicsRootDescriptorTable(RP_LIGHTING_NORMAL, m_gbuffer.srvWorldNormal);
  m_commandList->SetGraphicsRootDescriptorTable(RP_LIGHTING_ALBEDO, m_gbuffer.srvAlbedo);

  m_commandList->DrawInstanced(4, 1, 0, 0);
}
