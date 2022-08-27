#include "StreamOutputApp.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include <DirectXTex.h>
#include <fstream>
#include <stack>

#include <filesystem>

using namespace std;
using namespace DirectX;
namespace fs = std::filesystem;

StreamOutputApp::StreamOutputApp()
{
  m_camera.SetLookAt(
    XMFLOAT3(0.0f, 13.0f, 30.0f),
    XMFLOAT3(0.0f, 10.0f, 0.0f)
  );
}

void StreamOutputApp::CreateRootSignatures()
{
  CD3DX12_DESCRIPTOR_RANGE srvAlbedo;
  srvAlbedo.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

  CD3DX12_STATIC_SAMPLER_DESC samplerDesc;
  samplerDesc.Init(
    0,
    D3D12_FILTER_MIN_MAG_MIP_LINEAR,
    D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    D3D12_TEXTURE_ADDRESS_MODE_WRAP);

  // RootSignature
  array<CD3DX12_ROOT_PARAMETER, 6> rootParams;
  rootParams[0].InitAsConstantBufferView(0);
  rootParams[1].InitAsConstantBufferView(1);
  rootParams[2].InitAsDescriptorTable(1, &srvAlbedo);
  rootParams[3].InitAsUnorderedAccessView(0); // u0
  rootParams[4].InitAsUnorderedAccessView(1); // u1
  rootParams[5].InitAsShaderResourceView(1); // t1

  CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
  rootSignatureDesc.Init(
    UINT(rootParams.size()), rootParams.data(),
    1, &samplerDesc,
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

  ComPtr<ID3DBlob> signature, errBlob;
  D3D12SerializeRootSignature(&rootSignatureDesc,
    D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &errBlob);
  m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature));


  rootSignatureDesc.Flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;
  D3D12SerializeRootSignature(&rootSignatureDesc,
    D3D_ROOT_SIGNATURE_VERSION_1_0, &signature, &errBlob);
  m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureGS));
}

void StreamOutputApp::Prepare()
{
  SetTitle("VertexStreamOutput");
  CreateRootSignatures();

  m_commandList->Reset(m_commandAllocators[0].Get(), nullptr);
  ID3D12DescriptorHeap* heaps[] = { m_heap->GetHeap().Get() };
  m_commandList->SetDescriptorHeaps(1, heaps);
  m_commandList->Close();

  auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ShaderParameters));
  m_sceneParameterCB = CreateConstantBuffers(cbDesc);


  //m_skinActor = PrepareModelData("assets/model/alicia/Alicia_solid.pmx");
  m_skinActor = model::LoadModelData("assets/model/alicia/Alicia_solid.pmx", this, model::ModelLoadFlag::ModelLoadFlag_Flip_UV);
  // このサンプルで使用するシェーダーパラメーター集合で定数バッファを作る.
  for (auto& batch : m_skinActor.DrawBatches) {
    auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ShaderDrawMeshParameter));
    batch.materialParameterCB = CreateConstantBuffers(desc);
  }

  // 初期ポーズでは分かりづらいので少しポーズを付けておく.
  {
    {
      auto neck = m_skinActor.FindNode("首");
      neck->transform = XMMatrixRotationY(XMConvertToRadians(20.0f)) * neck->transform;
    }
    {
      auto elbow = m_skinActor.FindNode("左ひじ");
      elbow->transform = XMMatrixRotationY(-DirectX::XM_PIDIV2) * elbow->transform;
    }
    {
      auto elbow = m_skinActor.FindNode("左ひざ");
      elbow->transform = XMMatrixRotationX(DirectX::XM_PIDIV4) * elbow->transform;
    }
  }


  // Position 用 DESC を参照して UAV 出力用バッファの支度.
  auto uavResDesc = CD3DX12_RESOURCE_DESC(m_skinActor.Position->GetDesc());
  uavResDesc.Format = DXGI_FORMAT_UNKNOWN;
  uavResDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  auto uavRes = CreateResource(uavResDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, D3D12_HEAP_TYPE_DEFAULT);
  m_skinActor.extraBuffers["uavPos"] = uavRes;

  auto uavDescriptor = GetDescriptorManager()->Alloc();
  m_skinActor.extraHandles["uavPos"] = uavDescriptor;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
  uavDesc.Format = uavResDesc.Format;
  uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uavDesc.Buffer.StructureByteStride = sizeof(XMFLOAT3);
  uavDesc.Buffer.NumElements = UINT(uavResDesc.Width / uavDesc.Buffer.StructureByteStride);
  m_device->CreateUnorderedAccessView(uavRes.Get(), nullptr, &uavDesc, uavDescriptor);

  // Normal 出力用も同様に生成.
  uavRes = CreateResource(uavResDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, D3D12_HEAP_TYPE_DEFAULT);
  uavDescriptor = GetDescriptorManager()->Alloc();
  m_skinActor.extraBuffers["uavNormal"] = uavRes;
  m_skinActor.extraHandles["uavNormal"] = uavDescriptor;
  m_device->CreateUnorderedAccessView(uavRes.Get(), nullptr, &uavDesc, uavDescriptor);

  // StreamOut 用のバッファを作成.
  auto soResDesc = CD3DX12_RESOURCE_DESC::Buffer( (sizeof(XMFLOAT3)*2+sizeof(XMFLOAT2)) * m_skinActor.totalVertexCount);
  auto soRes = CreateResource(soResDesc, D3D12_RESOURCE_STATE_STREAM_OUT, nullptr, D3D12_HEAP_TYPE_DEFAULT);
  m_skinActor.extraBuffers["soBuffer"] = soRes;
  soRes->SetName(L"soBuffer");

  soResDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMUINT4));
  soRes = CreateResource(soResDesc, D3D12_RESOURCE_STATE_STREAM_OUT, nullptr, D3D12_HEAP_TYPE_DEFAULT);
  m_skinActor.extraBuffers["soBufferCount"] = soRes;
  soRes->SetName(L"soBufferCount");

  PreparePipeline();
}

