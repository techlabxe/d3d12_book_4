struct VSInput
{
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float2 UV0 : TEXCOORD0;
};

struct PSInput
{
  float4 Position : SV_POSITION;
  float3 Normal : NORMAL;
  float2 UV0 : TEXCOORD0;
  float4 PositionW : TEXCOORD1;
};

struct PSOutput
{
  float4 Position : SV_TARGET0;
  float4 NormalAndSpc     : SV_TARGET1;
  float4 AlbedoAndSpcMask : SV_TARGET2;
};

cbuffer SceneParameter : register(b0)
{
  float4x4 view;
  float4x4 proj;
  float4   lightDir;
  float4   cameraPosition;
}

cbuffer ShaderDrawMeshParameter : register(b1) {
  float4x4 mtxWorld;
  float4 diffuse;
  float4 ambient;
}

Texture2D gAlbedo : register(t0);
Texture2D gSpecular : register(t1);

SamplerState gSampler : register(s0);



PSInput mainVS(VSInput In)
{
  float4x4 mtxVP = mul(view, proj);
  float4 worldPosition = mul(In.Position, mtxWorld);
  PSInput result = (PSInput)0;
  result.Position = mul(worldPosition, mtxVP);
  result.Normal = mul(In.Normal, (float3x3)mtxWorld);
  result.UV0 = In.UV0;

  result.PositionW = worldPosition;
  return result;
}

PSOutput  mainPS(PSInput In) 
{
  float4 albedo = gAlbedo.Sample(gSampler, In.UV0) * float4(diffuse.xyz, 1);
  clip(albedo.w - 0.5);

  float specularMask = gSpecular.Sample(gSampler, In.UV0).r;

  PSOutput output = (PSOutput)0;
  output.Position = In.PositionW;
  output.NormalAndSpc.xyz = In.Normal.xyz;
  output.NormalAndSpc.w = diffuse.w;
  output.AlbedoAndSpcMask.xyz = albedo.xyz;
  output.AlbedoAndSpcMask.w = specularMask;

  return output;
}


void mainPS_zprepass(PSInput In)
{
  float4 albedo = gAlbedo.Sample(gSampler, In.UV0) * float4(diffuse.xyz, 1);
  clip(albedo.w - 0.5);
}



