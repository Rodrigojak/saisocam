#include <windows.h>
#include <cmath>
#include <cstdint>

struct CVector {
    float x, y, z;
};

#define ADDR_PLAYER_PED         0xB6F5F0
#define ADDR_MENU_ACTIVE        0xBA67A4
#define ADDR_WIDESCREEN_ON      0xB6F065
#define ADDR_THE_CAMERA         0xB6F028

typedef void(__thiscall* RestoreWithJumpCut_t)(void* pCamera);
typedef void(__cdecl* AddMessageJumpQ_t)(const char* text, unsigned int time, unsigned short flag, bool bPreviousBrief);

RestoreWithJumpCut_t CamRestoreWithJumpCut = (RestoreWithJumpCut_t)0x50BAB0;
AddMessageJumpQ_t ShowScreenMessage = (AddMessageJumpQ_t)0x69F1E0;

// Já começa ativado por defeito (true)
bool g_ModEnabled = true;
int g_ZoomLevel = 1;
int g_CurrentSide = 0;
const float g_ZoomDistances[3] = { 9.5f, 14.0f, 20.5f };
const float g_SideMultipliers[4][2] = {
    { -1.0f, -1.0f },
    {  1.0f, -1.0f },
    {  1.0f,  1.0f },
    { -1.0f,  1.0f }
};

float g_SmoothZoom = 14.0f;
float g_SmoothX = -1.0f;
float g_SmoothY = -1.0f;
CVector g_SmoothFocus = { 0.0f, 0.0f, 0.0f };
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

void ApplyIsometricCamera(const CVector& camPos, const CVector& lookAt) {
    uint8_t activeCamIndex = *(uint8_t*)(ADDR_THE_CAMERA + 0x59);
    uintptr_t activeCam = ADDR_THE_CAMERA + 0x174 + (activeCamIndex * 0x238);

    CVector front = { lookAt.x - camPos.x, lookAt.y - camPos.y, lookAt.z - camPos.z };
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
    if (OriginalCameraProcess) {
        OriginalCameraProcess(pCamera);
    }

    bool menuActive = *(bool*)ADDR_MENU_ACTIVE;
    bool cutsceneActive = *(bool*)ADDR_WIDESCREEN_ON;
    uintptr_t playerPed = *(uintptr_t*)ADDR_PLAYER_PED;

    if (!playerPed || menuActive) return;

    if (cutsceneActive) {
        CamRestoreWithJumpCut((void*)ADDR_THE_CAMERA);
        return;
    }

    DWORD now = GetTickCount();

    // Tecla V - Alternar Zoom
    if ((GetAsyncKeyState(0x56) & 0x8000) && (now - g_LastKeyTime > 250)) {
        g_LastKeyTime = now;
        g_ZoomLevel = (g_ZoomLevel + 1) % 3;
        if (g_ZoomLevel == 0) ShowScreenMessage("Zoom: ~y~PROXIMA", 1000, 0, false);
        if (g_ZoomLevel == 1) ShowScreenMessage("Zoom: ~g~MEDIA", 1000, 0, false);
        if (g_ZoomLevel == 2) ShowScreenMessage("Zoom: ~b~DISTANTE", 1000, 0, false);
    }

    // Tecla Z - Rodar Horário
    if ((GetAsyncKeyState(0x5A) & 0x8000) && (now - g_LastKeyTime > 250)) {
        g_LastKeyTime = now;
        g_CurrentSide = (g_CurrentSide + 1) % 4;
    }

    // Tecla X - Rodar Anti-Horário
    if ((GetAsyncKeyState(0x58) & 0x8000) && (now - g_LastKeyTime > 250)) {
        g_LastKeyTime = now;
        g_CurrentSide = (g_CurrentSide + 3) % 4;
    }

    CVector targetPos;
    if (!GetEntityPosition(playerPed, targetPos)) return;

    float targetZoom = g_ZoomDistances[g_ZoomLevel];

    uintptr_t vehiclePtr = *(uintptr_t*)(playerPed + 0x58C);
    uint32_t pedFlags = *(uint32_t*)(playerPed + 0x46C);
    if ((pedFlags & 0x100) && vehiclePtr) {
        GetEntityPosition(vehiclePtr, targetPos);
        CVector moveSpeed = *(CVector*)(vehiclePtr + 0x44);
        float speed = std::sqrt(moveSpeed.x * moveSpeed.x + moveSpeed.y * moveSpeed.y) * 50.0f;
        targetZoom += 4.0f + (speed * 0.45f);
        if (targetZoom > 42.0f) targetZoom = 42.0f;
    }

    g_SmoothZoom += (targetZoom - g_SmoothZoom) * 0.08f;
    g_SmoothX += (g_SideMultipliers[g_CurrentSide][0] - g_SmoothX) * 0.12f;
    g_SmoothY += (g_SideMultipliers[g_CurrentSide][1] - g_SmoothY) * 0.12f;

    g_SmoothFocus.x += (targetPos.x - g_SmoothFocus.x) * 0.15f;
    g_SmoothFocus.y += (targetPos.y - g_SmoothFocus.y) * 0.15f;
    g_SmoothFocus.z += (targetPos.z - g_SmoothFocus.z) * 0.15f;

    CVector camPos = {
        g_SmoothFocus.x + (g_SmoothZoom * g_SmoothX),
        g_SmoothFocus.y + (g_SmoothZoom * g_SmoothY),
        g_SmoothFocus.z + (g_SmoothZoom * 1.25f)
    };

    ApplyIsometricCamera(camPos, g_SmoothFocus);
}

void InstallHook() {
    uintptr_t targetFunc = 0x50EA40;
    DWORD oldProtect;
    VirtualProtect((void*)targetFunc, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    
    OriginalCameraProcess = (CameraProcess_t)targetFunc;
    uintptr_t relAddr = (uintptr_t)HookedCameraProcess - (targetFunc + 5);
    *(uint8_t*)targetFunc = 0xE9;
    *(uintptr_t*)(targetFunc + 1) = relAddr;
    
    VirtualProtect((void*)targetFunc, 5, oldProtect, &oldProtect);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InstallHook();
    }
    return TRUE;
}
