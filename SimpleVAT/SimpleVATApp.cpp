#include "SimpleVATApp.h"

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

SimpleVATApp::SimpleVATApp()
{
  m_camera.SetLookAt(
    XMFLOAT3( 10.5f, 9.5f, 10.5f),
    XMFLOAT3( 0.28f, 2.5f, -0.3f)
  );

  m_sceneParameters.lightDir = XMFLOAT4(0.5f, 0.25f, 0.1f, 0.0f);
  m_sceneParameters.animationFrame = 0;

  m_vatFluidMaterial.mtxWorld = XMMatrixTranspose(XMMatrixIdentity());
  m_vatFluidMaterial.diffuse = XMFLOAT4(0.65f, 0.85f, 1, 1);
  m_vatFluidMaterial.ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);

  m_vatDestroyMaterial.mtxWorld = XMMatrixTranspose(XMMatrixIdentity());
  m_vatDestroyMaterial.diffuse = XMFLOAT4(0.85f, 0.25f, 0.3f, 1);
  m_vatDestroyMaterial.ambient = XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f);
}

void SimpleVATApp::CreateRootSignatures()
{
  CD3DX12_STATIC_SAMPLER_DESC samplerDesc;
  samplerDesc.Init(
    0,
    D3D12_FILTER_MIN_MAG_MIP_LINEAR,
    D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    D3D12_TEXTURE_ADDRESS_MODE_WRAP);

  CD3DX12_STATIC_SAMPLER_DESC vatSamplerDesc;
  vatSamplerDesc.Init(
    0,
    D3D12_FILTER_MIN_MAG_MIP_POINT,
    D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
    D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

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
    CD3DX12_DESCRIPTOR_RANGE srvVatPos;
    srvVatPos.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

    CD3DX12_DESCRIPTOR_RANGE srvVatNormal;
    srvVatNormal.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1

    // RootSignature
    array<CD3DX12_ROOT_PARAMETER, 4> rootParams;
    rootParams[RP_VAT_SCENE_CB].InitAsConstantBufferView(0);
    rootParams[RP_VAT_MATERIAL].InitAsConstantBufferView(1);
    rootParams[RP_VAT_POSITON].InitAsDescriptorTable(1, &srvVatPos);
    rootParams[RP_VAT_NORMAL].InitAsDescriptorTable(1, &srvVatNormal);

    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.Init(
      UINT(rootParams.size()), rootParams.data(),
      1, &vatSamplerDesc );

    ComPtr<ID3DBlob> signature, errBlob;
    D3D12SerializeRootSignature(&rootSignatureDesc,
      D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &errBlob);
    m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureVAT));
  }
}

void SimpleVATApp::Prepare()
{
  SetTitle("SimpleVAT");
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

  PrepareVATData();

  PreparePipeline();
}

void SimpleVATApp::Cleanup()
{
  WaitForIdleGPU();

  m_model.Release();
}

void SimpleVATApp::OnMouseButtonDown(UINT msg)
{
  auto io = ImGui::GetIO();
  if (io.WantCaptureMouse)
  {
    return;
  }
  m_camera.OnMouseButtonDown((int)msg);
}
void SimpleVATApp::OnMouseButtonUp(UINT msg)
{
  m_camera.OnMouseButtonUp();
}

void SimpleVATApp::OnMouseMove(UINT msg, int dx, int dy)
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

void SimpleVATApp::PreparePipeline()
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
    ComPtr<ID3D12PipelineState> pipelineState;
    Shader shaderVS, shaderPS;
    std::vector<wstring> flags;
    std::vector<Shader::DefineMacro> defines;

    shaderVS.load(L"shaderVAT.hlsl", Shader::Vertex, L"mainVS", flags, defines);
    shaderPS.load(L"shaderVAT.hlsl", Shader::Pixel, L"mainPS", flags, defines);

    auto psoDesc = book_util::CreateDefaultPsoDesc(
      DXGI_FORMAT_R8G8B8A8_UNORM,
      rasterizerState,
      nullptr, 0,
      m_rootSignatureVAT,
      shaderVS.getCode(), shaderPS.getCode()
    );

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_VAT_DRAW] = pipelineState;
  }
}

