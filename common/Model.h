#pragma once

#include <DirectXMath.h>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <filesystem>

#include "D3D12AppBase.h"

struct aiBone;
struct aiScene;
namespace Assimp {
  class Importer;
}


namespace model {
  using Buffer = D3D12AppBase::Buffer;

  struct Node {
    std::vector<std::shared_ptr<Node>> children;
    DirectX::XMMATRIX  transform = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX  worldTransform = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX  offsetMatrix = DirectX::XMMatrixIdentity();
    std::string name;

    Node() = default;

    void UpdateMatrices(DirectX::XMMATRIX mtxParent);
  };

  struct Material {
    DescriptorHandle albedoSRV;
    DescriptorHandle specularSRV;

    DirectX::XMFLOAT3 diffuse;
    float shininess = 0;
    DirectX::XMFLOAT3 ambient;
  };

  struct DrawBatch {
    UINT vertexOffsetCount;
    UINT indexCount;
    UINT indexOffsetCount;
    UINT materialIndex;

    std::vector<aiBone*> boneList;
    std::vector<std::shared_ptr<Node>> boneList2;
    std::vector<Buffer>  boneMatrixPalette;
    std::vector<Buffer>  materialParameterCB;
  };

  struct ModelAsset {
    Buffer Position, Normal, UV0;
    Buffer BoneIndices, BoneWeights;
    Buffer Tangent;
    Buffer Indices;

    std::vector<DrawBatch> DrawBatches;
    Assimp::Importer* importer;
    const aiScene* scene;
    UINT   totalVertexCount;
    UINT   totalIndexCount;

    DirectX::XMMATRIX invGlobalTransform;
    std::shared_ptr<Node> rootNode;
    std::vector<Material> materials;

    void Release();
    std::shared_ptr<Node> FindNode(const std::string& name);

    enum VBViewType {
      VBV_Position = 0,
      VBV_Normal,
      VBV_UV0,
      VBV_BlendIndices,
      VBV_BlendWeights,
      VBV_Tangent,
    };
    std::unordered_map<VBViewType, D3D12_VERTEX_BUFFER_VIEW> vertexBufferViews;
    D3D12_INDEX_BUFFER_VIEW indexBufferView;

    // 関連付けて保持しておきたい拡張バッファなど.
    std::unordered_map<std::string, Buffer> extraBuffers;
    std::unordered_map<std::string, DescriptorHandle> extraHandles;
  };

  enum ModelLoadFlag {
    ModelLoadFlag_None = 0,
    ModelLoadFlag_Flip_UV =     1u << 0,
    ModelLoadFlag_CalcTangent=  1u << 1,
  };
  ModelAsset LoadModelData(std::filesystem::path filePath, D3D12AppBase* appBase, ModelLoadFlag loadFlags = ModelLoadFlag_None);
}
