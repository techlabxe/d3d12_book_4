#define LIGHT_NUM (100)

struct PSInput
{
  float4 Position : SV_POSITION;
  float2 UV0 : TEXCOORD0;
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

  float4   lightColors[8];
  float4   pointLights[LIGHT_NUM];

  float4x4 invViewProj;
}

cbuffer ShaderDrawMeshParameter : register(b1) {
  float4x4 mtxWorld;
  float4 diffuse;
  float4 ambient;
}

Texture2D gWorldPosition : register(t0);
Texture2D gWorldNormal : register(t1);
Texture2D gAlbedo : register(t2);

SamplerState gSampler : register(s0);


PSInput mainVS(uint vertexId : SV_VertexId)
{
  float x = (vertexId >> 1) * 2.0 - 1.0;
  float y = 1.0 - (vertexId &  1) * 2.0;

  PSInput result = (PSInput)0;
  result.Position = float4(x, y, 0, 1);

  float u = (vertexId >> 1);
  float v = (vertexId & 1);

  result.UV0 = float2(u, v);
  return result;
}

float4  mainPS(PSInput In) : SV_TARGET
{
  float2 uv = In.UV0;

  float4 worldPosition = gWorldPosition.Sample(gSampler, uv);

  if (worldPosition.w < 0.5) {
    discard; // 位置記録がない
  }

  float4 worldNormal = gWorldNormal.Sample(gSampler, uv);
  float4 albedo = gAlbedo.Sample(gSampler, uv);
  float3 normal = normalize(worldNormal.xyz);
  float3 toCameraDir = normalize(cameraPosition.xyz - worldPosition.xyz);

  // スペキュラの計算(マスク考慮).
  float specularPower = worldNormal.w;
  float3 r = reflect(-normalize(lightDir.xyz), normal);
  float specular = saturate(dot(r, toCameraDir));
  float specularMask = albedo.w;
  specular = pow(specular, specularPower) * specularMask;

  // ランバートライティング
  float dotNL = dot(normalize(lightDir.xyz), normal);
  dotNL = saturate(dotNL);

  float3 color = 0;
  color += dotNL * albedo.xyz;
  color += specular;
  color += albedo.xyz * 0.1; // ambient

  // シーン内のポイントライトの処理.
  [unroll]
  for (int i = 0; i < LIGHT_NUM; ++i) {
    float lightDistance = length(worldPosition.xyz - pointLights[i].xyz);
    if (lightDistance < pointLights[i].w) {
      // ライトの影響を受ける
      float rate = saturate(1.0 - lightDistance / pointLights[i].w);
      float3 lightColor = rate * lightColors[i % 8].xyz;
      color += lightColor * albedo.xyz;
    }
  }
  return float4(color, 1);
}

