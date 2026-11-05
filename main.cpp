/**
 * ============================================================================
 * Mod: Isometric Camera Mod for GTA San Andreas (v1.0 US Hoodlum / Compact)
 * File: main.cpp / IsometricCameraSA.cpp
 * Target: .asi (Dynamic Link Library - x86 32-bit)
 * ============================================================================
 */

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shlwapi.lib")

namespace IsometricCamMod {

    static const float PI = 3.14159265358979323846f;
    static const float DEG2RAD = PI / 180.0f;

    template <typename T>
    static inline T ClampVal(T val, T minVal, T maxVal) {
        if (val < minVal) return minVal;
        if (val > maxVal) return maxVal;
        return val;
    }

    struct CVector {
        float x, y, z;
        CVector() : x(0.0f), y(0.0f), z(0.0f) {}
        CVector(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

        CVector operator+(const CVector& rhs) const { return CVector(x + rhs.x, y + rhs.y, z + rhs.z); }
        CVector operator-(const CVector& rhs) const { return CVector(x - rhs.x, y - rhs.y, z - rhs.z); }
        CVector operator*(float s) const { return CVector(x * s, y * s, z * s); }

        float Magnitude() const { return std::sqrt(x * x + y * y + z * z); }

        void Normalise() {
            float mag = Magnitude();
            if (mag > 0.0001f) {
                x /= mag;
                y /= mag;
                z /= mag;
            }
        }
    };

    static inline CVector CrossProduct(const CVector& a, const CVector& b) {
        return CVector(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        );
    }

    struct CRGBA {
        uint8_t r, g, b, a;
        CRGBA(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _a = 255) : r(_r), g(_g), b(_b), a(_a) {}
    };

    #pragma pack(push, 1)
    struct CMatrix {
        CVector right;
        uint32_t flags;
        CVector up;
        uint32_t pad1;
        CVector at;
        uint32_t pad2;
        CVector pos;
        uint32_t pad3;
        void* m_pAttachMatrix;
        uint8_t m_bOwnsAttachedMatrix;
        uint8_t pad4[3];

        void UpdateRW() {
            typedef void(__thiscall* fnUpdateRW)(CMatrix*);
            reinterpret_cast<fnUpdateRW>(0x59BBB0)(this);
        }
    };

    struct CPlaceable {
        void* vtable;
        CVector m_placementPos;
        float m_fHeading;
        CMatrix* m_matrix;

        CVector GetPosition() const {
            if (m_matrix) return m_matrix->pos;
            return m_placementPos;
        }
    };

    struct CCam {
        uint8_t pad0[0x0C];
        uint16_t m_nMode;
        uint8_t pad1[0xEC - 0x0E];
        float m_fFOV;
        uint8_t pad2[0x128 - 0xF0];
        CVector m_vecFront;
        CVector m_vecSource;
        CVector m_vecSourceBeforeLookBehind;
        CVector m_vecUp;
        uint8_t pad3[0x164 - 0x158];
        CVector m_vecTargetCoorsForFudgeInter;
        uint8_t pad4[0x238 - 0x170];
    };
    #pragma pack(pop)

    namespace GameSA {
        static inline uintptr_t TheCameraAddr() { return 0xB6F028; }

        static inline CPlaceable* GetCameraPlaceable() {
            return reinterpret_cast<CPlaceable*>(TheCameraAddr());
        }

        static inline uint8_t GetActiveCamIndex() {
            return *reinterpret_cast<uint8_t*>(TheCameraAddr() + 0x59);
        }

        static inline CCam& GetActiveCam() {
            CCam* cams = reinterpret_cast<CCam*>(TheCameraAddr() + 0x174);
            uint8_t idx = GetActiveCamIndex();
            if (idx > 2) idx = 0;
            return cams[idx];
        }

        static inline void SetCameraOrientation(float rad) {
            *reinterpret_cast<float*>(TheCameraAddr() + 0xCE0) = rad;
        }

        static inline void RestoreCameraWithJumpCut() {
            typedef void(__thiscall* fnRestoreWithJumpCut)(void*);
            reinterpret_cast<fnRestoreWithJumpCut>(0x50BAB0)(reinterpret_cast<void*>(TheCameraAddr()));
        }

        static inline void SetRwCamPosition() {
            typedef void(__thiscall* fnSetRwCamPosition)(void*);
            reinterpret_cast<fnSetRwCamPosition>(0x50AFA0)(reinterpret_cast<void*>(TheCameraAddr()));
        }

        static inline void CalculateDerivedValues(bool bForMirror, bool bOriented) {
            typedef void(__thiscall* fnCalcDerived)(void*, bool, bool);
            reinterpret_cast<fnCalcDerived>(0x514D60)(reinterpret_cast<void*>(TheCameraAddr()), bForMirror, bOriented);
        }

