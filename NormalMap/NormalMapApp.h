#pragma once
#include "D3D12AppBase.h"
#include "DirectXMath.h"
#include "Camera.h"

#include <array>
#include <unordered_map>

#include "Model.h"

class NormalMapApp : public D3D12AppBase {
public:
  NormalMapApp();

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

    UINT drawFlag = 0;
    float heightScale = 0.02f;
  };
  ShaderParameters m_sceneParameters;

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

  std::vector<Buffer> m_sceneParameterCB;

  using PipelineState = ComPtr<ID3D12PipelineState>;
  std::unordered_map<std::string, PipelineState> m_pipelines;

  enum RootParameterList {
    RP_SCENE_CB = 0,
    RP_MATERIAL = 1,
    RP_BASE_COLOR = 2,
    RP_NORMAL_MAP = 3,
    RP_HEIGHT_MAP = 4,
  };

  enum DrawMode
  {
    DrawMode_NormalMap,
    DrawMode_ParallaxMap,
    DrawMode_ParallaxOcclusion,
  };
  DrawMode m_mode = DrawMode_NormalMap;

  model::ModelAsset m_model;

  const std::string PSO_DEFAULT = "PSO_DEFAULT";
  
  UINT64 m_frameCount = 0;

  Texture m_texPlaneBase;
  Texture m_texPlaneNormal;
  Texture m_texPlaneHeight;
};