void StreamOutputApp::Cleanup()
{
  WaitForIdleGPU();

  m_skinActor.Release();
}

void StreamOutputApp::OnMouseButtonDown(UINT msg)
{
  auto io = ImGui::GetIO();
  if (io.WantCaptureMouse)
  {
    return;
  }
  m_camera.OnMouseButtonDown((int)msg);
}
void StreamOutputApp::OnMouseButtonUp(UINT msg)
{
  m_camera.OnMouseButtonUp();
}

void StreamOutputApp::OnMouseMove(UINT msg, int dx, int dy)
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

void StreamOutputApp::PreparePipeline()
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

    { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  3, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "BLENDWEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 4, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };

  HRESULT hr;
  // GS による出力.
  {
    ComPtr<ID3D12PipelineState> pipelineState;
    Shader shaderVS, shaderGS;
    std::vector<wstring> flags;
    std::vector<Shader::DefineMacro> defines;

    shaderVS.load(L"shader.hlsl", Shader::Vertex, L"mainVS", flags, defines);

    auto psoDesc = book_util::CreateDefaultPsoDesc(
      DXGI_FORMAT_R8G8B8A8_UNORM,
      rasterizerState,
      inputElementDesc.data(), UINT(inputElementDesc.size()),
      m_rootSignatureGS,
      shaderVS.getCode(), nullptr
    );

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;

    D3D12_SO_DECLARATION_ENTRY soEntries[3] = { 0 };
    soEntries[0].Stream = 0;
    soEntries[0].SemanticName = "POSITION";
    soEntries[0].ComponentCount = 3;
    soEntries[0].OutputSlot = 0;

    soEntries[1].Stream = 0;
    soEntries[1].SemanticName = "NORMAL";
    soEntries[1].ComponentCount = 3;
    soEntries[2].OutputSlot = 0;

    soEntries[2].Stream = 0;
    soEntries[2].SemanticName = "TEXCOORD";
    soEntries[2].ComponentCount = 2;
    soEntries[2].OutputSlot = 0;

    psoDesc.StreamOutput.NumEntries = 3;
    psoDesc.StreamOutput.pSODeclaration = soEntries;

    UINT strides[] = {
      UINT(sizeof(XMFLOAT3)*2 + sizeof(XMFLOAT2)),  // 頂点POS/Normal/UV
    };
    psoDesc.StreamOutput.NumStrides = 1;
    psoDesc.StreamOutput.pBufferStrides = strides;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_GS_OUT] = pipelineState;
  }

  // VS からストリームアウト出力.
  {
    ComPtr<ID3D12PipelineState> pipelineState;
    Shader shaderVS, shaderGS;
    std::vector<wstring> flags;
    std::vector<Shader::DefineMacro> defines;

    shaderVS.load(L"shaderSOFromVS.hlsl", Shader::Vertex, L"mainVS", flags, defines);

    auto psoDesc = book_util::CreateDefaultPsoDesc(
      DXGI_FORMAT_R8G8B8A8_UNORM,
      rasterizerState,
      inputElementDesc.data(), UINT(inputElementDesc.size()),
      m_rootSignatureGS,
      shaderVS.getCode(), nullptr
    );

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;

    D3D12_SO_DECLARATION_ENTRY soEntries[3] = { 0 };
    soEntries[0].Stream = 0;
    soEntries[0].SemanticName = "POSITION";
    soEntries[0].ComponentCount = 3;
    soEntries[0].OutputSlot = 0;

    soEntries[1].Stream = 0;
    soEntries[1].SemanticName = "NORMAL";
    soEntries[1].ComponentCount = 3;
    soEntries[2].OutputSlot = 0;

    soEntries[2].Stream = 0;
    soEntries[2].SemanticName = "TEXCOORD";
    soEntries[2].ComponentCount = 2;
    soEntries[2].OutputSlot = 0;

    psoDesc.StreamOutput.NumEntries = 3;
    psoDesc.StreamOutput.pSODeclaration = soEntries;

    UINT strides[] = {
      UINT(sizeof(XMFLOAT3) * 2 + sizeof(XMFLOAT2)),  // 頂点POS/Normal/UV
    };
    psoDesc.StreamOutput.NumStrides = 1;
    psoDesc.StreamOutput.pBufferStrides = strides;

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_VS_OUT] = pipelineState;
}

  // ストリーム出力後の描画用.
  inputElementDesc = {
    { "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
  };
  {
    ComPtr<ID3D12PipelineState> pipelineState;
    Shader shaderVS, shaderPS;
    std::vector<wstring> flags;
    std::vector<Shader::DefineMacro> defines;

    shaderVS.load(L"shaderDrawSOBuf.hlsl", Shader::Vertex, L"mainVS", flags, defines);
    shaderPS.load(L"shaderDrawSOBuf.hlsl", Shader::Pixel, L"mainPS", flags, defines);

    auto psoDesc = book_util::CreateDefaultPsoDesc(
      DXGI_FORMAT_R8G8B8A8_UNORM,
      rasterizerState,
      inputElementDesc.data(), UINT(inputElementDesc.size()),
      m_rootSignature,
      shaderVS.getCode(), shaderPS.getCode()
    );

    hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState));
    ThrowIfFailed(hr, "CreateGraphicsPipelineState Failed.");
    m_pipelines[PSO_SO_DRAW] = pipelineState;
  }

}