        static inline float GetTimeStep() { return *reinterpret_cast<float*>(0xB7CB5C); }
        static inline bool IsUserPaused() { return *reinterpret_cast<bool*>(0xB7CB49); }
        static inline bool IsCodePaused() { return *reinterpret_cast<bool*>(0xB7CB48); }
        static inline bool IsCutsceneRunning() { return *reinterpret_cast<bool*>(0xB5F851); }
        static inline bool IsMenuActive() { return *reinterpret_cast<bool*>(0xBA6748 + 0x32); }

        static inline CPlaceable* FindPlayerPed(int playerId = 0) {
            typedef CPlaceable*(__cdecl* fnFindPlayerPed)(int);
            return reinterpret_cast<fnFindPlayerPed>(0x56E210)(playerId);
        }

        static inline CPlaceable* FindPlayerVehicle(int playerId = 0, bool bIncludeRemote = false) {
            typedef CPlaceable*(__cdecl* fnFindPlayerVehicle)(int, bool);
            return reinterpret_cast<fnFindPlayerVehicle>(0x56E0D0)(playerId, bIncludeRemote);
        }

        static inline void AddHudMessage(const char* text, uint32_t timeMs = 1800) {
            typedef void(__cdecl* fnAddMessageJumpQ)(const char*, uint32_t, uint16_t, bool);
            reinterpret_cast<fnAddMessageJumpQ>(0x69F1E0)(text, timeMs, 0, false);
        }
    }

    static const float ISO_DEFAULT_PITCH_DEG = 38.0f;

    enum class IsoQuadrant : int {
        NORTH_EAST = 0,
        NORTH_WEST = 1,
        SOUTH_WEST = 2,
        SOUTH_EAST = 3
    };

    struct ModConfig {
        bool bIsometricEnabled = false;
        IsoQuadrant eCurrentQuadrant = IsoQuadrant::NORTH_EAST;
        float fViewDistance = 22.0f;
        float fMinDistance = 10.0f;
        float fMaxDistance = 45.0f;
        float fPositionSmoothSpeed = 7.5f;
        float fFOV = 55.0f;
    };

    static ModConfig g_Config;
    static FILE* g_LogFile = nullptr;
    static HMODULE g_hThisModule = nullptr;

    static CVector g_CurrentCamPos(0.0f, 0.0f, 0.0f);
    static CVector g_CurrentLookAt(0.0f, 0.0f, 0.0f);
    static float g_CurrentYawRad = 45.0f * DEG2RAD;
    static float g_CurrentPitchRad = ISO_DEFAULT_PITCH_DEG * DEG2RAD;
    static float g_CurrentDist = 22.0f;
    static bool g_bCamInitialized = false;

    class Logger {
    public:
        static void Init(HMODULE hModule) {
            char modPath[MAX_PATH] = { 0 };
            GetModuleFileNameA(hModule, modPath, MAX_PATH);
            PathRemoveFileSpecA(modPath);
            std::string logFilePath = std::string(modPath) + "\\IsometricCameraSA.log";
            g_LogFile = fopen(logFilePath.c_str(), "w");
            if (g_LogFile) {
                fprintf(g_LogFile, "[INFO] IsometricCameraSA inicializado com sucesso.\n");
                fflush(g_LogFile);
            }
        }
        static void Shutdown() {
            if (g_LogFile) {
                fclose(g_LogFile);
                g_LogFile = nullptr;
            }
        }
        static void Info(const char* fmt, ...) {
            if (!g_LogFile) return;
            va_list args;
            va_start(args, fmt);
            vfprintf(g_LogFile, fmt, args);
            fprintf(g_LogFile, "\n");
            fflush(g_LogFile);
            va_end(args);
        }
    };

    static float NormalizeAngleRad(float angle) {
        while (angle > PI) angle -= 2.0f * PI;
        while (angle < -PI) angle += 2.0f * PI;
        return angle;
    }

    static float LerpAngleRad(float current, float target, float factor) {
        float diff = NormalizeAngleRad(target - current);
        return NormalizeAngleRad(current + diff * ClampVal(factor, 0.0f, 1.0f));
    }

    static CVector SmoothLerpVector(const CVector& current, const CVector& target, float speed, float dtSeconds) {
        float alpha = 1.0f - std::exp(-speed * dtSeconds);
        return CVector(
            current.x + (target.x - current.x) * alpha,
            current.y + (target.y - current.y) * alpha,
            current.z + (target.z - current.z) * alpha
        );
    }

    static void HandleInput() {
        if (GameSA::IsMenuActive()) return;

        static bool bPrevKeyState = false;
        bool bCurrKeyState = (GetAsyncKeyState('Z') & 0x8000) != 0;

        if (bCurrKeyState && !bPrevKeyState) {
            g_Config.bIsometricEnabled = !g_Config.bIsometricEnabled;
            g_bCamInitialized = false;

            if (g_Config.bIsometricEnabled) {
                GameSA::AddHudMessage("~g~Camera Isometrica Ativada (~y~Tecla Z~g~)", 2000);
                Logger::Info("Camera isometrica ativada pelo usuario.");
            } else {
                GameSA::RestoreCameraWithJumpCut();
                GameSA::AddHudMessage("~w~Camera Padrao Restaurada", 1500);
                Logger::Info("Camera padrao restaurada.");
            }
        }
        bPrevKeyState = bCurrKeyState;
    }

