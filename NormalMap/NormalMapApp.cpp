#include "NormalMapApp.h"

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_win32.h"

#include <DirectXTex.h>
#include <fstream>
#include <stack>
#include <sstream>
#include <random>

#if 0
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>
#endif
using namespace std;
using namespace DirectX;
namespace fs = std::filesystem;

#if 0
std::string ConvertFromUTF8(const char* utf8Str)
{
  DWORD dwRet = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
  std::vector<wchar_t> wstrbuf;
  wstrbuf.resize(dwRet);

  MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wstrbuf.data(), int(wstrbuf.size()));

  dwRet = WideCharToMultiByte(932, 0, wstrbuf.data(), -1, NULL, 0, NULL, NULL);
  std::vector<char> strbuf;
  strbuf.resize(dwRet);

  WideCharToMultiByte(932, 0, wstrbuf.data(), -1, strbuf.data(), int(strbuf.size()), NULL, NULL);
  return std::string(strbuf.data());
}

void AddVertexIndex(XMINT4& v, int index) {
  if (v.x == -1) {
    v.x = index;
    return;
  }
  if (v.y == -1) {
    v.y = index;
    return;
  }
  if (v.z == -1) {
    v.z = index;
    return;
  }
  if (v.w == -1) {
    v.w = index;
    return;
  }
}
void AddVertexWeight(XMFLOAT4& v, float weight) {
  if (v.x < 0) {
    v.x = weight;
    return;
  }
  if (v.y < 0) {
    v.y = weight;
    return;
  }
  if (v.z < 0) {
    v.z = weight;
    return;
  }
  if (v.w < 0) {
    v.w = weight;
    return;
  }
}

