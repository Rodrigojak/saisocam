#include <windows.h>
#include <cmath>

// Estrutura de vetor 3D do GTA SA
struct CVector {
    float x, y, z;
};

// Endereços nativos do GTA: San Andreas v1.0 US
#define ADDR_PLAYER_PED         0xB6F5F0 // Ponteiro direto para o CPed do jogador (CJ)
#define ADDR_MENU_ACTIVE        0xBA67A4 // bool: Menu de pausa ativo
#define ADDR_WIDESCREEN_ON      0xB6F065 // bool: Cutscene / bordas pretas ativas
#define ADDR_THE_CAMERA         0xB6F028 // Objeto global TheCamera

// Funções nativas do motor do jogo (GTA SA 1.0 US)
typedef void(__thiscall* RestoreWithJumpCut_t)(void* pCamera);
typedef void(__cdecl* AddMessageJumpQ_t)(const char* text, unsigned int time, unsigned short flag, bool bPreviousBrief);

RestoreWithJumpCut_t CamRestoreWithJumpCut = (RestoreWithJumpCut_t)0x50BAB0;
AddMessageJumpQ_t ShowScreenMessage = (AddMessageJumpQ_t)0x69F1E0;

// Estado do Mod
bool g_ModEnabled = false;
int g_ZoomLevel = 1;       // 0: Próxima, 1: Média, 2: Distante
int g_CurrentSide = 0;     // 0 a 3 (4 ângulos de 90 graus)
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

// Lê a posição de uma entidade (CPed ou CVehicle) com verificação de ponteiro nulo
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

// Aplica a posição e direção diretamente na CCam ativa da TheCamera
void ApplyIsometricCamera(const CVector& camPos, const CVector& lookAt) {
    uint8_t activeCamIndex = *(uint8_t*)(ADDR_THE_CAMERA + 0x59);
    uintptr_t activeCam = ADDR_THE_CAMERA + 0x174 + (activeCamIndex * 0x238);

    // Calcula o vetor direção (Front) normalizado entre a câmara e o CJ
    CVector front = {
        lookAt.x - camPos.x,
        lookAt.y - camPos.y,
        lookAt.z - camPos.z
    };
    float len = std::sqrt(front.x * front.x + front.y * front.y + front.z * front.z);
    if (len > 0.0001f) {
        front.x /= len;
        front.y /= len;
        front.z /= len;
    }

    // Calcula um vetor Up perpendicular para manter o horizonte nivelado
    CVector worldUp = { 0.0f, 0.0f, 1.0f };
    CVector right = {
        front.y * worldUp.z - front.z * worldUp.y,
        front.z * worldUp.x - front.x * worldUp.z,
        front.x * worldUp.y - front.y * worldUp.x
    };
    float rLen = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rLen > 0.0001f) {
        right.x /= rLen; right.y /= rLen; right.z /= rLen;
    }
    CVector up = {
        right.y * front.z - right.z * front.y,
        right.z * front.x - right.x * front.z,
        right.x * front.y - right.y * front.x
    };

    // Escreve nos offsets da CCam ativa
    *(CVector*)(activeCam + 0x130) = front;  // m_vFront
    *(CVector*)(activeCam + 0x13C) = camPos; // m_vSource
    *(CVector*)(activeCam + 0x154) = up;     // m_vUp
}

// Hook no loop principal do jogo para atualizar a câmara sem conflitos de thread
typedef void(__cdecl* Idle_t)(void* data);
Idle_t OriginalIdle = nullptr;

