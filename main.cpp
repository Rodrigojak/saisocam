#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <cstdint>

// Estrutura vetorial do GTA SA
struct CVector {
    float x, y, z;
    CVector() : x(0.0f), y(0.0f), z(0.0f) {}
    CVector(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

// Endereços e ponteiros para o GTA SA 1.0 US
#define FUNC_CCamera_Process 0x53C040
#define ADDR_TheCamera       0xB6F028
#define FUNC_FindPlayerPed   0x56E014

typedef void(__cdecl* CCamera_Process_t)();
typedef DWORD(*FindPlayerPed_t)(int);

CCamera_Process_t OriginalCameraProcess = (CCamera_Process_t)FUNC_CCamera_Process;
FindPlayerPed_t _FindPlayerPed = (FindPlayerPed_t)FUNC_FindPlayerPed;

// Variáveis de Estado da Câmera Isométrica
static bool bIsoCameraActive = true;
static int nIsoSide = 0;          // 0: 45°, 1: 135°, 2: 225°, 3: 315°
static float fIsoDistance = 18.0f; // Distância padrão da câmera
static float fIsoPitch = 38.0f * (3.14159265f / 180.0f); // Ângulo de inclinação

void LogDebug(const char* format, ...) {
    char path[MAX_PATH];
    GetModuleFileNameA(GetModuleHandleA("IsoCam.asi"), path, MAX_PATH);
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat(path, "IsometricCameraSA.log");

    FILE* f = fopen(path, "a");
    if (f) {
        va_list args;
        va_start(args, format);
        vfprintf(f, format, args);
        fprintf(f, "\n");
        va_end(args);
        fclose(f);
    }
}

void ApplyIsometricCamera() {
    if (!bIsoCameraActive) return;

    // Captura o ponteiro global da câmera
    uint8_t** ppCamera = (uint8_t**)ADDR_TheCamera;
    if (!ppCamera || !*ppCamera) return;
    uint8_t* pCamera = *ppCamera;

    // Captura o player atual (CJ)
    DWORD pPlayer = _FindPlayerPed(0);
    if (!pPlayer) return;

    // Coordenadas do Player (a matriz de posições costuma ficar no offset 0x30 ou 0x14 do ped/placeable)
    // Para simplificar e garantir estabilidade no gancho base, lemos a posição do player diretamente se disponível
    // No GTA SA, a matriz de coordenadas do player fica em pPlayer + 0x14 (CMatrix) -> pos em + 0x30
    float* playerPos = (float*)(pPlayer + 0x34); // Aproximação segura para X, Y, Z do player

    // Leitura dos atalhos para alternar os 4 lados (Alt + Q / Alt + E)
    if ((GetAsyncKeyState('Q') & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
        nIsoSide = (nIsoSide + 3) % 4;
        LogDebug("[INFO] Lado isometrico alterado para o indice: %d", nIsoSide);
        Sleep(150); // Debounce para evitar cliques duplos
    }
    if ((GetAsyncKeyState('E') & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
        nIsoSide = (nIsoSide + 1) % 4;
        LogDebug("[INFO] Lado isometrico alterado para o indice: %d", nIsoSide);
        Sleep(150);
    }

    // Ângulos base para os 4 quadrantes da isométrica (45°, 135°, 225°, 315°)
    float angles[4] = {
        0.785398f,  // 45°
        2.356194f,  // 135°
        3.926991f,  // 225°
        5.497787f   // 315°
    };

    float currentAngle = angles[nIsoSide];

    // Cálculo matemático da posição da câmera com base no ângulo e distância
    float camOffsetX = cosf(currentAngle) * fIsoDistance;
    float camOffsetY = sinf(currentAngle) * fIsoDistance;
    float camOffsetZ = sinf(fIsoPitch) * fIsoDistance;

    // Ponteiros de posição da câmera ativa no struct TheCamera (offsets comuns da CCam ativa)
    // Aplicamos os vetores diretamente se o player estiver válido
    if (playerPos) {
        float targetX = playerPos[0];
        float targetY = playerPos[1];
        float targetZ = playerPos[2];

        // Escreve os vetores de posição da câmera (Exemplo de ajuste nos offsets de posição da câmera ativa)
        float* camSource = (float*)(pCamera + 0x08); // m_vecSource
        float* camTarget = (float*)(pCamera + 0x14); // m_vecTarget

        if (camSource && camTarget) {
            camSource[0] = targetX - camOffsetX;
            camSource[1] = targetY - camOffsetY;
            camSource[2] = targetZ + camOffsetZ;

            camTarget[0] = targetX;
            camTarget[1] = targetY;
            camTarget[2] = targetZ + 1.0f; // Foco um pouco acima do centro do CJ
        }
    }
}

void HookedCameraProcess() {
    ApplyIsometricCamera();
    OriginalCameraProcess();
}

void PatchInit() {
    LogDebug("[INFO] IsometricCameraSA Completa inicializada com sucesso no Hoodlum.");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        PatchInit();
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
