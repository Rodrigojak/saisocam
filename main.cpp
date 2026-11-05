#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

// Ponteiros para as funções originais do DirectX 9
typedef HRESULT(APIEND* EndScene_t)(LPDIRECT3DDEVICE9 pDevice);
EndScene_t oEndScene = nullptr;

// Textura de máscara externa (ex: gradiente em preto e branco)
LPDIRECT3DTEXTURE9 g_pMaskTexture = nullptr;
IDirect3DPixelShader9* g_pPixelShader = nullptr;

// Pixel Shader em HLSL embutido para criar o efeito de pixel art / gradiente alpha
const char* szShaderCode = 
    "sampler2D tex0 : register(s0);\n"
    "sampler2D texMask : register(s1);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    "   float4 baseColor = tex2D(tex0, uv);\n"
    "   float4 maskColor = tex2D(texMask, uv);\n"
    "   // Usa a luminância da máscara (preto e branco) como fator alpha/intensidade\n"
    "   float factor = dot(maskColor.rgb, float3(0.299, 0.587, 0.114));\n"
    "   // Exemplo: aplica o filtro de cor/pixelização baseado na máscara\n"
    "   float4 filteredColor = baseColor * factor + baseColor * (1.0 - factor);\n"
    "   return filteredColor;\n"
    "}\n";

// Hook da função EndScene do DirectX 9 onde desenhamos os efeitos na tela
HRESULT APIENTRY hkEndScene(LPDIRECT3DDEVICE9 pDevice) {
    if (!g_pMaskTexture) {
        // Tenta carregar a imagem de máscara da pasta raiz do jogo
        D3DXCreateTextureFromFileA(pDevice, "mask.png", &g_pMaskTexture);
    }

    if (g_pMaskTexture) {
        // Aqui você aplicaria o Pixel Shader e desenharia o quad na tela
        pDevice->SetTexture(1, g_pMaskTexture);
    }

    return oEndScene(pDevice);
}

// Ponto de entrada da DLL/ASI
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        // O código de hook do DirectX 9 é inicializado aqui
        break;
    case DLL_PROCESS_DETACH:
        if (g_pMaskTexture) g_pMaskTexture->Release();
        break;
    }
    return TRUE;
}