    static void ProcessIsometricCamera() {
        HandleInput();
        if (!g_Config.bIsometricEnabled) return;

        CPlaceable* pPlayer = GameSA::FindPlayerPed(0);
        if (!pPlayer) return;

        CPlaceable* pVehicle = GameSA::FindPlayerVehicle(0, false);
        float dtSeconds = ClampVal(GameSA::GetTimeStep() / 50.0f, 0.001f, 0.1f);
        bool bInVehicle = (pVehicle != nullptr);

        CPlaceable* pFocusEntity = bInVehicle ? pVehicle : pPlayer;
        CVector targetPos = pFocusEntity->GetPosition();
        targetPos.z += bInVehicle ? 0.85f : 0.65f;

        float targetYawRad = 45.0f * DEG2RAD + (static_cast<int>(g_Config.eCurrentQuadrant) * 90.0f * DEG2RAD);
        float targetPitchRad = ISO_DEFAULT_PITCH_DEG * DEG2RAD;

        if (!g_bCamInitialized) {
            g_CurrentCamPos = GameSA::GetCameraPlaceable()->GetPosition();
            g_CurrentLookAt = targetPos;
            g_CurrentYawRad = targetYawRad;
            g_CurrentPitchRad = targetPitchRad;
            g_CurrentDist = g_Config.fViewDistance;
            g_bCamInitialized = true;
        }

        g_CurrentYawRad = LerpAngleRad(g_CurrentYawRad, targetYawRad, 0.1f);
        g_CurrentPitchRad = LerpAngleRad(g_CurrentPitchRad, targetPitchRad, 0.1f);

        float horizontalRadius = g_CurrentDist * std::cos(g_CurrentPitchRad);
        float verticalHeight = g_CurrentDist * std::sin(g_CurrentPitchRad);

        CVector idealCamOffset(
            std::sin(g_CurrentYawRad) * horizontalRadius,
            -std::cos(g_CurrentYawRad) * horizontalRadius,
            verticalHeight
        );

        CVector idealCamPos = targetPos + idealCamOffset;
        g_CurrentCamPos = SmoothLerpVector(g_CurrentCamPos, idealCamPos, g_Config.fPositionSmoothSpeed, dtSeconds);
        g_CurrentLookAt = SmoothLerpVector(g_CurrentLookAt, targetPos, g_Config.fPositionSmoothSpeed * 1.4f, dtSeconds);

        CVector vecForward = g_CurrentLookAt - g_CurrentCamPos;
        vecForward.Normalise();

        CVector worldUp(0.0f, 0.0f, 1.0f);
        CVector vecRight = CrossProduct(worldUp, vecForward);
        vecRight.Normalise();

        CVector vecUp = CrossProduct(vecForward, vecRight);
        vecUp.Normalise();

        CCam& activeCam = GameSA::GetActiveCam();
        activeCam.m_vecSource = g_CurrentCamPos;
        activeCam.m_vecFront = vecForward;
        activeCam.m_vecUp = vecUp;
        activeCam.m_vecTargetCoorsForFudgeInter = g_CurrentLookAt;
        activeCam.m_fFOV = g_Config.fFOV;

        CPlaceable* pCamPlaceable = GameSA::GetCameraPlaceable();
        if (pCamPlaceable && pCamPlaceable->m_matrix) {
            pCamPlaceable->m_matrix->right = vecRight;
            pCamPlaceable->m_matrix->up = vecForward;
            pCamPlaceable->m_matrix->at = vecUp;
            pCamPlaceable->m_matrix->pos = g_CurrentCamPos;
            pCamPlaceable->m_matrix->UpdateRW();
        }
        GameSA::SetRwCamPosition();
        GameSA::CalculateDerivedValues(false, true);
        GameSA::SetCameraOrientation(g_CurrentYawRad);
    }

    static DWORD WINAPI CameraUpdateLoop(LPVOID /*lpParam*/) {
        Sleep(2500);
        Logger::Init(g_hThisModule);
        Logger::Info("Loop de atualizacao de camera estavel iniciado.");

        while (true) {
            if (!GameSA::IsUserPaused() && !GameSA::IsCodePaused() && !GameSA::IsCutsceneRunning()) {
                ProcessIsometricCamera();
            }
            Sleep(10);
        }
        return 0;
    }

} // namespace IsometricCamMod

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        IsometricCamMod::g_hThisModule = hModule;
        CreateThread(nullptr, 0, IsometricCamMod::CameraUpdateLoop, nullptr, 0, nullptr);
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        IsometricCamMod::Logger::Shutdown();
    }
    return TRUE;
}
