#pragma once
#include "D3D12AppBase.h"
#include "DirectXMath.h"
#include "Camera.h"

#include <array>
#include <unordered_map>

#include "Model.h"

class WaitableSwapchainApp : public D3D12AppBase {
public:
  WaitableSwapchainApp();

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

    DirectX::XMFLOAT4 pointLightColors[8];
    DirectX::XMFLOAT4 pointLights[100];
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
  void DrawModelInZPrePass();
  void DrawModelInGBuffer();
  void DeferredLightingPass();
private:
  Camera m_camera;

  ComPtr<ID3D12RootSignature> m_rootSignature;
  ComPtr<ID3D12RootSignature> m_rootSignatureLighting;
  ComPtr<ID3D12RootSignature> m_rootSignatureZPrePass;
  std::vector<Buffer> m_sceneParameterCB;

  using PipelineState = ComPtr<ID3D12PipelineState>;
  std::unordered_map<std::string, PipelineState> m_pipelines;

  enum RootParameterList {
    RP_SCENE_CB = 0,
    RP_MATERIAL = 1,
    RP_ALBEDO = 2,
    RP_SPECULAR = 3,
  };

  enum RootParameterListDeferredLighting {
    RP_LIGHTING_SCENE_CB = 0,
    RP_LIGHTING_WPOS = 1,
    RP_LIGHTING_NORMAL = 2,
    RP_LIGHTING_ALBEDO = 3,
  };

  model::ModelAsset m_model;

  const std::string PSO_DEFAULT = "PSO_DEFAULT";
  const std::string PSO_ZPREPASS = "PSO_ZPREPASS";
  const std::string PSO_DRAW_LIGHTING = "PSO_LIGHTING";

  struct GBuffer {
    Buffer worldPosition;
    Buffer worldNormal;
    Buffer albedo;

    DescriptorHandle srvWorldPosition;
    DescriptorHandle srvWorldNormal;
    DescriptorHandle srvAlbedo;

    DescriptorHandle rtvWorldPosition;
    DescriptorHandle rtvWorldNormal;
    DescriptorHandle rtvAlbedo;
  };
  GBuffer m_gbuffer;
};