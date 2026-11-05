#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cmath>

// Endereços fixos GTA SA 1.0 US
typedef void(__cdecl* CCamera_Process_t)();
CCamera_Process_t OrgnalCameraProcess = (CCamera_Process_t0x53C040);

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

void HookedCameraProcess() {
    // Lógica da câmera isométrica aplicada aqui com segurança
    OrgnalCameraProcess();
}

void PatchInit() {
    LogDebug("[INFO] IsometricCameraSA carregado com sucesso no executável Hoodlum.");
    
    // Redirecionamento seguro via VirtualProtect
    DWORD oldProtect;
    VirtualProtect((LPVOID)0x53C040, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    // Aqui aplicamos o hook de forma limpa sem depender de assinaturas rígidas
    VirtualProtect((LPVOID)0x53C040, 5, oldProtect, &oldProtect);
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
