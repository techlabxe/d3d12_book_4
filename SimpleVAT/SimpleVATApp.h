#pragma once
#include "D3D12AppBase.h"
#include "DirectXMath.h"
#include "Camera.h"

#include <array>
#include <unordered_map>

#include "Model.h"

class SimpleVATApp : public D3D12AppBase {
public:
  SimpleVATApp();

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

    UINT animationFrame;
  };
  ShaderParameters m_sceneParameters;


private:
  struct ShaderDrawMeshParameter {
    DirectX::XMMATRIX mtxWorld;
    DirectX::XMFLOAT4 diffuse; // xyz: diffuseRGB, w: specularShininess
    DirectX::XMFLOAT4 ambient; // xyz: ambientRGB
  };

  struct VATData {
    Texture texPosition;
    Texture texNormal;
    int vertexCount;
    int animationCount;
  };

  void CreateRootSignatures();
  void PreparePipeline();

  void PrepareVATData();
  VATData LoadVAT(std::string path);

  void RenderHUD();
  
  void DrawModel();

private:
  Camera m_camera;

  ComPtr<ID3D12RootSignature> m_rootSignature;
  ComPtr<ID3D12RootSignature> m_rootSignatureVAT;
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
  enum VATRootParameterList {
    RP_VAT_SCENE_CB = 0,
    RP_VAT_MATERIAL = 1,
    RP_VAT_POSITON  = 2,
    RP_VAT_NORMAL = 3,
  };

  enum DrawMode
  {
    DrawMode_Fluid,
    DrawMode_Destroy,
  };
  DrawMode m_mode = DrawMode_Fluid;

  model::ModelAsset m_model;

  const std::string PSO_DEFAULT = "PSO_DEFAULT";
  const std::string PSO_VAT_DRAW = "PSO_VAT_DRAW";
  
  UINT64 m_frameCount = 0;

  Texture m_texPlaneBase;

  VATData m_vatFluid, m_vatDestroy;
  ShaderDrawMeshParameter m_vatFluidMaterial, m_vatDestroyMaterial;
  std::vector<Buffer> m_vatMaterialCB;

  bool m_autoAnimation = true;
};