#pragma once
#include "D3D12AppBase.h"
#include "DirectXMath.h"
#include "Camera.h"

#include <array>
#include <unordered_map>

#include "Model.h"

class GPUParticleApp : public D3D12AppBase {
public:
  GPUParticleApp();

  virtual void Prepare();
  virtual void Cleanup();
  virtual void Render();

  virtual void OnMouseButtonDown(UINT msg);
  virtual void OnMouseButtonUp(UINT msg);
  virtual void OnMouseMove(UINT msg, int dx, int dy);

  struct ShaderParameters
  {
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 proj;
    DirectX::XMFLOAT4 lightDir;
    DirectX::XMFLOAT4 cameraPosition;

    DirectX::XMFLOAT4 forceCenter1;

    UINT MaxParticleCount;
    float frameDeltaTime = 0;

    UINT padd0 = 0;
    UINT padd1 = 0;

    DirectX::XMFLOAT4 particleColors[8];
    DirectX::XMFLOAT4X4 matBillboard;
  };
  ShaderParameters m_sceneParameters;
  const UINT MaxParticleCount = 100000;

  struct GpuParticleElement {
    UINT  isActive;	// ê∂ë∂ÉtÉâÉO.
    float lifeTime;
    float elapsed;
    UINT  colorIndex;
    DirectX::XMFLOAT4 position;
    DirectX::XMFLOAT4 velocity;
  };

private:
  struct ShaderDrawMeshParameter {
    DirectX::XMMATRIX mtxWorld;
    DirectX::XMFLOAT4 diffuse; // xyz: diffuseRGB, w: specularShininess
    DirectX::XMFLOAT4 ambient; // xyz: ambientRGB
  };
  void CreateRootSignatures();
  
  void PreparePipeline();

  void RenderHUD();
  
  void DrawModelWithNormalMap();

private:
  Camera m_camera;

  ComPtr<ID3D12RootSignature> m_rootSignature;
  ComPtr<ID3D12RootSignature> m_rootSignatureCompute;
  ComPtr<ID3D12RootSignature> m_rootSignatureParticleDraw;
  ComPtr<ID3D12RootSignature> m_rootSignatureParticleTexDraw;

  std::vector<Buffer> m_sceneParameterCB;

  using PipelineState = ComPtr<ID3D12PipelineState>;
  std::unordered_map<std::string, PipelineState> m_pipelines;

  enum RootParameterList {
    RP_SCENE_CB = 0,
    RP_MATERIAL = 1,
    RP_BASE_COLOR = 2,
  };

  enum RootParameterGpuParticleCompute {
    RP_CS_SCENE_CB = 0,
    RP_CS_PARTICLE = 1,
    RP_CS_PARTICLE_INDEXLIST = 2,
  };
  enum RootParameterGpuParticleRender {
    RP_PARTICLE_DRAW_SCENE_CB = 0,
    RP_PARTICLE_DRAW_DATA = 1,
  };

  enum RootParameterGpuParticleTexRender {
    RP_PARTICLE_DRAW_TEX_SCENE_CB = 0,
    RP_PARTICLE_DRAW_TEX_DATA = 1,
    RP_PARTICLE_DRAW_TEX_TEXTURE = 2,
  };

  model::ModelAsset m_model;
  Texture m_texPlaneBase;

  const std::string PSO_DEFAULT = "PSO_DEFAULT";
  const std::string PSO_CS_INIT = "PSO_CS_INIT";
  const std::string PSO_CS_EMIT = "PSO_CS_EMIT";
  const std::string PSO_CS_UPDATE = "PSO_CS_UPDATE";
  const std::string PSO_DRAW_PARTICLE = "PSO_DRAW_PARTICLE";
  const std::string PSO_DRAW_PARTICLE_USE_TEX = "PSO_DRAW_PARTICLE_USE_TEX";

  
  struct ParticleVertex {
    DirectX::XMFLOAT3 Pos; DirectX::XMFLOAT2 UV0;
  };

  Buffer m_gpuParticleIndexList;
  Buffer m_gpuParticleElement;
  DescriptorHandle m_uavParticleIndexList;

  SimpleModelData m_modelParticleBoard;

  Texture m_texParticle;
  UINT64 m_frameCount = 0;
};