void StreamOutputApp::Render()
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


  m_scenePatameters.lightDir = XMFLOAT4(0.0f, 20.0f, 20.0f, 0.0f);
  auto mtxProj = XMMatrixPerspectiveFovRH(XMConvertToRadians(45.0f), float(m_width) / float(m_height), 0.1f, 100.0f);
  XMStoreFloat4x4(&m_scenePatameters.view, XMMatrixTranspose(m_camera.GetViewMatrix()));
  XMStoreFloat4x4(&m_scenePatameters.proj, XMMatrixTranspose(mtxProj));

  // ボーン マトリックスパレットの準備.
  m_skinActor.rootNode->UpdateMatrices(XMMatrixRotationY(-DirectX::XM_PIDIV4));
  for (auto& batch : m_skinActor.DrawBatches) {
    std::vector<XMMATRIX> matrices;
    for (auto bone : batch.boneList2) {
      auto mtx = bone->offsetMatrix * bone->worldTransform * m_skinActor.invGlobalTransform;
      matrices.push_back(XMMatrixTranspose(mtx));
    }


    auto& bonesCB = batch.boneMatrixPalette[m_frameIndex];
    ShaderDrawMeshParameter batchParams{};
    batchParams.offset.x = batch.vertexOffsetCount;
    int memorySize = int(sizeof(XMMATRIX) * matrices.size());
    memcpy(batchParams.bones, matrices.data(), memorySize);
    memorySize += sizeof(XMUINT4);

    WriteToUploadHeapMemory(bonesCB.Get(), memorySize, &batchParams);
  }
  auto imageIndex = m_swapchain->GetCurrentBackBufferIndex();
  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  WriteToUploadHeapMemory(m_sceneParameterCB[imageIndex].Get(), sizeof(ShaderParameters), &m_scenePatameters);

  auto rtv = m_swapchain->GetCurrentRTV();
  auto dsv = m_defaultDepthDSV;

  // カラーバッファ(レンダーターゲットビュー)のクリア
  float m_clearColor[4] = { 0.1f,0.5f,0.95f,0 };
  m_commandList->ClearRenderTargetView(rtv, m_clearColor, 0, nullptr);

  // デプスバッファ(デプスステンシルビュー)のクリア
  m_commandList->ClearDepthStencilView(
    dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

  // 描画先をセット
  D3D12_CPU_DESCRIPTOR_HANDLE handleRtvs[] = { rtv };
  D3D12_CPU_DESCRIPTOR_HANDLE handleDsv = dsv;
  m_commandList->OMSetRenderTargets(1, handleRtvs, FALSE, &handleDsv);

  // ビューポートとシザーのセット
  auto viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, float(m_width), float(m_height));
  auto scissorRect = CD3DX12_RECT(0, 0, LONG(m_width), LONG(m_height));
  m_commandList->RSSetViewports(1, &viewport);
  m_commandList->RSSetScissorRects(1, &scissorRect);

  m_commandList->SetGraphicsRootConstantBufferView(0, m_sceneParameterCB[imageIndex]->GetGPUVirtualAddress());

  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


  // 頂点データ出力パイプラインの設定.
  if (m_mode == DrawMode_GS) {
    m_commandList->SetGraphicsRootSignature(m_rootSignatureGS.Get());
    m_commandList->SetPipelineState(m_pipelines[PSO_GS_OUT].Get());
  }
  if (m_mode == DrawMode_VS) {
    m_commandList->SetGraphicsRootSignature(m_rootSignatureGS.Get());
    m_commandList->SetPipelineState(m_pipelines[PSO_VS_OUT].Get());
  }

  // ストリームアウト出力設定.
  {
    auto soBuffer = m_skinActor.extraBuffers["soBuffer"];
    auto soBufferCount = m_skinActor.extraBuffers["soBufferCount"];
    D3D12_STREAM_OUTPUT_BUFFER_VIEW soView{};
    soView.SizeInBytes = soBuffer->GetDesc().Width;
    soView.BufferLocation = soBuffer->GetGPUVirtualAddress();
    soView.BufferFilledSizeLocation = soBufferCount->GetGPUVirtualAddress();

    m_commandList->SOSetTargets(0, 1, &soView);
  }

  int count = 0;
  for(auto& batch : m_skinActor.DrawBatches) {
    std::vector<D3D12_VERTEX_BUFFER_VIEW> vbViews = {
      m_skinActor.vertexBufferViews[model::ModelAsset::VBV_Position],
      m_skinActor.vertexBufferViews[model::ModelAsset::VBV_Normal],
      m_skinActor.vertexBufferViews[model::ModelAsset::VBV_UV0],
    };
    m_commandList->IASetVertexBuffers(0, UINT(vbViews.size()), vbViews.data());

    if (m_skinActor.BoneIndices) {
      vbViews = {
        m_skinActor.vertexBufferViews[model::ModelAsset::VBV_BlendIndices],
        m_skinActor.vertexBufferViews[model::ModelAsset::VBV_BlendWeights],
      };
      m_commandList->IASetVertexBuffers(3, UINT(vbViews.size()), vbViews.data());
    }
    auto& bonesCB = batch.boneMatrixPalette[m_frameIndex];
    m_commandList->IASetIndexBuffer(&m_skinActor.indexBufferView);
    m_commandList->SetGraphicsRootConstantBufferView(1, bonesCB->GetGPUVirtualAddress());

    const auto& material = m_skinActor.materials[batch.materialIndex];
    m_commandList->SetGraphicsRootDescriptorTable(2, material.albedoSRV);

    m_commandList->DrawIndexedInstanced(batch.indexCount, 1, batch.indexOffsetCount, batch.vertexOffsetCount, 0);
  }

  // StreamOut バッファを 頂点入力として使えるようステート変更.
  auto soBuffer = m_skinActor.extraBuffers["soBuffer"];
  std::vector<D3D12_RESOURCE_BARRIER> beginBarriers = {
    CD3DX12_RESOURCE_BARRIER::Transition(soBuffer.Get(),
      D3D12_RESOURCE_STATE_STREAM_OUT,
      D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
      ),
  };

  // Stream Out の設定を解除.
  D3D12_STREAM_OUTPUT_BUFFER_VIEW soView{};
  m_commandList->SOSetTargets(0, 1, &soView);

  // ストリームアウト出力された頂点データで描画を行う.
  {
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    m_commandList->SetPipelineState(m_pipelines[PSO_SO_DRAW].Get());
    m_commandList->ResourceBarrier(UINT(beginBarriers.size()), beginBarriers.data());

    D3D12_VERTEX_BUFFER_VIEW vbView;
    auto desc = soBuffer->GetDesc();
    vbView.BufferLocation = soBuffer->GetGPUVirtualAddress();
    vbView.SizeInBytes = UINT(desc.Width);
    vbView.StrideInBytes = sizeof(XMFLOAT3) + sizeof(XMFLOAT3) + sizeof(XMFLOAT2);
    m_commandList->IASetVertexBuffers(0, 1, &vbView);

    m_commandList->SetGraphicsRootConstantBufferView(0, m_sceneParameterCB[imageIndex]->GetGPUVirtualAddress());

    for (auto& batch : m_skinActor.DrawBatches) {
      const auto& material = m_skinActor.materials[batch.materialIndex];
      m_commandList->SetGraphicsRootDescriptorTable(2, material.albedoSRV);

      auto& bonesCB = batch.boneMatrixPalette[m_frameIndex];
      m_commandList->SetGraphicsRootConstantBufferView(1, bonesCB->GetGPUVirtualAddress());

      m_commandList->DrawInstanced(batch.indexCount, 1, batch.indexOffsetCount, 0);
    }

    std::vector<D3D12_RESOURCE_BARRIER> endBarriers = {
      CD3DX12_RESOURCE_BARRIER::Transition(soBuffer.Get(),
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        D3D12_RESOURCE_STATE_STREAM_OUT
        )
    };
    m_commandList->ResourceBarrier(UINT(endBarriers.size()), endBarriers.data());
  }
  RenderHUD();

  // レンダーターゲットからスワップチェイン表示可能へ
  {
    auto barrierToPresent = m_swapchain->GetBarrierToPresent();
    CD3DX12_RESOURCE_BARRIER barriers[] = {
      barrierToPresent,
    };

    m_commandList->ResourceBarrier(_countof(barriers), barriers);
  }
  m_commandList->Close();
  ID3D12CommandList* lists[] = { m_commandList.Get() };
  m_commandQueue->ExecuteCommandLists(1, lists);

  m_swapchain->Present(1, 0);
  m_swapchain->WaitPreviousFrame(m_commandQueue, m_frameIndex, GpuWaitTimeout);
}

void StreamOutputApp::RenderHUD()
{
  // ImGui
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  // ImGui ウィジェットを描画する.
  auto framerate = ImGui::GetIO().Framerate;
  ImGui::Begin("Information");
  ImGui::Text("Framerate %.3f ms", 1000.0f / framerate);
  ImGui::Combo("Mode", (int*)&m_mode, "Mode GSOut\0Mode VSout\0\0");
  ImGui::End();

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList.Get());
}