void __cdecl HookedIdle(void* data) {
    // Executa toda a lógica original do frame do jogo primeiro
    OriginalIdle(data);

    bool menuActive = *(bool*)ADDR_MENU_ACTIVE;
    bool cutsceneActive = *(bool*)ADDR_WIDESCREEN_ON;
    uintptr_t playerPed = *(uintptr_t*)ADDR_PLAYER_PED;

    if (!playerPed || menuActive) return;

    DWORD now = GetTickCount();

    // Tecla I (0x49) - Ativar / Desativar
    if ((GetAsyncKeyState(0x49) & 0x8000) && (now - g_LastKeyTime > 250)) {
        g_LastKeyTime = now;
        g_ModEnabled = !g_ModEnabled;
        if (g_ModEnabled) {
            GetEntityPosition(playerPed, g_SmoothFocus);
            ShowScreenMessage((char*)"Camera Isometrica: ~g~ATIVADA", 1500, 0, false);
        } else {
            CamRestoreWithJumpCut((void*)ADDR_THE_CAMERA);
            ShowScreenMessage((char*)"Camera Isometrica: ~r~DESATIVADA", 1500, 0, false);
        }
    }

    if (!g_ModEnabled) return;

    if (cutsceneActive) {
        CamRestoreWithJumpCut((void*)ADDR_THE_CAMERA);
        return;
    }

    // Tecla V (0x56) - Alternar Zoom
    if ((GetAsyncKeyState(0x56) & 0x8000) && (now - g_LastKeyTime > 250)) {
        g_LastKeyTime = now;
        g_ZoomLevel = (g_ZoomLevel + 1) % 3;
        if (g_ZoomLevel == 0) ShowScreenMessage((char*)"Zoom: ~y~PROXIMA", 1000, 0, false);
        if (g_ZoomLevel == 1) ShowScreenMessage((char*)"Zoom: ~g~MEDIA", 1000, 0, false);
        if (g_ZoomLevel == 2) ShowScreenMessage((char*)"Zoom: ~b~DISTANTE", 1000, 0, false);
    }

    // Tecla Z (0x5A) - Rodar Horário
    if ((GetAsyncKeyState(0x5A) & 0x8000) && (now - g_LastKeyTime > 250)) {
        g_LastKeyTime = now;
        g_CurrentSide = (g_CurrentSide + 1) % 4;
    }

    // Tecla X (0x58) - Rodar Anti-Horário
    if ((GetAsyncKeyState(0x58) & 0x8000) && (now - g_LastKeyTime > 250)) {
        g_LastKeyTime = now;
        g_CurrentSide = (g_CurrentSide + 3) % 4;
    }

    CVector targetPos;
    if (!GetEntityPosition(playerPed, targetPos)) return;

    float targetZoom = g_ZoomDistances[g_ZoomLevel];

    // Verifica se está num veículo (offset 0x46C = bInVehicle flag / 0x58C = pVehicle)
    uintptr_t vehiclePtr = *(uintptr_t*)(playerPed + 0x58C);
    uint32_t pedFlags = *(uint32_t*)(playerPed + 0x46C);
    if ((pedFlags & 0x100) && vehiclePtr) {
        GetEntityPosition(vehiclePtr, targetPos);
        CVector moveSpeed = *(CVector*)(vehiclePtr + 0x44);
        float speed = std::sqrt(moveSpeed.x * moveSpeed.x + moveSpeed.y * moveSpeed.y) * 50.0f;
        targetZoom += 4.0f + (speed * 0.45f);
        if (targetZoom > 42.0f) targetZoom = 42.0f;
    }

    // Interpolação suave (Lerp)
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

// Instala o hook na chamada de renderização/loop principal (0x53ECCB)
void InstallHook() {
    uintptr_t callAddr = 0x53E981; // Chamada dentro de AppEventHandler para Idle (0x53E920)
    DWORD oldProtect;
    VirtualProtect((void*)callAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
    OriginalIdle = (Idle_t)0x53E920;
    uintptr_t relAddr = (uintptr_t)HookedIdle - (callAddr + 5);
    *(uint8_t*)callAddr = 0xE8; // CALL
    *(uintptr_t*)(callAddr + 1) = relAddr;
    VirtualProtect((void*)callAddr, 5, oldProtect, &oldProtect);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        InstallHook();
    }
    return TRUE;
}