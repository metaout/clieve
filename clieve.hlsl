Texture2D shaderTexture;
SamplerState samplerState;

cbuffer PixelShaderSettings {
  float Time;
  float Scale;
  float2 Resolution;
  float4 Background;
};

static const int N = 12;

float4 readPixel(float2 pixel) {
  return shaderTexture.Sample(samplerState, (pixel + 0.5) / Resolution);
}

float3 readRipple(int index) {
  float x = Resolution.x * ((float(index) + 0.5) / float(N));
  return readPixel(float2(x, 0.5)).rgb;
}

float rippleHeight(float2 tex, float2 origin, float age, float aspect) {
  float life = saturate(1.0 - age);
  float decay = pow(life, 1.35);

  float2 delta = float2((tex.x - origin.x) * aspect, tex.y - origin.y);
  float dist = length(delta);

  float radius = age * 1.05;
  float width = 0.035 + age * 0.08;
  float trailWidth = 0.18 + age * 0.28;

  float phase = (dist - radius) * 70.0;

  float frontBand = (dist - radius) / width;
  float frontEnvelope = exp(-frontBand * frontBand);

  float inside = 1.0 - step(radius, dist);
  float trailBand = saturate((radius - dist) / max(trailWidth, 0.0001));
  float trailEnvelope = inside * exp(-trailBand * 2.4);

  float envelope = max(frontEnvelope, trailEnvelope * 0.52) * decay;
  float centerFade = smoothstep(0.0, 0.03, dist);

  float waveA = sin(phase - age * 26.0);
  float waveB = sin(phase * 0.48 - age * 12.0) * 0.32;
  float waveC = sin(phase * 1.75 - age * 42.0) * 0.22;

  return (waveA + waveB + waveC) * envelope * centerFade * 1.25;
}

float waterHeight(float2 tex, float aspect) {
  float height = 0.0;

  [unroll]
  for (int i = 0; i < N; i++) {
    float3 ripple = readRipple(i);
    float2 origin = ripple.rg;
    float age = ripple.b;

    height += rippleHeight(tex, origin, age, aspect);
  }

  return height;
}

float3 waterNormal(float2 tex, float aspect) {
  float dx = 1.0 / Resolution.x;
  float dy = 1.0 / Resolution.y;

  float hL = waterHeight(tex - float2(dx, 0.0), aspect);
  float hR = waterHeight(tex + float2(dx, 0.0), aspect);
  float hU = waterHeight(tex - float2(0.0, dy), aspect);
  float hD = waterHeight(tex + float2(0.0, dy), aspect);

  float2 grad = float2(hR - hL, hD - hU);

  return normalize(float3(-grad.x * 7.0, -grad.y * 7.0, 1.0));
}

float4 main(float4 pos : SV_POSITION, float2 tex : TEXCOORD) : SV_TARGET {
  float metaHeight = 0.01;

  if (tex.y < metaHeight) {
    return float4(Background.rgb, 1.0);
  }

  float aspect = Resolution.x / Resolution.y;

  float height = waterHeight(tex, aspect);
  float3 normal = waterNormal(tex, aspect);

  float2 refractOffset = normal.xy * 0.075;
  refractOffset.x /= aspect;

  float2 uv = tex + refractOffset;
  uv = saturate(uv);
  uv.y = max(uv.y, metaHeight);

  float4 base = shaderTexture.Sample(samplerState, uv);
  
  float3 lightDir = normalize(float3(-0.35, -0.55, 0.75));
  float3 viewDir = normalize(float3(0.0, 0.0, 1.0));
  float3 halfDir = normalize(lightDir + viewDir);

  float diffuse = saturate(dot(normal, lightDir));
  float specular = pow(saturate(dot(normal, halfDir)), 70.0);

  float fresnel = pow(1.0 - saturate(normal.z), 2.0);
  float shade = 0.92 + diffuse * 0.12;

  float3 waterTint = float3(0.82, 0.94, 1.0);
  float3 color = base.rgb;

  color *= shade;
  color = lerp(color, color * waterTint, 0.16);
  color += specular * 0.65;
  color += fresnel * 0.12;
  color += height * 0.055;

  return float4(saturate(color), 1.0);
}