void SimpleVATApp::PrepareVATData()
{
  m_vatFluid = LoadVAT("assets/vat/FluidSample");
  m_vatDestroy = LoadVAT("assets/vat/DestroyWall");

  // VAT データ描画用のマテリアルを設定.
  auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ShaderDrawMeshParameter));
  m_vatMaterialCB = CreateConstantBuffers(cbDesc);
}

SimpleVATApp::VATData SimpleVATApp::LoadVAT(std::string path)
{
  VATData data;
  data.texPosition = LoadTexture(path + ".ptex.dds");
  data.texNormal = LoadTexture(path + ".ntex.dds");

  const auto& desc = data.texPosition.res->GetDesc();
  data.vertexCount = int(desc.Width);
  data.animationCount = desc.Height;

  return data;
}


void SimpleVATApp::Render()
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

  UINT TotalAnimationFrames = 0, VertexCount = 0;

  if (m_mode == DrawMode_Fluid) {
    TotalAnimationFrames = m_vatFluid.animationCount;
    VertexCount = m_vatFluid.vertexCount;
    WriteToUploadHeapMemory(m_vatMaterialCB[m_frameIndex].Get(), sizeof(ShaderDrawMeshParameter), &m_vatFluidMaterial);
  }
  if (m_mode == DrawMode_Destroy) {
    TotalAnimationFrames = m_vatDestroy.animationCount;
    VertexCount = m_vatDestroy.vertexCount;
    WriteToUploadHeapMemory(m_vatMaterialCB[m_frameIndex].Get(), sizeof(ShaderDrawMeshParameter), &m_vatDestroyMaterial);
  }
 
 
  D3D12_CPU_DESCRIPTOR_HANDLE handleRtvs[] = { m_swapchain->GetCurrentRTV() };
  D3D12_CPU_DESCRIPTOR_HANDLE handleDsv = m_defaultDepthDSV;
  m_commandList->OMSetRenderTargets(_countof(handleRtvs), handleRtvs, FALSE, &handleDsv);

  m_commandList->SetGraphicsRootSignature(m_rootSignatureVAT.Get());
  m_commandList->SetPipelineState(m_pipelines[PSO_VAT_DRAW].Get());
  m_commandList->SetGraphicsRootConstantBufferView(RP_VAT_SCENE_CB, m_sceneParameterCB[m_frameIndex]->GetGPUVirtualAddress());
  m_commandList->SetGraphicsRootConstantBufferView(RP_VAT_MATERIAL, m_vatMaterialCB[m_frameIndex]->GetGPUVirtualAddress());
  if (m_mode == DrawMode_Fluid) {
    m_commandList->SetGraphicsRootDescriptorTable(RP_VAT_POSITON, m_vatFluid.texPosition.srv);
    m_commandList->SetGraphicsRootDescriptorTable(RP_VAT_NORMAL, m_vatFluid.texNormal.srv);
  }
  if (m_mode == DrawMode_Destroy) {
    m_commandList->SetGraphicsRootDescriptorTable(RP_VAT_POSITON, m_vatDestroy.texPosition.srv);
    m_commandList->SetGraphicsRootDescriptorTable(RP_VAT_NORMAL, m_vatDestroy.texNormal.srv);
  }
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commandList->DrawInstanced(VertexCount, 1, 0, 0);

  DrawModel();

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

  if (m_autoAnimation) {
    ++m_sceneParameters.animationFrame;
  }
  m_sceneParameters.animationFrame = m_sceneParameters.animationFrame % TotalAnimationFrames;
}

void SimpleVATApp::RenderHUD()
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

  ImGui::Checkbox("AutoAnimation", &m_autoAnimation);

  if (ImGui::Combo("VAT", (int*)&m_mode, "Fluid VAT\0Destroy\0\0")) {
    m_sceneParameters.animationFrame = 0;
  }

  ImGui::InputInt("AnimeFrame", (int*) & m_sceneParameters.animationFrame);

  ImGui::End();

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
}


void SimpleVATApp::DrawModel()
{
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commandList->SetPipelineState(m_pipelines[PSO_DEFAULT].Get());
  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

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



// Blender での流体のアニメーションは以下のサイトを参考に設定しました。
// http://qcganime.web.fc2.com/BLENDER28/FluidHowToM01.html
// https://horohorori.com/blender-note/physics-simulations/about-fluid-simulation-domain/
