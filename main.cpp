#include <windows.h>
#include <cmath>
#include <cstdint>

struct CVector {
    float x, y, z;
};

#define ADDR_PLAYER_PED         0xB6F5F0
#define ADDR_MENU_ACTIVE        0xBA67A4
#define ADDR_WIDESCREEN_ON      0xB6F065

typedef void(__thiscall* RestoreWithJumpCut_t)(void* pCamera);
typedef void(__cdecl* AddMessageJumpQ_t)(const char* text, unsigned int time, unsigned short flag, bool bPreviousBrief);

RestoreWithJumpCut_t CamRestoreWithJumpCut = (RestoreWithJumpCut_t)0x50BAB0;
AddMessageJumpQ_t ShowScreenMessage = (AddMessageJumpQ_t)0x69F1E0;

int g_CamMode = 0; // 0 = Normal do Jogo, 1 = Isométrica
DWORD g_LastKeyTime = 0;

bool GetEntityPosition(uintptr_t entityPtr, CVector& outPos) {
    if (!entityPtr) return false;
    uintptr_t matrixPtr = *(uintptr_t*)(entityPtr + 0x14);
    if (matrixPtr) {
        outPos.x = *(float*)(matrixPtr + 0x30);
        outPos.y = *(float*)(matrixPtr + 0x34);
        outPos.z = *(float*)(matrixPtr + 0x38);
        return true;
    }
    outPos.x = *(float*)(entityPtr + 0x04);
    outPos.y = *(float*)(entityPtr + 0x08);
    outPos.z = *(float*)(entityPtr + 0x0C);
    return true;
}

void ApplyIsometricCamera(uintptr_t camStruct, const CVector& targetPos) {
    if (!camStruct) return;
    uint8_t activeCamIndex = *(uint8_t*)(camStruct + 0x59);
    uintptr_t activeCam = camStruct + 0x174 + (activeCamIndex * 0x238);

    // Posição fixa isométrica limpa e estável
    CVector camPos = {
        targetPos.x - 12.0f,
        targetPos.y - 12.0f,
        targetPos.z + 14.0f
    };

    CVector front = {
        targetPos.x - camPos.x,
        targetPos.y - camPos.y,
        targetPos.z - camPos.z
    };
    float len = std::sqrt(front.x * front.x + front.y * front.y + front.z * front.z);
    if (len > 0.0001f) { front.x /= len; front.y /= len; front.z /= len; }

    CVector worldUp = { 0.0f, 0.0f, 1.0f };
    CVector right = {
        front.y * worldUp.z - front.z * worldUp.y,
        front.z * worldUp.x - front.x * worldUp.z,
        front.x * worldUp.y - front.y * worldUp.x
    };
    float rLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rLen > 0.0001f) { right.x /= rLen; right.y /= rLen; right.z /= rLen; }

    CVector up = {
        right.y * front.z - right.z * front.y,
        right.z * front.x - right.x * front.z,
        right.x * front.y - right.y * front.x
    };

    *(CVector*)(activeCam + 0x130) = front;
    *(CVector*)(activeCam + 0x13C) = camPos;
    *(CVector*)(activeCam + 0x154) = up;
}

typedef void(__thiscall* CameraProcess_t)(void* pCamera);
CameraProcess_t OriginalCameraProcess = nullptr;

void __fastcall HookedCameraProcess(void* pCamera, void* edx) {
    // Validação estrita para evitar crash por ponteiro nulo (0x00000000)
    if (!pCamera) {
        if (OriginalCameraProcess) OriginalCameraProcess(pCamera);
        return;
    }

    if (OriginalCameraProcess) {
        OriginalCameraProcess(pCamera);
    }

    bool menuActive = *(bool*)ADDR_MENU_ACTIVE;
    bool cutsceneActive = *(bool*)ADDR_WIDESCREEN_ON;
    uintptr_t playerPed = *(uintptr_t*)ADDR_PLAYER_PED;

    if (!playerPed || menuActive || cutsceneActive) return;

    DWORD now = GetTickCount();

    // Tecla V alterna o modo de câmara de forma fluida
    if ((GetAsyncKeyState(0x56) & 0x8000) && (now - g_LastKeyTime > 300)) {
        g_LastKeyTime = now;
        g_CamMode = (g_CamMode + 1) % 2; // Alterna entre 0 (Normal) e 1 (Isométrica)

        if (g_CamMode == 0) {
            CamRestoreWithJumpCut(pCamera);
            if (ShowScreenMessage) ShowScreenMessage("Camera: ~g~Padrao", 1500, 0, false);
        } else {
            if (ShowScreenMessage) ShowScreenMessage("Camera: ~y~Isometrica", 1500, 0, false);
        }
    }

    // Aplica a câmara isométrica apenas se o modo estiver ativo
    if (g_CamMode == 1) {
        CVector targetPos;
        if (GetEntityPosition(playerPed, targetPos)) {
            ApplyIsometricCamera((uintptr_t)pCamera, targetPos);
        }
    }
}

void InstallHook() {
    uintptr_t targetFunc = 0x50EA40; // Endereço seguro de CCamera::Process no GTA SA 1.0 US
    DWORD oldProtect;
    if (VirtualProtect((void*)targetFunc, 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        OriginalCameraProcess = (CameraProcess_t)targetFunc;
        uintptr_t relAddr = (uintptr_t)HookedCameraProcess - (targetFunc + 5);
        *(uint8_t*)targetFunc = 0xE9;
        *(uintptr_t*)(targetFunc + 1) = relAddr;
        VirtualProtect((void*)targetFunc, 5, oldProtect, &oldProtect);
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InstallHook();
    }
    return TRUE;
}
