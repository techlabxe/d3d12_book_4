#include "NormalMapApp.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include <DirectXTex.h>
#include <fstream>
#include <stack>
#include <sstream>
#include <random>

using namespace std;
using namespace DirectX;
namespace fs = std::filesystem;

NormalMapApp::NormalMapApp()
{
  m_camera.SetLookAt(
    XMFLOAT3( 0.0f, 250.0f, 120.0f),
    XMFLOAT3( 0.0f, 5.0f, 0.0f)
  );

  m_sceneParameters.lightDir = XMFLOAT4(0.5f, 0.25f, 0.1f, 0.0f);
}

void NormalMapApp::CreateRootSignatures()
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

    CD3DX12_DESCRIPTOR_RANGE srvNormalMap;
    srvNormalMap.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1

    CD3DX12_DESCRIPTOR_RANGE srvHeightMap;
    srvHeightMap.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // t1

    // RootSignature
    array<CD3DX12_ROOT_PARAMETER, 5> rootParams;
    rootParams[RP_SCENE_CB].InitAsConstantBufferView(0);
    rootParams[RP_MATERIAL].InitAsConstantBufferView(1);
    rootParams[RP_BASE_COLOR].InitAsDescriptorTable(1, &srvBaseColor);
    rootParams[RP_NORMAL_MAP].InitAsDescriptorTable(1, &srvNormalMap);
    rootParams[RP_HEIGHT_MAP].InitAsDescriptorTable(1, &srvHeightMap);

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
}

void NormalMapApp::Prepare()
{
  SetTitle("NormalMap");
  CreateRootSignatures();

  m_commandList->Reset(m_commandAllocators[0].Get(), nullptr);
  ID3D12DescriptorHeap* heaps[] = { m_heap->GetHeap().Get() };
  m_commandList->SetDescriptorHeaps(1, heaps);
  m_commandList->Close();

  auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ShaderParameters));
  m_sceneParameterCB = CreateConstantBuffers(cbDesc);

  auto loadFlags = model::ModelLoadFlag::ModelLoadFlag_CalcTangent;
  m_model = model::LoadModelData("assets/model/plane.obj", this, loadFlags);
  // このサンプルで使用するシェーダーパラメーター集合で定数バッファを作る.
  for (auto& batch : m_model.DrawBatches) {
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ShaderDrawMeshParameter));
    batch.materialParameterCB = CreateConstantBuffers(desc);
  }

  m_texPlaneBase = LoadTexture("assets/texture/wood.jpg");
  m_texPlaneNormal = LoadTexture("assets/texture/four_NM_height_c.tga");
  m_texPlaneHeight = LoadTexture("assets/texture/four_NM_height_a.tga");

  //m_texPlaneNormal = LoadTexture("assets/texture/normal_hillT.png");
  //m_texPlaneHeight = LoadTexture("assets/texture/height_hillT.png");

  PreparePipeline();
}

void NormalMapApp::Cleanup()
{
  WaitForIdleGPU();

  m_model.Release();
}

void NormalMapApp::OnMouseButtonDown(UINT msg)
{
  auto io = ImGui::GetIO();
  if (io.WantCaptureMouse)
  {
    return;
  }
  m_camera.OnMouseButtonDown((int)msg);
}
void NormalMapApp::OnMouseButtonUp(UINT msg)
{
  m_camera.OnMouseButtonUp();
}

void NormalMapApp::OnMouseMove(UINT msg, int dx, int dy)
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

void NormalMapApp::PreparePipeline()
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

    { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT,    3, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,   4, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
      inputElementDesc.data(), uint32_t(inputElementDesc.size()),
      m_rootSignature,
      shaderVS.getCode(), shaderPS.getCode()
    );

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_DEFAULT] = pipelineState;
  }

}


void NormalMapApp::Render()
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
  m_sceneParameters.drawFlag = m_mode;

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

  DrawModelWithNormalMap();

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

void NormalMapApp::RenderHUD()
{
  // ImGui
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  // ImGui ウィジェットを描画する.
  auto framerate = ImGui::GetIO().Framerate;
  ImGui::Begin("Information");
  ImGui::Text("Frametime %.3f ms", 1000.0f / framerate);

  auto eye = m_camera.GetPosition();
  ImGui::Text("EyePos (%.2f, %.2f, %.2f)", eye.m128_f32[0], eye.m128_f32[1], eye.m128_f32[2]);
  float* lightDir = reinterpret_cast<float*>(&m_sceneParameters.lightDir);
  ImGui::InputFloat3("Light", lightDir, "%.2f");

  ImGui::Combo("Mode", (int*) & m_mode, "NormalMap\0ParallaxMap\0ParallaxOcclusion\0\0");

  ImGui::InputFloat("HScale", &m_sceneParameters.heightScale);
  ImGui::End();

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
}


void NormalMapApp::DrawModelWithNormalMap()
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
    if (m_model.Tangent) {
      m_commandList->IASetVertexBuffers(3, 1, &m_model.vertexBufferViews[model::ModelAsset::VBV_Tangent]);
    }

    m_commandList->IASetIndexBuffer(&m_model.indexBufferView);

    if (!batch.boneMatrixPalette.empty()) {
      auto& bonesCB = batch.boneMatrixPalette[m_frameIndex];
      m_commandList->SetGraphicsRootConstantBufferView(1, bonesCB->GetGPUVirtualAddress());
    }
    const auto& material = m_model.materials[batch.materialIndex];
    auto& materialCB = batch.materialParameterCB[m_frameIndex];
    m_commandList->SetGraphicsRootConstantBufferView(RP_MATERIAL, materialCB->GetGPUVirtualAddress());
    m_commandList->SetGraphicsRootDescriptorTable(RP_BASE_COLOR, m_texPlaneBase.srv);
    m_commandList->SetGraphicsRootDescriptorTable(RP_NORMAL_MAP, m_texPlaneNormal.srv);
    m_commandList->SetGraphicsRootDescriptorTable(RP_HEIGHT_MAP, m_texPlaneHeight.srv);

    m_commandList->DrawIndexedInstanced(batch.indexCount, 1, batch.indexOffsetCount, batch.vertexOffsetCount, 0);
  }
}

