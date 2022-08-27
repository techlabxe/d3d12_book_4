struct PSInput
{
  float4 Position : SV_POSITION;
  float4 Color : COLOR;
};

cbuffer SceneParameter : register(b0)
{
  float4x4 view;
  float4x4 proj;
  float4   lightDir;
  float4   cameraPosition;

  uint     animationFrame;
}
cbuffer ShaderDrawMeshParameter : register(b1) {
  float4x4 mtxWorld;
  float4 diffuse;
  float4 ambient;
}

Texture2D gTexPosition: register(t0);
Texture2D gTexNormal  : register(t1);

PSInput mainVS(uint vertexId : SV_VertexID)
{
  PSInput result = (PSInput)0;
  int3 loc = 0;
  loc.x = vertexId;
  loc.y = animationFrame;
  
  float4 pos = gTexPosition.Load(loc);
  float4 nrm = gTexNormal.Load(loc);

  if (pos.w < 0.5) {
    pos.w = -1;
  }

  float4x4 mtxVP = mul(view, proj);
  float4 worldPosition = mul(pos, mtxWorld);
  float3 worldNormal = mul(nrm, (float3x3)mtxWorld);
  result.Position = mul(worldPosition, mtxVP);

  float dotNL = saturate(dot(normalize(lightDir.xyz), worldNormal));
  result.Color.xyz = dotNL * diffuse.xyz + ambient.xyz;

  return result;
}

float4 mainPS(PSInput In) : SV_TARGET
{
  return In.Color;
}

