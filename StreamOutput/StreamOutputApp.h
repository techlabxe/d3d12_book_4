#pragma once
#include "D3D12AppBase.h"
#include "DirectXMath.h"
#include "Camera.h"

#include <array>
#include <unordered_map>

#include "Model.h"

class StreamOutputApp : public D3D12AppBase {
public:
  StreamOutputApp();

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
  };
  ShaderParameters m_scenePatameters;

  struct ShaderDrawMeshParameter
  {
    DirectX::XMUINT4  offset;
    DirectX::XMMATRIX bones[512];
  };
private:
  void CreateRootSignatures();

  void PreparePipeline();

  void RenderHUD();
private:
  Camera m_camera;

  ComPtr<ID3D12RootSignature> m_rootSignature, m_rootSignatureGS;
  std::vector<Buffer> m_sceneParameterCB;

  using PipelineState = ComPtr<ID3D12PipelineState>;
  std::unordered_map<std::string, PipelineState> m_pipelines;

  enum DrawMode
  {
    DrawMode_GS = 0,
    DrawMode_VS,
  };
  DrawMode m_mode = DrawMode_GS;

  model::ModelAsset m_skinActor;

  const std::string PSO_VS_OUT = "VS_OUT";
  const std::string PSO_GS_OUT = "GS_OUT";
  const std::string PSO_SO_DRAW = "SO_DRAW";
};