DirectX::XMMATRIX ConvertMatrix(const aiMatrix4x4& mtx) {
  DirectX::XMFLOAT4X4 m(mtx[0]);
  return XMMatrixTranspose(XMLoadFloat4x4(&m));
}
#endif

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

  //m_model = PrepareModelData("assets/model/plane.obj");
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
#if 0
NormalMapApp::ModelAsset NormalMapApp::PrepareModelData(std::string fileName)
{
  ModelAsset model;
  model.importer = new Assimp::Importer();
  uint32_t flags = 0;
  flags |= aiProcess_Triangulate | /*aiProcess_FlipUVs*/0 | aiProcess_CalcTangentSpace;
  model.scene = model.importer->ReadFile(fileName, flags);

  auto scene = model.scene;
  UINT totalVertexCount = 0, totalIndexCount = 0;
  bool hasBone = false;

  std::stack<std::shared_ptr<Node>> nodes;
  model.rootNode = std::make_shared<Node>();
  nodes.push(model.rootNode);

  std::stack<aiNode*> nodeStack;
  nodeStack.push(scene->mRootNode);
  while (!nodeStack.empty()) {
    auto* node = nodeStack.top();
    nodeStack.pop();

    auto nodeTarget = nodes.top(); nodes.pop();

    for (uint32_t i = 0; i < node->mNumChildren; ++i) {
      nodeStack.push(node->mChildren[i]);
    }
    // 操作用の子ノードを生成.
    for (uint32_t i = 0; i < node->mNumChildren; ++i) {
      auto child = std::make_shared<Node>();
      nodeTarget->children.push_back(child);
      nodes.push(child);
    }

    auto name = ConvertFromUTF8(node->mName.C_Str());
    auto meshCount = node->mNumMeshes;
    nodeTarget->name = name;

    if (meshCount > 0) {
      for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
        auto meshIndex = node->mMeshes[i];
        const auto* mesh = scene->mMeshes[meshIndex];
        totalVertexCount += mesh->mNumVertices;
        totalIndexCount += mesh->mNumFaces * 3;
        hasBone |= mesh->HasBones();
      }
    }
    nodeTarget->transform = ConvertMatrix(node->mTransformation);
  }

  auto vbDescPN = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMFLOAT3) * totalVertexCount);
  model.position = CreateResource(vbDescPN, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);
  model.normal = CreateResource(vbDescPN, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);
  model.tangent  = CreateResource(vbDescPN, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);
  model.binormal = CreateResource(vbDescPN, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);

  auto vbDescT = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMFLOAT2) * totalVertexCount);
  model.uv0 = CreateResource(vbDescPN, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);

  if (hasBone) {
    auto vbDescB = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMINT4) * totalVertexCount);
    model.boneIndices = CreateResource(vbDescB, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);

    auto vbDescW = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMFLOAT4) * totalVertexCount);
    model.boneWeights = CreateResource(vbDescW, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);
  }

  auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * totalIndexCount);
  model.indices = CreateResource(ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);

  std::vector<XMFLOAT3> vbPos, vbNrm;
  std::vector<XMFLOAT2> vbUV0;
  std::vector<XMINT4>  vbBIndices;
  std::vector<XMFLOAT4> vbBWeights;
  std::vector<XMFLOAT3> vbTangents;
  std::vector<XMFLOAT3> vbBinormals;;
  std::vector<UINT> ibIndices;
  vbPos.reserve(totalVertexCount);
  vbNrm.reserve(totalVertexCount);
  vbUV0.reserve(totalVertexCount);
  vbBIndices.resize(totalVertexCount, XMINT4(-1, -1, -1, -1));
  vbBWeights.resize(totalVertexCount, XMFLOAT4(-1.0f, -1.0f, -1.0f, -1.0f));
  ibIndices.reserve(totalIndexCount);
  vbTangents.reserve(totalVertexCount);
  vbBinormals.reserve(totalVertexCount);

  totalVertexCount = 0;
  totalIndexCount = 0;
  nodeStack.push(scene->mRootNode);
  while (!nodeStack.empty()) {
    auto* node = nodeStack.top();
    nodeStack.pop();

    for (uint32_t i = 0; i < node->mNumChildren; ++i) {
      nodeStack.push(node->mChildren[i]);
    }

    auto name = ConvertFromUTF8(node->mName.C_Str());
    auto meshCount = node->mNumMeshes;
    char buf[256];

    if (meshCount > 0) {
      for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
        auto meshIndex = node->mMeshes[i];
        const auto* mesh = scene->mMeshes[meshIndex];

        DrawBatch batch{};
        batch.vertexOffsetCount = totalVertexCount;
        batch.indexOffsetCount = totalIndexCount;
        batch.indexCount = mesh->mNumFaces * 3;
        batch.materialIndex = mesh->mMaterialIndex;

        const auto* vPosStart = reinterpret_cast<const XMFLOAT3*>(mesh->mVertices);
        vbPos.insert(vbPos.end(), vPosStart, vPosStart + mesh->mNumVertices);

        const auto* vNrmStart = reinterpret_cast<const XMFLOAT3*>(mesh->mNormals);
        vbNrm.insert(vbNrm.end(), vNrmStart, vNrmStart + mesh->mNumVertices);

        std::vector<XMFLOAT2> uvWork(mesh->mNumVertices);
        for (int j = 0; j < mesh->mNumVertices; ++j) {
          const auto& src = mesh->mTextureCoords[0][j];
          uvWork[j].x = src.x;
          uvWork[j].y = src.y;
        }

        vbUV0.insert(vbUV0.end(), uvWork.begin(), uvWork.end());

        if (mesh->HasTangentsAndBitangents()) {
          const auto* vTangentStart = reinterpret_cast<const XMFLOAT3*>(mesh->mTangents);
          vbTangents.insert(vbTangents.end(), vTangentStart, vTangentStart + mesh->mNumVertices);
          const auto* vBinormalStart = reinterpret_cast<const XMFLOAT3*>(mesh->mBitangents);
          vbBinormals.insert(vbBinormals.end(), vBinormalStart, vBinormalStart + mesh->mNumVertices);
        }

        for (int f = 0; f < int(mesh->mNumFaces); ++f) {
          for (int fi = 0; fi < int(mesh->mFaces[f].mNumIndices); ++fi) {
            auto vertexIndex = mesh->mFaces[f].mIndices[fi];
            ibIndices.push_back(vertexIndex);
          }
        }

        if (hasBone) {
          if (mesh->HasBones()) {
            // 有効なボーン情報を持つものを抽出.
            std::vector<string> boneNameList;
            std::vector<aiBone*> activeBones;
            std::vector<int> boneIndexList;
            for (uint32_t j = 0; j < mesh->mNumBones; ++j) {
              const auto bone = mesh->mBones[j];
              auto name = ConvertFromUTF8(bone->mName.C_Str());
              if (bone->mNumWeights > 0) {
                boneIndexList.push_back(int(boneNameList.size()));
                boneNameList.push_back(name);
                activeBones.push_back(bone);
              }
            }

            auto vertexBase = batch.vertexOffsetCount;
            for (int boneIndex = 0; boneIndex < int(activeBones.size()); ++boneIndex) {
              auto bone = activeBones[boneIndex];
              for (int j = 0; j < int(bone->mNumWeights); ++j) {
                auto weightInfo = bone->mWeights[j];
                auto vertexIndex = vertexBase + weightInfo.mVertexId;
                auto weight = weightInfo.mWeight;

                AddVertexIndex(vbBIndices[vertexIndex], boneIndex);
                AddVertexWeight(vbBWeights[vertexIndex], weight);
              }
            }

            batch.boneList = activeBones;

            for (int boneIndex = 0; boneIndex < int(activeBones.size()); ++boneIndex) {
              auto bone = activeBones[boneIndex];
              auto name = ConvertFromUTF8(bone->mName.C_Str());

              auto node = model.findNode(name);
              assert(node != nullptr);
              node->offsetMatrix = ConvertMatrix(bone->mOffsetMatrix);
              batch.boneList2.push_back(node);
            }

            auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMMATRIX) * activeBones.size());
            batch.boneMatrixPalette = CreateConstantBuffers(desc);
          }
        }

        // マテリアル情報
        {
          auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(ShaderDrawMeshParameter));
          batch.materialParameterCB = CreateConstantBuffers(desc);
        }

        totalVertexCount += mesh->mNumVertices;
        totalIndexCount += mesh->mNumFaces * 3;

        model.drawBatches.emplace_back(batch);
        hasBone |= mesh->HasBones();
      }
    }
  }

  for (int i = 0; i<int(model.scene->mNumMaterials); ++i) {
    Material m{};
    auto material = model.scene->mMaterials[i];

    fs::path baseDir(fileName);
    baseDir = baseDir.parent_path();
    aiString path;
    auto ret = material->GetTexture(aiTextureType_DIFFUSE, 0, &path);
    if (ret == aiReturn_SUCCESS) {
      auto texfileName = ConvertFromUTF8(path.C_Str());
      auto textureFilePath = (baseDir / texfileName).string();

      auto tex = LoadTexture(textureFilePath);
      m.albedoSRV = tex.srv;
    } else {
      auto tex = LoadTexture("white.png");
      m.albedoSRV = tex.srv;
    }
    
    ret = material->GetTexture(aiTextureType_SPECULAR, 0, &path);
    if (ret == aiReturn_SUCCESS) {
      auto texfileName = ConvertFromUTF8(path.C_Str());
      auto textureFilePath = (baseDir / texfileName).string();
      
      auto tex = LoadTexture(textureFilePath);
      m.specularSRV = tex.srv;
    } else {
      auto tex = LoadTexture("black.png");
      m.specularSRV = tex.srv;
    }
    
    float shininess = 0;
    ret = material->Get(AI_MATKEY_SHININESS, shininess);
    m.shininess = shininess;

    aiColor3D diffuse{};
    ret = material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
    m.diffuse = XMFLOAT3(diffuse.r, diffuse.g, diffuse.b);

    aiColor3D ambient{};
    ret = material->Get(AI_MATKEY_COLOR_AMBIENT, ambient);
    m.ambient = XMFLOAT3(ambient.r, ambient.g, ambient.b);

    model.materials.push_back(m);
  }
  model.totalVertexCount = totalVertexCount;
  model.totalIndexCount = totalIndexCount;

  // ボーン情報未設定領域を掃除.
  if (hasBone) {
    for (auto& v : vbBIndices) {
      if (v.x < 0) { v.x = 0; }
      if (v.y < 0) { v.y = 0; }
      if (v.z < 0) { v.z = 0; }
      if (v.w < 0) { v.w = 0; }
    }
    for (auto& v : vbBWeights) {
      if (v.x < 0.0f) { v.x = 0.0f; }
      if (v.y < 0.0f) { v.y = 0.0f; }
      if (v.z < 0.0f) { v.z = 0.0f; }
      if (v.w < 0.0f) { v.w = 0.0f; }

      float total = v.x + v.y + v.z + v.w;
      assert(std::abs(total) > 0.999f && std::abs(total) < 1.01f);
    }
  }

  WriteToUploadHeapMemory(model.position.Get(), uint32_t(sizeof(XMFLOAT3) * vbPos.size()), vbPos.data());
  WriteToUploadHeapMemory(model.normal.Get(), uint32_t(sizeof(XMFLOAT3) * vbNrm.size()), vbNrm.data());
  WriteToUploadHeapMemory(model.uv0.Get(), uint32_t(sizeof(XMFLOAT2) * vbUV0.size()), vbUV0.data());
  WriteToUploadHeapMemory(model.tangent.Get(), uint32_t(sizeof(XMFLOAT3)* vbTangents.size()), vbTangents.data());
  WriteToUploadHeapMemory(model.binormal.Get(), uint32_t(sizeof(XMFLOAT3)* vbBinormals.size()), vbBinormals.data());

  model.vbvPosition = { model.position->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT3) * vbPos.size()), uint32_t(sizeof(XMFLOAT3)) };
  model.vbvNormal = { model.normal->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT3) * vbNrm.size()), uint32_t(sizeof(XMFLOAT3)) };
  model.vbvUV0 = { model.uv0->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT2) * vbUV0.size()), uint32_t(sizeof(XMFLOAT2)) };
  
  model.vbvTangent = { model.tangent->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT3) * vbTangents.size()), uint32_t(sizeof(XMFLOAT3)) };
  model.vbvBinormal = { model.binormal->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT3) * vbBinormals.size()), uint32_t(sizeof(XMFLOAT3)) };

  if (hasBone) {
    WriteToUploadHeapMemory(model.boneIndices.Get(), uint32_t(sizeof(XMINT4) * vbBIndices.size()), vbBIndices.data());
    WriteToUploadHeapMemory(model.boneWeights.Get(), uint32_t(sizeof(XMFLOAT4) * vbBWeights.size()), vbBWeights.data());
    model.vbvBlendIndices = { model.boneIndices->GetGPUVirtualAddress(), uint32_t(sizeof(XMINT4) * vbBIndices.size()), sizeof(XMINT4) };
    model.vbvBlendWeights = { model.boneWeights->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT4) * vbBWeights.size()), sizeof(XMFLOAT4)};
  }
  WriteToUploadHeapMemory(model.indices.Get(), uint32_t(sizeof(UINT) * ibIndices.size()), ibIndices.data());
  model.ibvIndices = { model.indices->GetGPUVirtualAddress(), uint32_t(sizeof(UINT) * ibIndices.size()), DXGI_FORMAT_R32_UINT };

  auto mtx = ConvertMatrix(scene->mRootNode->mTransformation);
  model.invGlobalTransform = XMMatrixInverse(nullptr, mtx);

  return model;
}
#endif

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
  ImGui::Text("Framerate %.3f ms", 1000.0f / framerate);

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

