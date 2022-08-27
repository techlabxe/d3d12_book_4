struct GpuParticleElement
{
  uint  isActive;	// ê∂ë∂ÉtÉâÉO.
  float lifeTime;
  float elapsed;
  uint  colorIndex;
  float4 position;
  float4 velocity;
};

cbuffer SceneParameter : register(b0)
{
  float4x4 view;
  float4x4 proj;
  float4   lightDir;
  float4   cameraPosition;

  float4 forceCenter1;

  uint  MaxParticleCount;
  float frameDeltaTime;
  uint padd0;
  uint padd1;

  float4 particleColors[8];
  float4x4 matBillboard;
}

RWStructuredBuffer<GpuParticleElement> gParticles : register(u0);

struct PSInput {
  float4 Position : SV_POSITION;
  float4 Color : COLOR;
};


PSInput mainVS(uint vertexId : SV_VertexId) {
  PSInput output = (PSInput)0;

  if (vertexId >= MaxParticleCount) {
    return output;
  }
  if (gParticles[vertexId].isActive == 0) {
    return output;
  }

  float4 position = gParticles[vertexId].position;
  uint colorIndex = gParticles[vertexId].colorIndex;
  position.w = 1;
  float4x4 mtxVP = mul(view, proj);
  output.Position = mul(position, mtxVP);
  output.Color = particleColors[colorIndex];
  return output;
}

float4 mainPS(PSInput input) : SV_TARGET {
  return input.Color;
}



struct VSInputEx {
  float4 Position : POSITION;
  float2 UV0 : TEXCOORD0;
  uint   instanceID : SV_InstanceID;
};
struct PSInputEx {
  float4 Position : SV_POSITION;
  float3 Color : COLOR;
  float2 UV0   : TEXCOORD0;
};

PSInputEx mainVSEx(VSInputEx input) {
  PSInputEx output = (PSInputEx)0;
  int index = input.instanceID;

  if (gParticles[index].isActive == 0) {
    return output;
  }
  float4x4 mtxVP = mul(view, proj);
  float4 position = mul(matBillboard, input.Position);
  position += gParticles[index].position;
  position.w = 1;
  output.Position = mul(position, mtxVP);

  uint colorIndex = gParticles[index].colorIndex;
  output.Color = particleColors[colorIndex].xyz;
  output.UV0 = input.UV0;

  return output;
}

Texture2D gTexParticle : register(t0);
SamplerState gSampler : register(s0);


float4 mainPSEx(PSInputEx input) : SV_Target{
  float4 color = gTexParticle.Sample(gSampler, input.UV0);
  return float4(input.Color * color.xyz, color.a);
}
