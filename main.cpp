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

    struct CColPoint {
        CVector m_vecPoint;
        float field_C;
        CVector m_vecNormal;
        float field_1C;
        uint8_t m_nSurfaceTypeA;
        uint8_t m_nPieceTypeA;
        uint8_t m_nLightingA;
        uint8_t padA;
        uint8_t m_nSurfaceTypeB;
        uint8_t m_nPieceTypeB;
        uint8_t m_nLightingB;
        uint8_t padB;
        float m_fDepth;
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

    enum eCamMode : uint16_t {
        MODE_SNIPER = 7,
        MODE_ROCKETLAUNCHER = 8,
        MODE_ROCKETLAUNCHER_HS = 51,
        MODE_CAMERA = 46,
        MODE_AIMWEAPON = 53,
        MODE_AIMWEAPON_FROMCAR = 55,
        MODE_AIMWEAPON_ATTACHED = 65
    };

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

        static inline bool IsWidescreenOn() {
            return *reinterpret_cast<bool*>(TheCameraAddr() + 0x75);
        }

        static inline uint16_t GetPedZoomIndicator() {
            return *reinterpret_cast<uint16_t*>(TheCameraAddr() + 0xF0);
        }

        static inline uint16_t GetCarZoomIndicator() {
            return *reinterpret_cast<uint16_t*>(TheCameraAddr() + 0xEE);
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
        static inline int GetCurrentInteriorArea() { return *reinterpret_cast<int*>(0xB72914); }

        static inline bool IsMenuActive() { return *reinterpret_cast<bool*>(0xBA6748 + 0x32); }
        static inline uint8_t GetCurrentMenuPage() { return *reinterpret_cast<uint8_t*>(0xBA6748 + 0x15D); }
        static inline int GetMenuMousePosY() { return *reinterpret_cast<int*>(0xBA6748 + 0x3C); }

        static inline int GetScreenWidth() { return *reinterpret_cast<int*>(0xC17044); }
        static inline int GetScreenHeight() { return *reinterpret_cast<int*>(0xC17048); }

        static inline CPlaceable* FindPlayerPed(int playerId = 0) {
            typedef CPlaceable*(__cdecl* fnFindPlayerPed)(int);
            return reinterpret_cast<fnFindPlayerPed>(0x56E210)(playerId);
        }

        static inline CPlaceable* FindPlayerVehicle(int playerId = 0, bool bIncludeRemote = false) {
            typedef CPlaceable*(__cdecl* fnFindPlayerVehicle)(int, bool);
            return reinterpret_cast<fnFindPlayerVehicle>(0x56E0D0)(playerId, bIncludeRemote);
        }

        static inline CVector GetEntityMoveSpeed(CPlaceable* pEntity) {
            if (!pEntity) return CVector(0.0f, 0.0f, 0.0f);
            return *reinterpret_cast<CVector*>(reinterpret_cast<uintptr_t>(pEntity) + 0x44);
        }

        static inline int GetEntityAreaCode(CPlaceable* pEntity) {
            if (!pEntity) return 0;
            return *reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(pEntity) + 0x2F);
        }

        static inline void* GetPad(int padNumber = 0) {
            typedef void*(__cdecl* fnGetPad)(int);
            return reinterpret_cast<fnGetPad>(0x53FB70)(padNumber);
        }

        static inline bool CycleCameraModeDownJustDown() {
            void* pad = GetPad(0);
            if (!pad) return false;
            typedef bool(__thiscall* fnCycleCam)(void*);
            return reinterpret_cast<fnCycleCam>(0x5404E0)(pad);
        }

        static inline bool IsChangeViewButtonHeld() {
            void* pad = GetPad(0);
            if (!pad) return false;
            int16_t changeViewState = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(pad) + 0x1A);
            return changeViewState != 0;
        }

        static inline bool ArePlayerControlsDisabled() {
            void* pad = GetPad(0);
            if (!pad) return false;
            uint16_t disableFlags = *reinterpret_cast<uint16_t*>(reinterpret_cast<uintptr_t>(pad) + 0x10E);
            return disableFlags != 0;
        }

        static inline bool ProcessLineOfSight(
            const CVector& origin, const CVector& target, CColPoint& outColPoint, void*& outEntity,
            bool buildings, bool vehicles, bool peds, bool objects, bool dummies,
            bool doSeeThrough, bool cameraIgnore, bool shootThrough)
        {
            typedef bool(__cdecl* fnProcessLOS)(
                const CVector&, const CVector&, CColPoint&, void*&,
                bool, bool, bool, bool, bool, bool, bool, bool);
            return reinterpret_cast<fnProcessLOS>(0x56A490)(
                origin, target, outColPoint, outEntity,
                buildings, vehicles, peds, objects, dummies,
                doSeeThrough, cameraIgnore, shootThrough);
        }

        static inline void AddHudMessage(const char* text, uint32_t timeMs = 1800) {
            typedef void(__cdecl* fnAddMessageJumpQ)(const char*, uint32_t, uint16_t, bool);
            reinterpret_cast<fnAddMessageJumpQ>(0x69F1E0)(text, timeMs, 0, false);
        }

        static inline void FontSetScale(float w, float h) {
            typedef void(__cdecl* fn)(float, float);
            reinterpret_cast<fn>(0x719380)(w, h);
        }
        static inline void FontSetColor(CRGBA color) {
            typedef void(__cdecl* fn)(CRGBA);
            reinterpret_cast<fn>(0x719430)(color);
        }
        static inline void FontSetOrientation(uint8_t align) {
            typedef void(__cdecl* fn)(uint8_t);
            reinterpret_cast<fn>(0x719610)(align);
        }
        static inline void FontSetFontStyle(uint8_t style) {
            typedef void(__cdecl* fn)(uint8_t);
            reinterpret_cast<fn>(0x719490)(style);
        }
        static inline void FontSetProportional(bool on) {
            typedef void(__cdecl* fn)(bool);
            reinterpret_cast<fn>(0x7195B0)(on);
        }
        static inline void FontSetBackground(bool enable, bool includeWrap) {
            typedef void(__cdecl* fn)(bool, bool);
            reinterpret_cast<fn>(0x7195C0)(enable, includeWrap);
        }
        static inline void FontSetDropShadowPosition(int16_t pos) {
            typedef void(__cdecl* fn)(int16_t);
            reinterpret_cast<fn>(0x719570)(pos);
        }
        static inline void FontSetDropColor(CRGBA color) {
            typedef void(__cdecl* fn)(CRGBA);
            reinterpret_cast<fn>(0x719510)(color);
        }
        static inline void FontSetWrapx(float wrapX) {
            typedef void(__cdecl* fn)(float);
            reinterpret_cast<fn>(0x7194D0)(wrapX);
        }
        static inline void FontPrintString(float x, float y, const char* text) {
            typedef void(__cdecl* fn)(float, float, const char*);
            reinterpret_cast<fn>(0x71A700)(x, y, text);
        }
    }

    static const float ISO_DEFAULT_PITCH_DEG = 38.0f;
    static const float ISO_INTERIOR_PITCH_DEG = 52.0f;
    static const uint8_t MENUPAGE_CONTROLLER_PC_ID = 37;

    enum class VehicleIsoMode : int {
        FIXED_PERSPECTIVE = 0,
        FLEXIBLE_BEHIND_CAR = 1
    };

    enum class IsoQuadrant : int {
        NORTH_EAST = 0,
        NORTH_WEST = 1,
        SOUTH_WEST = 2,
        SOUTH_EAST = 3
    };

    struct ModConfig {
        bool bIsometricEnabled = false;
        VehicleIsoMode eVehicleMode = VehicleIsoMode::FLEXIBLE_BEHIND_CAR;
        IsoQuadrant eCurrentQuadrant = IsoQuadrant::NORTH_EAST;

        float fViewDistance = 22.0f;
        float fMinDistance = 10.0f;
        float fMaxDistance = 45.0f;
        float fInteriorDistanceScale = 0.48f;

        float fPositionSmoothSpeed = 7.5f;
        float fRotationSmoothSpeed = 6.0f;
        float fCollisionPullInSpeed = 18.0f;
        float fFOV = 55.0f;

        int vKeyModifier = VK_MENU;
        int vKeyRotateLeft = 'Q';
        int vKeyRotateRight = 'E';
    };

    static ModConfig g_Config;
    static FILE* g_LogFile = nullptr;
    static PVOID g_VehHandle = nullptr;
    static HMODULE g_hThisModule = nullptr;

    static CVector g_CurrentCamPos(0.0f, 0.0f, 0.0f);
    static CVector g_CurrentLookAt(0.0f, 0.0f, 0.0f);
    static float g_CurrentYawRad = 45.0f * DEG2RAD;
    static float g_CurrentPitchRad = ISO_DEFAULT_PITCH_DEG * DEG2RAD;
    static float g_CurrentDist = 22.0f;
    static bool g_bCamInitialized = false;
    static int g_MenuSelectedCustomRow = -1;

    class Logger {
    public:
        static void Init(HMODULE hModule) {
            char modPath[MAX_PATH] = { 0 };
            GetModuleFileNameA(hModule, modPath, MAX_PATH);
            PathRemoveFileSpecA(modPath);
            std::string logFilePath = std::string(modPath) + "\\IsometricCameraSA.log";
            g_LogFile = fopen(logFilePath.c_str(), "w");
            if (g_LogFile) {
                fprintf(g_LogFile, "[INFO] IsometricCameraSA inicializado.\n");
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

    static LONG CALLBACK VectoredCrashHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_ACCESS_VIOLATION) {
            Logger::Info("[CRASH] Access Violation capturado.");
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    class ConfigManager {
    public:
        static void Load() {
            char path[MAX_PATH] = { 0 };
            GetModuleFileNameA(g_hThisModule, path, MAX_PATH);
            PathRenameExtensionA(path, ".ini");
            g_Config.eVehicleMode = static_cast<VehicleIsoMode>(GetPrivateProfileIntA("IsometricCamera", "VehicleCameraMode", 1, path));
            char distBuf[32] = { 0 };
            GetPrivateProfileStringA("IsometricCamera", "ViewDistance", "22.0", distBuf, sizeof(distBuf), path);
            g_Config.fViewDistance = ClampVal(static_cast<float>(atof(distBuf)), g_Config.fMinDistance, g_Config.fMaxDistance);
            g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>(GetPrivateProfileIntA("IsometricCamera", "DefaultQuadrant", 0, path) % 4);
        }
        static void Save() {
            char path[MAX_PATH] = { 0 };
            GetModuleFileNameA(g_hThisModule, path, MAX_PATH);
            PathRenameExtensionA(path, ".ini");
            char buf[64];
            snprintf(buf, sizeof(buf), "%d", static_cast<int>(g_Config.eVehicleMode));
            WritePrivateProfileStringA("IsometricCamera", "VehicleCameraMode", buf, path);
            snprintf(buf, sizeof(buf), "%.2f", g_Config.fViewDistance);
            WritePrivateProfileStringA("IsometricCamera", "ViewDistance", buf, path);
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

    static const char* GetQuadrantName(IsoQuadrant q) {
        switch (q) {
            case IsoQuadrant::NORTH_EAST: return "Nordeste (45 deg)";
            case IsoQuadrant::NORTH_WEST: return "Noroeste (135 deg)";
            case IsoQuadrant::SOUTH_WEST: return "Sudoeste (225 deg)";
            case IsoQuadrant::SOUTH_EAST: return "Sudeste (315 deg)";
            default: return "Nordeste";
        }
    }

    static void HandleCameraControlsAndCycling(CPlaceable* pPlayer, CPlaceable* pVehicle) {
        if (GameSA::IsMenuActive()) return;
        bool bInVehicle = (pVehicle != nullptr);

        if (GameSA::CycleCameraModeDownJustDown()) {
            if (g_Config.bIsometricEnabled) {
                g_Config.bIsometricEnabled = false;
                GameSA::RestoreCameraWithJumpCut();
                GameSA::AddHudMessage("~w~Camera Padrao Restaurada", 1200);
            } else {
                if (!bInVehicle && GameSA::GetPedZoomIndicator() >= 2) {
                    g_Config.bIsometricEnabled = true;
                    g_bCamInitialized = false;
                    GameSA::AddHudMessage("~g~Camera Isometrica Ativada", 2000);
                } else if (bInVehicle && GameSA::GetCarZoomIndicator() >= 5) {
                    g_Config.bIsometricEnabled = true;
                    g_bCamInitialized = false;
                    GameSA::AddHudMessage("~g~Camera Isometrica (Veiculo) Ativada", 2000);
                }
            }
        }
    }

    static void ProcessIsometricCamera() {
        CPlaceable* pPlayer = GameSA::FindPlayerPed(0);
        if (!pPlayer) return;

        CPlaceable* pVehicle = GameSA::FindPlayerVehicle(0, false);
        HandleCameraControlsAndCycling(pPlayer, pVehicle);

        if (!g_Config.bIsometricEnabled) return;

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

    // Abordagem Segura baseada em loop de thread dedicada por frame em vez de hook de chamada relativo
    static DWORD WINAPI CameraUpdateLoop(LPVOID /*lpParam*/) {
        Sleep(2000); // Aguarda o jogo carregar completamente
        Logger::Init(g_hThisModule);
        ConfigManager::Load();
        Logger::Info("Loop de atualizacao de camera iniciado com sucesso.");

        while (true) {
            if (!GameSA::IsUserPaused() && !GameSA::IsCodePaused()) {
                ProcessIsometricCamera();
            }
            Sleep(10); // Intervalo seguro para sincronização com o framerate
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
        IsometricCamMod::ConfigManager::Save();
        IsometricCamMod::Logger::Shutdown();
    }
    return TRUE;
}