#if 0
void NormalMapApp::Node::UpdateMatrices(DirectX::XMMATRIX mtxParent)
{
  worldTransform = transform * mtxParent;

  for (auto& c : children) {
    c->UpdateMatrices(worldTransform);
  }
}

std::shared_ptr<NormalMapApp::Node> NormalMapApp::ModelAsset::findNode(const std::string& name)
{
  std::stack<std::shared_ptr<Node>> nodes;
  nodes.push(rootNode);
  while (!nodes.empty()) {
    auto node = nodes.top();
    nodes.pop();

    if (node->name == name) {
      return node;
    }

    for (auto& child : node->children) {
      nodes.push(child);
    }
  }
  return nullptr;
}
#endif

void NormalMapApp::DrawModelWithNormalMap()
{
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commandList->SetPipelineState(m_pipelines[PSO_DEFAULT].Get());

  // 描画先をセット
  D3D12_CPU_DESCRIPTOR_HANDLE handleRtvs[] = { m_swapchain->GetCurrentRTV() };
  D3D12_CPU_DESCRIPTOR_HANDLE handleDsv = m_defaultDepthDSV;
  m_commandList->OMSetRenderTargets(_countof(handleRtvs), handleRtvs, FALSE, &handleDsv);

  for (auto& batch : m_model.DrawBatches) {
#if 0
    std::vector<D3D12_VERTEX_BUFFER_VIEW> vbViews = {
      m_model.vbvPosition, m_model.vbvNormal, m_model.vbvUV0
    };
    m_commandList->IASetVertexBuffers(0, UINT(vbViews.size()), vbViews.data());

    if (m_model.boneIndices) {
      vbViews = {
        m_model.vbvBlendIndices, m_model.vbvBlendWeights
      };
      m_commandList->IASetVertexBuffers(3, UINT(vbViews.size()), vbViews.data());
    }
    if (m_model.tangent) {
      vbViews = {
        m_model.vbvTangent, m_model.vbvBinormal
      };
      m_commandList->IASetVertexBuffers(3, UINT(vbViews.size()), vbViews.data());
    }
#endif
    std::vector<D3D12_VERTEX_BUFFER_VIEW> vbViews = {
      m_model.vertexBufferViews[model::ModelAsset::VBV_Position],
      m_model.vertexBufferViews[model::ModelAsset::VBV_Normal],
      m_model.vertexBufferViews[model::ModelAsset::VBV_UV0],
    };
    m_commandList->IASetVertexBuffers(0, UINT(vbViews.size()), vbViews.data());
    if (m_model.Tangent) {
      m_commandList->IASetVertexBuffers(3, 1, &m_model.vertexBufferViews[model::ModelAsset::VBV_Tangent]);
    }

    //auto ibView = m_model.ibvIndices;
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

