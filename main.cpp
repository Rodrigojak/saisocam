/**
 * ============================================================================
 * Mod: Isometric Camera Mod for GTA San Andreas (v1.0 US Hoodlum / Compact)
 * File: main.cpp / IsometricCameraSA.cpp
 * Target: .asi (Dynamic Link Library - x86 32-bit)
 * Build Command (GitHub Actions / MSVC x86):
 *   cl /EHsc /LD /O2 main.cpp user32.lib psapi.lib shlwapi.lib /link /OUT:IsoCam.asi
 * ============================================================================
 *
 * VERSÃO STANDALONE (ZERO DEPENDÊNCIAS EXTERNAS):
 * Não requer download externo do plugin-sdk ("plugin.h"). Todos os endereços de
 * memória, estruturas e ponteiros nativos do GTA San Andreas 1.0 US estão
 * mapeados diretamente neste arquivo para compilar direto no GitHub Actions.
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
        int vKeyZoomIn = VK_OEM_PLUS;
        int vKeyZoomOut = VK_OEM_MINUS;
    };

    struct LoadedAsiInfo {
        std::string fileName;
        std::string fullPath;
        uintptr_t baseAddress;
        DWORD imageSize;
        bool bPotentialCameraConflict;
        std::string conflictReason;
    };

    static ModConfig g_Config;
    static std::vector<LoadedAsiInfo> g_LoadedAsiPlugins;
    static FILE* g_LogFile = nullptr;
    static PVOID g_VehHandle = nullptr;
    static HMODULE g_hThisModule = nullptr;

    static CVector g_CurrentCamPos(0.0f, 0.0f, 0.0f);
    static CVector g_CurrentLookAt(0.0f, 0.0f, 0.0f);
    static float g_CurrentYawRad = 45.0f * DEG2RAD;
    static float g_CurrentPitchRad = ISO_DEFAULT_PITCH_DEG * DEG2RAD;
    static float g_CurrentDist = 22.0f;
    static bool g_bCamInitialized = false;
    static bool g_bWasInCutsceneOrScriptCam = false;
    static bool g_bWasInInterior = false;
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
                Info("======================================================================");
                Info(" IsometricCameraSA.asi - Inicializando Log de Diagnostico");
                Info(" Data/Hora de Inicio: %s", GetTimestamp().c_str());
                Info(" Diretorio Base: %s", modPath);
                Info("======================================================================");
            }
        }

        static void Shutdown() {
            if (g_LogFile) {
                Info("======================================================================");
                Info(" Encerrando IsometricCameraSA.asi normalmente.");
                Info("======================================================================");
                fclose(g_LogFile);
                g_LogFile = nullptr;
            }
        }

        static void Info(const char* fmt, ...) {
            va_list args;
            va_start(args, fmt);
            WriteLog("INFO", fmt, args);
            va_end(args);
        }

        static void Warn(const char* fmt, ...) {
            va_list args;
            va_start(args, fmt);
            WriteLog("WARN", fmt, args);
            va_end(args);
        }

        static void Error(const char* fmt, ...) {
            va_list args;
            va_start(args, fmt);
            WriteLog("ERROR", fmt, args);
            va_end(args);
        }

    private:
        static std::string GetTimestamp() {
            time_t now = time(nullptr);
            struct tm tstruct;
            char buf[64];
            localtime_s(&tstruct, &now);
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tstruct);
            return std::string(buf);
        }

        static void WriteLog(const char* level, const char* fmt, va_list args) {
            if (!g_LogFile) return;
            fprintf(g_LogFile, "[%s] [%s] ", GetTimestamp().c_str(), level);
            vfprintf(g_LogFile, fmt, args);
            fprintf(g_LogFile, "\n");
            fflush(g_LogFile);
        }
    };

    static LONG CALLBACK VectoredCrashHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
        if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
            code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW) {
            uintptr_t faultAddr = reinterpret_cast<uintptr_t>(pExceptionInfo->ExceptionRecord->ExceptionAddress);
            HMODULE hFaultMod = nullptr;
            char modName[MAX_PATH] = "Desconhecido";
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(faultAddr), &hFaultMod)) {
                GetModuleFileNameA(hFaultMod, modName, MAX_PATH);
            }
            Logger::Error("CRITICAL EXCEPTION! Codigo: 0x%08X no endereco: 0x%08X", code, faultAddr);
            Logger::Error("Modulo causador: %s", PathFindFileNameA(modName));
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    class AsiScanner {
    public:
        static void ScanLoadedModules() {
            g_LoadedAsiPlugins.clear();
            HANDLE hProcess = GetCurrentProcess();
            HMODULE hMods[1024];
            DWORD cbNeeded = 0;
            if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
                size_t numModules = cbNeeded / sizeof(HMODULE);
                for (size_t i = 0; i < numModules; ++i) {
                    char szModPath[MAX_PATH] = { 0 };
                    if (GetModuleFileNameExA(hProcess, hMods[i], szModPath, sizeof(szModPath))) {
                        const char* ext = PathFindExtensionA(szModPath);
                        if (ext && (_stricmp(ext, ".asi") == 0 || _stricmp(ext, ".dll") == 0)) {
                            const char* fileName = PathFindFileNameA(szModPath);
                            if (_stricmp(ext, ".asi") == 0 || _stricmp(fileName, "vorbisFile.dll") == 0) {
                                MODULEINFO modInfo = { 0 };
                                GetModuleInformation(hProcess, hMods[i], &modInfo, sizeof(modInfo));
                                LoadedAsiInfo info;
                                info.fileName = fileName;
                                info.fullPath = szModPath;
                                info.baseAddress = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
                                info.imageSize = modInfo.SizeOfImage;
                                info.bPotentialCameraConflict = (std::string(fileName).find("firstperson") != std::string::npos);
                                g_LoadedAsiPlugins.push_back(info);
                            }
                        }
                    }
                }
            }
        }
    };

    class ConfigManager {
    public:
        static std::string GetIniPath() {
            char path[MAX_PATH] = { 0 };
            GetModuleFileNameA(g_hThisModule, path, MAX_PATH);
            PathRenameExtensionA(path, ".ini");
            return std::string(path);
        }

        static void Load() {
            std::string ini = GetIniPath();
            g_Config.eVehicleMode = static_cast<VehicleIsoMode>(
                GetPrivateProfileIntA("IsometricCamera", "VehicleCameraMode", 1, ini.c_str()));
            char distBuf[32] = { 0 };
            GetPrivateProfileStringA("IsometricCamera", "ViewDistance", "22.0", distBuf, sizeof(distBuf), ini.c_str());
            g_Config.fViewDistance = ClampVal(static_cast<float>(atof(distBuf)), g_Config.fMinDistance, g_Config.fMaxDistance);
            g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>(
                GetPrivateProfileIntA("IsometricCamera", "DefaultQuadrant", 0, ini.c_str()) % 4);
        }

        static void Save() {
            std::string ini = GetIniPath();
            char buf[64];
            snprintf(buf, sizeof(buf), "%d", static_cast<int>(g_Config.eVehicleMode));
            WritePrivateProfileStringA("IsometricCamera", "VehicleCameraMode", buf, ini.c_str());
            snprintf(buf, sizeof(buf), "%.2f", g_Config.fViewDistance);
            WritePrivateProfileStringA("IsometricCamera", "ViewDistance", buf, ini.c_str());
            snprintf(buf, sizeof(buf), "%d", static_cast<int>(g_Config.eCurrentQuadrant));
            WritePrivateProfileStringA("IsometricCamera", "DefaultQuadrant", buf, ini.c_str());
        }
    };

    static bool VerifyGameVersion10US() {
        uint8_t byteAtHook = *reinterpret_cast<uint8_t*>(0x53C09A);
        if (byteAtHook == 0xE8) {
            Logger::Info("Assinatura de memoria validada: GTA San Andreas v1.0 US detectado.");
            return true;
        }
        Logger::Error("Assinatura em 0x53C09A invalida (0x%02X)!", byteAtHook);
        return false;
    }

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
        alpha = ClampVal(alpha, 0.0f, 1.0f);
        return CVector(
            current.x + (target.x - current.x) * alpha,
            current.y + (target.y - current.y) * alpha,
            current.z + (target.z - current.z) * alpha
        );
    }

    static float GetQuadrantYawRadians(IsoQuadrant q) {
        switch (q) {
            case IsoQuadrant::NORTH_EAST: return 45.0f * DEG2RAD;
            case IsoQuadrant::NORTH_WEST: return 135.0f * DEG2RAD;
            case IsoQuadrant::SOUTH_WEST: return 225.0f * DEG2RAD;
            case IsoQuadrant::SOUTH_EAST: return 315.0f * DEG2RAD;
            default: return 45.0f * DEG2RAD;
        }
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

    static bool ShouldYieldToNativeCamera(CPlaceable* pPlayer) {
        if (!pPlayer) return true;
        if (GameSA::IsMenuActive() || GameSA::IsUserPaused() || GameSA::IsCodePaused()) return true;
        if (GameSA::IsCutsceneRunning() || GameSA::IsWidescreenOn() || GameSA::ArePlayerControlsDisabled()) return true;

        uint16_t activeMode = GameSA::GetActiveCam().m_nMode;
        if (activeMode == MODE_SNIPER || activeMode == MODE_ROCKETLAUNCHER ||
            activeMode == MODE_ROCKETLAUNCHER_HS || activeMode == MODE_CAMERA ||
            activeMode == MODE_AIMWEAPON || activeMode == MODE_AIMWEAPON_FROMCAR ||
            activeMode == MODE_AIMWEAPON_ATTACHED) {
            return true;
        }
        return false;
    }

    static bool IsPlayerInInterior(CPlaceable* pPlayer) {
        return (GameSA::GetCurrentInteriorArea() != 0) || (GameSA::GetEntityAreaCode(pPlayer) != 0);
    }

    static void HandleCameraControlsAndCycling(CPlaceable* pPlayer, CPlaceable* pVehicle) {
        if (GameSA::IsMenuActive()) return;

        bool bInVehicle = (pVehicle != nullptr);
        static bool bPrevRotLeft = false;
        static bool bPrevRotRight = false;

        bool bModKeyDown = (GetAsyncKeyState(g_Config.vKeyModifier) & 0x8000) != 0 || GameSA::IsChangeViewButtonHeld();
        bool bRotLeftDown = bModKeyDown && (GetAsyncKeyState(g_Config.vKeyRotateLeft) & 0x8000) != 0;
        bool bRotRightDown = bModKeyDown && (GetAsyncKeyState(g_Config.vKeyRotateRight) & 0x8000) != 0;

        if (g_Config.bIsometricEnabled) {
            if (bRotLeftDown && !bPrevRotLeft) {
                int nextQ = (static_cast<int>(g_Config.eCurrentQuadrant) + 1) % 4;
                g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>(nextQ);
                ConfigManager::Save();
                char msg[96];
                snprintf(msg, sizeof(msg), "~y~Perspectiva:~w~ %s", GetQuadrantName(g_Config.eCurrentQuadrant));
                GameSA::AddHudMessage(msg, 1500);
            }
            if (bRotRightDown && !bPrevRotRight) {
                int prevQ = (static_cast<int>(g_Config.eCurrentQuadrant) + 3) % 4;
                g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>(prevQ);
                ConfigManager::Save();
                char msg[96];
                snprintf(msg, sizeof(msg), "~y~Perspectiva:~w~ %s", GetQuadrantName(g_Config.eCurrentQuadrant));
                GameSA::AddHudMessage(msg, 1500);
            }
        }
        bPrevRotLeft = bRotLeftDown;
        bPrevRotRight = bRotRightDown;

        // Botão V (Ciclar Câmera) ativa/desativa a isométrica como estágio extra
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

        if (!g_Config.bIsometricEnabled || ShouldYieldToNativeCamera(pPlayer)) {
            return;
        }

        float dtSeconds = ClampVal(GameSA::GetTimeStep() / 50.0f, 0.001f, 0.1f);
        bool bInVehicle = (pVehicle != nullptr);
        bool bInInterior = IsPlayerInInterior(pPlayer);

        CPlaceable* pFocusEntity = bInVehicle ? pVehicle : pPlayer;
        CVector targetPos = pFocusEntity->GetPosition();
        targetPos.z += bInVehicle ? 0.85f : 0.65f;

        float targetYawRad = GetQuadrantYawRadians(g_Config.eCurrentQuadrant);
        float targetPitchRad = (bInInterior ? ISO_INTERIOR_PITCH_DEG : ISO_DEFAULT_PITCH_DEG) * DEG2RAD;

        if (bInVehicle && g_Config.eVehicleMode == VehicleIsoMode::FLEXIBLE_BEHIND_CAR) {
            if (pVehicle->m_matrix) {
                CVector vehForward = pVehicle->m_matrix->up;
                float vehHeadingRad = std::atan2(-vehForward.x, vehForward.y);
                targetYawRad = NormalizeAngleRad(vehHeadingRad + PI);
            }
        }

        float desiredDist = g_Config.fViewDistance;
        if (bInInterior) {
            desiredDist = ClampVal(desiredDist * g_Config.fInteriorDistanceScale, 4.5f, 12.0f);
        }

        if (!g_bCamInitialized) {
            g_CurrentCamPos = GameSA::GetCameraPlaceable()->GetPosition();
            g_CurrentLookAt = targetPos;
            g_CurrentYawRad = targetYawRad;
            g_CurrentPitchRad = targetPitchRad;
            g_CurrentDist = desiredDist;
            g_bCamInitialized = true;
        }

        float rotLerpFactor = 1.0f - std::exp(-g_Config.fRotationSmoothSpeed * dtSeconds);
        g_CurrentYawRad = LerpAngleRad(g_CurrentYawRad, targetYawRad, rotLerpFactor);
        g_CurrentPitchRad = LerpAngleRad(g_CurrentPitchRad, targetPitchRad, rotLerpFactor);
        g_CurrentDist = g_CurrentDist + (desiredDist - g_CurrentDist) * rotLerpFactor;

        float horizontalRadius = g_CurrentDist * std::cos(g_CurrentPitchRad);
        float verticalHeight = g_CurrentDist * std::sin(g_CurrentPitchRad);

        CVector idealCamOffset(
            std::sin(g_CurrentYawRad) * horizontalRadius,
            -std::cos(g_CurrentYawRad) * horizontalRadius,
            verticalHeight
        );

        CVector idealCamPos = targetPos + idealCamOffset;

        CColPoint colPoint;
        void* pColEntity = nullptr;
        bool bHitObstacle = GameSA::ProcessLineOfSight(
            targetPos, idealCamPos, colPoint, pColEntity,
            true, false, false, true, false, true, true, false
        );

        if (bHitObstacle) {
            CVector hitVec = colPoint.m_vecPoint - targetPos;
            float hitDist = hitVec.Magnitude();
            if (hitDist > 1.8f) {
                hitVec.Normalise();
                idealCamPos = targetPos + hitVec * (hitDist - 0.45f);
            } else {
                idealCamPos = colPoint.m_vecPoint;
            }
        }

        float posSpeed = bHitObstacle ? g_Config.fCollisionPullInSpeed : g_Config.fPositionSmoothSpeed;
        g_CurrentCamPos = SmoothLerpVector(g_CurrentCamPos, idealCamPos, posSpeed, dtSeconds);
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

    static void ProcessPauseMenuControlsInjection() {
        if (!GameSA::IsMenuActive() || GameSA::GetCurrentMenuPage() != MENUPAGE_CONTROLLER_PC_ID) {
            g_MenuSelectedCustomRow = -1;
            return;
        }

        float scaleX = static_cast<float>(GameSA::GetScreenWidth()) / 640.0f;
        float scaleY = static_cast<float>(GameSA::GetScreenHeight()) / 448.0f;

        float basePosX = 68.0f * scaleX;
        float valuePosX = 572.0f * scaleX;
        float row0PosY = 308.0f * scaleY;
        float row1PosY = 334.0f * scaleY;
        float row2PosY = 360.0f * scaleY;

        float mouseY = static_cast<float>(GameSA::GetMenuMousePosY());
        if (mouseY >= row0PosY - (6.0f * scaleY) && mouseY < row1PosY - (4.0f * scaleY)) {
            g_MenuSelectedCustomRow = 0;
        } else if (mouseY >= row1PosY - (4.0f * scaleY) && mouseY < row2PosY - (4.0f * scaleY)) {
            g_MenuSelectedCustomRow = 1;
        } else if (mouseY >= row2PosY - (4.0f * scaleY)) {
            g_MenuSelectedCustomRow = 2;
        }

        static bool bPrevLeft = false, bPrevRight = false, bPrevEnter = false;
        bool bLeftDown = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 || (GetAsyncKeyState('A') & 0x8000) != 0;
        bool bRightDown = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 || (GetAsyncKeyState('D') & 0x8000) != 0;
        bool bEnterDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0 || (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        if (g_MenuSelectedCustomRow == 0 && ((bLeftDown && !bPrevLeft) || (bRightDown && !bPrevRight) || (bEnterDown && !bPrevEnter))) {
            g_Config.eVehicleMode = (g_Config.eVehicleMode == VehicleIsoMode::FLEXIBLE_BEHIND_CAR)
                ? VehicleIsoMode::FIXED_PERSPECTIVE
                : VehicleIsoMode::FLEXIBLE_BEHIND_CAR;
            ConfigManager::Save();
        } else if (g_MenuSelectedCustomRow == 1) {
            if (bLeftDown && !bPrevLeft) {
                g_Config.fViewDistance = ClampVal(g_Config.fViewDistance - 2.5f, g_Config.fMinDistance, g_Config.fMaxDistance);
                ConfigManager::Save();
            } else if ((bRightDown && !bPrevRight) || (bEnterDown && !bPrevEnter)) {
                g_Config.fViewDistance += 2.5f;
                if (g_Config.fViewDistance > g_Config.fMaxDistance) g_Config.fViewDistance = g_Config.fMinDistance;
                ConfigManager::Save();
            }
        } else if (g_MenuSelectedCustomRow == 2 && ((bLeftDown && !bPrevLeft) || (bRightDown && !bPrevRight) || (bEnterDown && !bPrevEnter))) {
            int delta = (bLeftDown && !bPrevLeft) ? 3 : 1;
            g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>((static_cast<int>(g_Config.eCurrentQuadrant) + delta) % 4);
            ConfigManager::Save();
        }

        bPrevLeft = bLeftDown;
        bPrevRight = bRightDown;
        bPrevEnter = bEnterDown;

        GameSA::FontSetBackground(false, false);
        GameSA::FontSetWrapx(620.0f * scaleX);
        GameSA::FontSetFontStyle(2);
        GameSA::FontSetProportional(true);
        GameSA::FontSetDropShadowPosition(1);
        GameSA::FontSetDropColor(CRGBA(0, 0, 0, 220));
        GameSA::FontSetScale(0.42f * scaleX, 0.88f * scaleY);

        GameSA::FontSetColor(g_MenuSelectedCustomRow == 0 ? CRGBA(225, 225, 255, 255) : CRGBA(145, 165, 190, 235));
        GameSA::FontSetOrientation(1);
        GameSA::FontPrintString(basePosX, row0PosY, "CAM. VEICULO ISOMETRICA");
        GameSA::FontSetOrientation(2);
        GameSA::FontPrintString(valuePosX, row0PosY, g_Config.eVehicleMode == VehicleIsoMode::FLEXIBLE_BEHIND_CAR ? "PERSPECTIVA FLEXIVEL" : "PERSPECTIVA FIXA");

        GameSA::FontSetColor(g_MenuSelectedCustomRow == 1 ? CRGBA(225, 225, 255, 255) : CRGBA(145, 165, 190, 235));
        GameSA::FontSetOrientation(1);
        GameSA::FontPrintString(basePosX, row1PosY, "DISTANCIA DE VISAO ISOMETRICA");
        char distText[64];
        snprintf(distText, sizeof(distText), "< %.1f METROS >", g_Config.fViewDistance);
        GameSA::FontSetOrientation(2);
        GameSA::FontPrintString(valuePosX, row1PosY, distText);

        GameSA::FontSetColor(g_MenuSelectedCustomRow == 2 ? CRGBA(225, 225, 255, 255) : CRGBA(145, 165, 190, 235));
        GameSA::FontSetOrientation(1);
        GameSA::FontPrintString(basePosX, row2PosY, "LADO DA PERSPECTIVA (ALT+Q/E)");
        GameSA::FontSetOrientation(2);
        GameSA::FontPrintString(valuePosX, row2PosY, GetQuadrantName(g_Config.eCurrentQuadrant));
    }

    typedef void(__thiscall* fnCCameraProcess)(void*);
    static fnCCameraProcess g_OrigCameraProcess = nullptr;

    static void __fastcall Hooked_CCamera_Process(void* pThis, void* /*edx*/) {
        if (g_OrigCameraProcess) {
            g_OrigCameraProcess(pThis);
        }
        ProcessIsometricCamera();
    }

    typedef void(__thiscall* fnMenuDrawFrontEnd)(void*);
    static fnMenuDrawFrontEnd g_OrigMenuDrawFrontEnd = nullptr;

    static void __fastcall Hooked_CMenuManager_DrawFrontEnd(void* pThis, void* /*edx*/) {
        if (g_OrigMenuDrawFrontEnd) {
            g_OrigMenuDrawFrontEnd(pThis);
        }
        ProcessPauseMenuControlsInjection();
    }

    static uintptr_t InstallRelativeCallHook(uintptr_t callInstructionAddr, uintptr_t newTargetAddr) {
        DWORD oldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(callInstructionAddr), 5, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return 0;
        }
        int32_t origRelOffset = *reinterpret_cast<int32_t*>(callInstructionAddr + 1);
        uintptr_t origTargetAddr = callInstructionAddr + 5 + origRelOffset;

        int32_t newRelOffset = static_cast<int32_t>(newTargetAddr - (callInstructionAddr + 5));
        *reinterpret_cast<int32_t*>(callInstructionAddr + 1) = newRelOffset;

        VirtualProtect(reinterpret_cast<void*>(callInstructionAddr), 5, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(callInstructionAddr), 5);
        return origTargetAddr;
    }

    static DWORD WINAPI DeferredInitThread(LPVOID /*lpParam*/) {
        Sleep(1500);
        Logger::Init(g_hThisModule);
        g_VehHandle = AddVectoredExceptionHandler(1, VectoredCrashHandler);

        if (!VerifyGameVersion10US()) return 0;

        ConfigManager::Load();
        AsiScanner::ScanLoadedModules();

        g_OrigCameraProcess = reinterpret_cast<fnCCameraProcess>(
            InstallRelativeCallHook(0x53C09A, reinterpret_cast<uintptr_t>(&Hooked_CCamera_Process))
        );

        g_OrigMenuDrawFrontEnd = reinterpret_cast<fnMenuDrawFrontEnd>(
            InstallRelativeCallHook(0x53EB9D, reinterpret_cast<uintptr_t>(&Hooked_CMenuManager_DrawFrontEnd))
        );

        Logger::Info("Hooks x86 instalados com sucesso.");
        return 0;
    }

} // namespace IsometricCamMod

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        IsometricCamMod::g_hThisModule = hModule;
        CreateThread(nullptr, 0, IsometricCamMod::DeferredInitThread, nullptr, 0, nullptr);
    } else if (ul_reason_for_call == DLL_PROCESS_DETACH) {
        IsometricCamMod::ConfigManager::Save();
        if (IsometricCamMod::g_VehHandle) {
            RemoveVectoredExceptionHandler(IsometricCamMod::g_VehHandle);
            IsometricCamMod::g_VehHandle = nullptr;
        }
        IsometricCamMod::Logger::Shutdown();
    }
    return TRUE;
}
