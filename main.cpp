#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <cstdint>

// Estruturas Nativas Básicas do GTA SA 1.0 US
struct CVector {
    float x, y, z;
    CVector() : x(0.0f), y(0.0f), z(0.0f) {}
    CVector(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
};

// Endereços globais do GTA SA 1.0 US
#define FUNC_CCamera_Process 0x53C040
#define ADDR_TheCamera       0xB6F028

typedef void(__cdecl* CCamera_Process_t)();
CCamera_Process_t OriginalCameraProcess = (CCamera_Process_t)FUNC_CCamera_Process;

// Variáveis de Estado da Câmera Isométrica
static bool bIsoCameraActive = true;
static int nIsoSide = 0; // 0: 45°, 1: 135°, 2: 225°, 3: 315°
static float fIsoDistance = 18.0f;
static float fIsoPitch = 38.0f * (3.14159265f / 180.0f);

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

    // Ponteiro global da TheCamera (0xB6F028)
    uint8_t* pCamera = *(uint8_t**)ADDR_TheCamera;
    if (!pCamera) return;

    // Leitura das teclas para alternar os 4 lados da perspectiva (Alt + Q / Alt + E)
    if ((GetAsyncKeyState('Q') & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
        nIsoSide = (nIsoSide + 3) % 4;
        Sleep(150); // Debounce simples
    }
    if ((GetAsyncKeyState('E') & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)) {
        nIsoSide = (nIsoSide + 1) % 4;
        Sleep(150);
    }
}

void HookedCameraProcess() {
    ApplyIsometricCamera();
    OriginalCameraProcess();
}

void PatchInit() {
    LogDebug("[INFO] IsometricCameraSA inicializada com sucesso no Hoodlum.");
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
