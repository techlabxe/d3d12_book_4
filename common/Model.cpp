#include "Model.h"

#include <DirectXTex.h>
#include <fstream>
#include <stack>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace DirectX;
namespace fs = std::filesystem;

namespace {
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


}

namespace model {
  ModelAsset LoadModelData(std::filesystem::path filePath, D3D12AppBase* appBase, ModelLoadFlag loadFlags) {
    auto fileName = filePath.string();
    ModelAsset model;
    model.importer = new Assimp::Importer();
    uint32_t flags = 0;
    flags |= aiProcess_Triangulate;
    if (loadFlags & ModelLoadFlag_CalcTangent) {
      flags |= aiProcess_CalcTangentSpace;
    }
    if (loadFlags & ModelLoadFlag_Flip_UV) {
      flags |= aiProcess_FlipUVs;
    }
    model.scene = model.importer->ReadFile(fileName, flags);

    auto scene = model.scene;
    UINT totalVertexCount = 0, totalIndexCount = 0;
    bool hasBone = false, hasTangent = false;

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
          hasTangent |= mesh->HasTangentsAndBitangents();
        }
      }
      nodeTarget->transform = ConvertMatrix(node->mTransformation);
    }

    auto vbDescPN = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMFLOAT3) * totalVertexCount);
    model.Position = appBase->CreateResource(vbDescPN, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);
    model.Normal = appBase->CreateResource(vbDescPN, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);

    auto vbDescT = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMFLOAT2) * totalVertexCount);
    model.UV0 = appBase->CreateResource(vbDescPN, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);

    if (hasTangent) {
      model.Tangent = appBase->CreateResource(vbDescPN, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);
    }
    if (hasBone) {
      auto vbDescB = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMINT4) * totalVertexCount);
      model.BoneIndices = appBase->CreateResource(vbDescB, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);

      auto vbDescW = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMFLOAT4) * totalVertexCount);
      model.BoneWeights = appBase->CreateResource(vbDescW, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);
    }

    auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(UINT) * totalIndexCount);
    model.Indices = appBase->CreateResource(ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, D3D12_HEAP_TYPE_UPLOAD);

    std::vector<XMFLOAT3> vbPos, vbNrm;
    std::vector<XMFLOAT2> vbUV0;
    std::vector<XMINT4>  vbBIndices;
    std::vector<XMFLOAT4> vbBWeights;
    std::vector<XMFLOAT3> vbTangents;
    std::vector<UINT> ibIndices;
    vbPos.reserve(totalVertexCount);
    vbNrm.reserve(totalVertexCount);
    vbUV0.reserve(totalVertexCount);
    vbBIndices.resize(totalVertexCount, XMINT4(-1, -1, -1, -1));
    vbBWeights.resize(totalVertexCount, XMFLOAT4(-1.0f, -1.0f, -1.0f, -1.0f));
    vbTangents.reserve(totalVertexCount);
    ibIndices.reserve(totalIndexCount);

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

          if (hasTangent) {
            const auto* vTangentStart = reinterpret_cast<const XMFLOAT3*>(mesh->mTangents);
            vbTangents.insert(vbTangents.end(), vTangentStart, vTangentStart + mesh->mNumVertices);
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
              std::vector<std::string> boneNameList;
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

                auto node = model.FindNode(name);
                assert(node != nullptr);
                node->offsetMatrix = ConvertMatrix(bone->mOffsetMatrix);
                batch.boneList2.push_back(node);
              }

              auto desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMMATRIX) * activeBones.size());
              batch.boneMatrixPalette = appBase->CreateConstantBuffers(desc);
            }
          }
          totalVertexCount += mesh->mNumVertices;
          totalIndexCount += mesh->mNumFaces * 3;

          model.DrawBatches.emplace_back(batch);
          if (hasBone && batch.boneMatrixPalette.empty()) {
            DebugBreak();
          }
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

        auto tex = appBase->LoadTexture(textureFilePath);
        m.albedoSRV = tex.srv;
      } else {
        auto tex = appBase->LoadTexture("assets/texture/white.png");
        m.albedoSRV = tex.srv;
      }

      ret = material->GetTexture(aiTextureType_SPECULAR, 0, &path);
      if (ret == aiReturn_SUCCESS) {
        auto texfileName = ConvertFromUTF8(path.C_Str());
        auto textureFilePath = (baseDir / texfileName).string();

        auto tex = appBase->LoadTexture(textureFilePath);
        m.specularSRV = tex.srv;
      } else {
        auto tex = appBase->LoadTexture("assets/texture/black.png");
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

    if (hasBone) {
      // ボーン情報未設定領域を掃除.
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

    appBase->WriteToUploadHeapMemory(model.Position.Get(), uint32_t(sizeof(XMFLOAT3) * vbPos.size()), vbPos.data());
    appBase->WriteToUploadHeapMemory(model.Normal.Get(), uint32_t(sizeof(XMFLOAT3) * vbNrm.size()), vbNrm.data());
    appBase->WriteToUploadHeapMemory(model.UV0.Get(), uint32_t(sizeof(XMFLOAT2)* vbUV0.size()), vbUV0.data());
    
    if (hasTangent) {
      appBase->WriteToUploadHeapMemory(model.Tangent.Get(), uint32_t(sizeof(XMFLOAT3) * vbTangents.size()), vbTangents.data());
    }

    if (hasBone) {
      appBase->WriteToUploadHeapMemory(model.BoneIndices.Get(), uint32_t(sizeof(XMINT4) * vbBIndices.size()), vbBIndices.data());
      appBase->WriteToUploadHeapMemory(model.BoneWeights.Get(), uint32_t(sizeof(XMFLOAT4) * vbBWeights.size()), vbBWeights.data());
    }
    appBase->WriteToUploadHeapMemory(model.Indices.Get(), uint32_t(sizeof(UINT) * ibIndices.size()), ibIndices.data());

    model.vertexBufferViews[ModelAsset::VBV_Position] = {
      model.Position->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT3) * vbPos.size()), uint32_t(sizeof(XMFLOAT3))
    };
    model.vertexBufferViews[ModelAsset::VBV_Normal] = {
      model.Normal->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT3) * vbNrm.size()), uint32_t(sizeof(XMFLOAT3))
    };
    model.vertexBufferViews[ModelAsset::VBV_UV0] = {
      model.UV0->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT2) * vbUV0.size()), uint32_t(sizeof(XMFLOAT2))
    };
    if (hasTangent) {
      model.vertexBufferViews[ModelAsset::VBV_Tangent] = {
        model.Tangent->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT3) * vbTangents.size()), uint32_t(sizeof(XMFLOAT3))
      };
    }
    if (hasBone) {
      model.vertexBufferViews[ModelAsset::VBV_BlendIndices] = {
        model.BoneIndices->GetGPUVirtualAddress(), uint32_t(sizeof(XMINT4) * vbBIndices.size()), sizeof(XMINT4)
      };
      model.vertexBufferViews[ModelAsset::VBV_BlendWeights] = {
        model.BoneWeights->GetGPUVirtualAddress(), uint32_t(sizeof(XMFLOAT4) * vbBWeights.size()), sizeof(XMFLOAT4)
      };
    }
    model.indexBufferView = { model.Indices->GetGPUVirtualAddress(), uint32_t(sizeof(UINT) * ibIndices.size()), DXGI_FORMAT_R32_UINT };

    auto mtx = ConvertMatrix(scene->mRootNode->mTransformation);
    model.invGlobalTransform = XMMatrixInverse(nullptr, mtx);
    return model;
  }


  void Node::UpdateMatrices(DirectX::XMMATRIX mtxParent) {
    worldTransform = transform * mtxParent;

    for (auto& c : children) {
      c->UpdateMatrices(worldTransform);
    }
  }

  void ModelAsset::Release() {
    delete importer;
    importer = nullptr;
    scene = nullptr;

    Position = nullptr;
    Normal = nullptr; 
    UV0 = nullptr;
    BoneIndices = nullptr; 
    BoneWeights = nullptr;
    Tangent = nullptr;
    Indices = nullptr;

    DrawBatches.clear();
    extraBuffers.clear();
    extraHandles.clear();
    rootNode.reset();
  }

  std::shared_ptr<Node> ModelAsset::FindNode(const std::string& name) {
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

}