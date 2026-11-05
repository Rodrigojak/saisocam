/**
 * ============================================================================
 * gemini Mod: Isometric Camera Mod for GTA San Andreas (v1.0 US Hoodlum / Compact)
 * File: main.cpp / IsometricCameraSA.cpp
 * Target: .asi (Dynamic Link Library - x86 32-bit)
 * Build Command (GitHub Actions / MSVC x86):
 *   cl /EHsc /LD /O2 main.cpp user32.lib /link /OUT:IsoCam.asi
 * ============================================================================
 *
 * VERSÃO STANDALONE (ZERO DEPENDÊNCIAS EXTERNAS):
 * Não requer download externo do plugin-sdk ("plugin.h"). Todos os endereços de
 * memória, estruturas (CVector, CMatrix, CCam, CCamera, CMenuManager, CPad,
 * CColPoint) e ponteiros de função nativos do GTA San Andreas 1.0 US estão
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

// Linkagem automática das bibliotecas do Windows SDK no MSVC (cl.exe)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shlwapi.lib")

namespace IsometricCamMod {

    // ========================================================================
    // 1. UTILITÁRIOS MATEMÁTICOS E ESTRUTURAS NATIVAS DO GTA SA 1.0 US
    // ========================================================================
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
        CVector right;      // 0x00 (Eixo X)
        uint32_t flags;     // 0x0C
        CVector up;         // 0x10 (Eixo Y - Vetor Forward no GTA SA)
        uint32_t pad1;      // 0x1C
        CVector at;         // 0x20 (Eixo Z - Vetor Up no GTA SA)
        uint32_t pad2;      // 0x2C
        CVector pos;        // 0x30 (Posição no mundo)
        uint32_t pad3;      // 0x3C
        void* m_pAttachMatrix;
        uint8_t m_bOwnsAttachedMatrix;
        uint8_t pad4[3];

        void UpdateRW() {
            typedef void(__thiscall* fnUpdateRW)(CMatrix*);
            reinterpret_cast<fnUpdateRW>(0x59BBB0)(this);
        }
    };

    struct CPlaceable {
        void* vtable;           // 0x00
        CVector m_placementPos; // 0x04
        float m_fHeading;       // 0x10
        CMatrix* m_matrix;      // 0x14

        CVector GetPosition() const {
            if (m_matrix) return m_matrix->pos;
            return m_placementPos;
        }
    };

    struct CColPoint {
        CVector m_vecPoint;      // 0x00
        float field_C;           // 0x0C
        CVector m_vecNormal;     // 0x10
        float field_1C;          // 0x1C
        uint8_t m_nSurfaceTypeA; // 0x20
        uint8_t m_nPieceTypeA;   // 0x21
        uint8_t m_nLightingA;    // 0x22
        uint8_t padA;            // 0x23
        uint8_t m_nSurfaceTypeB; // 0x24
        uint8_t m_nPieceTypeB;   // 0x25
        uint8_t m_nLightingB;    // 0x26
        uint8_t padB;            // 0x27
        float m_fDepth;          // 0x28
    };

    // Estrutura CCam do GTA SA 1.0 US (Tamanho exato: 0x238 bytes)
    struct CCam {
        uint8_t pad0[0x0C];
        uint16_t m_nMode;                        // 0x0C
        uint8_t pad1[0xEC - 0x0E];
        float m_fFOV;                            // 0xEC
        uint8_t pad2[0x128 - 0xF0];
        CVector m_vecFront;                      // 0x128
        CVector m_vecSource;                     // 0x134
        CVector m_vecSourceBeforeLookBehind;     // 0x140
        CVector m_vecUp;                         // 0x14C
        uint8_t pad3[0x164 - 0x158];
        CVector m_vecTargetCoorsForFudgeInter;   // 0x164
        uint8_t pad4[0x238 - 0x170];
    };
    #pragma pack(pop)

    // Enum de Modos de Câmera Nativos do GTA SA
    enum eCamMode : uint16_t {
        MODE_NONE = 0,
        MODE_TOPDOWN = 1,
        MODE_GTACLASSIC = 2,
        MODE_BEHINDCAR = 3,
        MODE_FOLLOWPED = 4,
        MODE_AIMING = 5,
        MODE_SNIPER = 7,
        MODE_ROCKETLAUNCHER = 8,
        MODE_CAM_ON_A_STRING = 18,
        MODE_CAMERA = 46,
        MODE_ROCKETLAUNCHER_HS = 51,
        MODE_AIMWEAPON = 53,
        MODE_AIMWEAPON_FROMCAR = 55,
        MODE_AIMWEAPON_ATTACHED = 65
    };

    // ========================================================================
    // 2. ACESSORES DE MEMÓRIA E FUNÇÕES DO EXECUTÁVEL GTA_SA.EXE (1.0 US)
    // ========================================================================
    namespace GameSA {
        // Ponteiro base de TheCamera (0xB6F028)
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
            // 0 = Perto, 1 = Médio, 2 = Longe
            return *reinterpret_cast<uint16_t*>(TheCameraAddr() + 0xF0);
        }

        static inline uint16_t GetCarZoomIndicator() {
            // 0..5 (5 = Cinematic Camera)
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

        // CTimer e Pausa
        static inline float GetTimeStep() { return *reinterpret_cast<float*>(0xB7CB5C); }
        static inline bool IsUserPaused() { return *reinterpret_cast<bool*>(0xB7CB49); }
        static inline bool IsCodePaused() { return *reinterpret_cast<bool*>(0xB7CB48); }

        // Cutscenes e Interiores
        static inline bool IsCutsceneRunning() { return *reinterpret_cast<bool*>(0xB5F851); }
        static inline int GetCurrentInteriorArea() { return *reinterpret_cast<int*>(0xB72914); }

        // Menu de Pausa (FrontEndMenuManager = 0xBA6748)
        static inline bool IsMenuActive() { return *reinterpret_cast<bool*>(0xBA6748 + 0x32); }
        static inline uint8_t GetCurrentMenuPage() { return *reinterpret_cast<uint8_t*>(0xBA6748 + 0x15D); }
        static inline int GetMenuMousePosY() { return *reinterpret_cast<int*>(0xBA6748 + 0x3C); }

        // Resolução da Tela (RsGlobal = 0xC17040)
        static inline int GetScreenWidth() { return *reinterpret_cast<int*>(0xC17044); }
        static inline int GetScreenHeight() { return *reinterpret_cast<int*>(0xC17048); }

        // Entidades: Player e Veículo
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

        // Controles (CPad = 0xB73458)
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

        // Colisão (CWorld::ProcessLineOfSight = 0x56A490)
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

        // Mensagens de HUD (CMessages::AddMessageJumpQ = 0x69F1E0)
        static inline void AddHudMessage(const char* text, uint32_t timeMs = 1800) {
            typedef void(__cdecl* fnAddMessageJumpQ)(const char*, uint32_t, uint16_t, bool);
            reinterpret_cast<fnAddMessageJumpQ>(0x69F1E0)(text, timeMs, 0, false);
        }

        // Funções de Fonte Nativa (CFont) para desenhar no Menu de Pausa
        static inline void FontSetScale(float w, float h) {
            typedef void(__cdecl* fn)(float, float);
            reinterpret_cast<fn>(0x719380)(w, h);
        }
        static inline void FontSetColor(CRGBA color) {
            typedef void(__cdecl* fn)(CRGBA);
            reinterpret_cast<fn>(0x719430)(color);
        }
        static inline void FontSetOrientation(uint8_t align) {
            // 0 = Center, 1 = Left, 2 = Right
            typedef void(__cdecl* fn)(uint8_t);
            reinterpret_cast<fn>(0x719610)(align);
        }
        static inline void FontSetFontStyle(uint8_t style) {
            // 2 = FONT_MENU
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

    // ========================================================================
    // 3. CONSTANTES E ENUMS DE CONFIGURAÇÃO DO MOD
    // ========================================================================
    static const float ISO_DEFAULT_PITCH_DEG = 38.0f;
    static const float ISO_INTERIOR_PITCH_DEG = 52.0f;
    static const uint8_t MENUPAGE_CONTROLLER_PC_ID = 37;

    enum class VehicleIsoMode : int {
        FIXED_PERSPECTIVE = 0,    // Mantém o ângulo cardinal isométrico fixo (igual a pé)
        FLEXIBLE_BEHIND_CAR = 1   // Mantém a altura/inclinação isométrica, mas gira atrás do veículo
    };

    enum class IsoQuadrant : int {
        NORTH_EAST = 0, // 45°
        NORTH_WEST = 1, // 135°
        SOUTH_WEST = 2, // 225°
        SOUTH_EAST = 3  // 315°
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

        int vKeyModifier = VK_MENU;     // ALT (ou segurar o botão V de câmera)
        int vKeyRotateLeft = 'Q';       // Q ou Seta Esquerda
        int vKeyRotateRight = 'E';      // E ou Seta Direita
        int vKeyZoomIn = VK_OEM_PLUS;   // +
        int vKeyZoomOut = VK_OEM_MINUS; // -
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

    // Estado de interpolação da câmera
    static CVector g_CurrentCamPos(0.0f, 0.0f, 0.0f);
    static CVector g_CurrentLookAt(0.0f, 0.0f, 0.0f);
    static float g_CurrentYawRad = 45.0f * DEG2RAD;
    static float g_CurrentPitchRad = ISO_DEFAULT_PITCH_DEG * DEG2RAD;
    static float g_CurrentDist = 22.0f;
    static bool g_bCamInitialized = false;
    static bool g_bWasInCutsceneOrScriptCam = false;
    static bool g_bWasInInterior = false;
    static int g_MenuSelectedCustomRow = -1;

    // ========================================================================
    // 4. SISTEMA DE LOG COMPLETO E DIAGNÓSTICO DE ERROS
    // ========================================================================
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
            char modName[MAX_PATH] = "Desconhecido (gta_sa.exe ou memoria dinamica)";

            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(faultAddr), &hFaultMod)) {
                GetModuleFileNameA(hFaultMod, modName, MAX_PATH);
            }

            Logger::Error("CRITICAL EXCEPTION DETECTADA! Codigo: 0x%08X no endereco: 0x%08X", code, faultAddr);
            Logger::Error("Modulo causador provavel: %s (Base: 0x%08X, Offset: +0x%08X)",
                          PathFindFileNameA(modName),
                          reinterpret_cast<uintptr_t>(hFaultMod),
                          faultAddr - reinterpret_cast<uintptr_t>(hFaultMod));

            if (pExceptionInfo->ContextRecord) {
                Logger::Error("Registradores x86 -> EAX: 0x%08X | EBX: 0x%08X | ECX: 0x%08X | EDX: 0x%08X",
                              pExceptionInfo->ContextRecord->Eax, pExceptionInfo->ContextRecord->Ebx,
                              pExceptionInfo->ContextRecord->Ecx, pExceptionInfo->ContextRecord->Edx);
                Logger::Error("Registradores x86 -> ESI: 0x%08X | EDI: 0x%08X | EBP: 0x%08X | ESP: 0x%08X | EIP: 0x%08X",
                              pExceptionInfo->ContextRecord->Esi, pExceptionInfo->ContextRecord->Edi,
                              pExceptionInfo->ContextRecord->Ebp, pExceptionInfo->ContextRecord->Esp,
                              pExceptionInfo->ContextRecord->Eip);
            }
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // ========================================================================
    // 5. LEITURA E AUDITORIA DE OUTROS SCRIPTS .ASI RODANDO NO JOGO
    // ========================================================================
    class AsiScanner {
    public:
        static void ScanLoadedModules() {
            g_LoadedAsiPlugins.clear();
            Logger::Info("Iniciando varredura de modulos .ASI carregados no processo gta_sa.exe...");

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
                            if (_stricmp(ext, ".asi") == 0 || _stricmp(fileName, "vorbisFile.dll") == 0 ||
                                _stricmp(fileName, "vorbisHooked.dll") == 0) {

                                MODULEINFO modInfo = { 0 };
                                GetModuleInformation(hProcess, hMods[i], &modInfo, sizeof(modInfo));

                                LoadedAsiInfo info;
                                info.fileName = fileName;
                                info.fullPath = szModPath;
                                info.baseAddress = reinterpret_cast<uintptr_t>(modInfo.lpBaseOfDll);
                                info.imageSize = modInfo.SizeOfImage;
                                AnalyzeConflictRisk(info);

                                g_LoadedAsiPlugins.push_back(info);
                            }
                        }
                    }
                }
            }

            ScanDirectoryForAsi("");
            ScanDirectoryForAsi("scripts\\");
            ScanDirectoryForAsi("plugins\\");
            ScanDirectoryForAsi("modloader\\");

            Logger::Info("Total de plugins .ASI ativos detectados em memoria: %zu", g_LoadedAsiPlugins.size());
            for (size_t i = 0; i < g_LoadedAsiPlugins.size(); ++i) {
                const LoadedAsiInfo& plugin = g_LoadedAsiPlugins[i];
                if (plugin.bPotentialCameraConflict) {
                    Logger::Warn(" -> [CONFLITO POTENCIAL] %s (Base: 0x%08X, Tam: %lu KB) - Motivo: %s",
                                 plugin.fileName.c_str(), plugin.baseAddress, plugin.imageSize / 1024,
                                 plugin.conflictReason.c_str());
                } else {
                    Logger::Info(" -> [OK] %s (Base: 0x%08X, Tam: %lu KB) - %s",
                                 plugin.fileName.c_str(), plugin.baseAddress, plugin.imageSize / 1024,
                                 plugin.fullPath.c_str());
                }
            }
        }

    private:
        static void AnalyzeConflictRisk(LoadedAsiInfo& info) {
            std::string lowerName = info.fileName;
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

            info.bPotentialCameraConflict = false;
            info.conflictReason = "Nenhum conflito conhecido";

            if (lowerName.find("firstperson") != std::string::npos || lowerName.find("fps") != std::string::npos) {
                info.bPotentialCameraConflict = true;
                info.conflictReason = "Mod de First Person sobrescreve CCamera::Process e ciclo do botao V.";
            } else if (lowerName.find("camhunt") != std::string::npos || lowerName.find("freecam") != std::string::npos) {
                info.bPotentialCameraConflict = true;
                info.conflictReason = "Mod de FreeCam / CamHunt manipula matrizes de TheCamera diretamente.";
            } else if (lowerName.find("v_hud") != std::string::npos || lowerName.find("gtavhud") != std::string::npos) {
                info.bPotentialCameraConflict = true;
                info.conflictReason = "GTA V HUD altera o CMenuManager e pode substituir a tela de opcoes/controles.";
            } else if (lowerName.find("cleo") != std::string::npos) {
                info.bPotentialCameraConflict = false;
                info.conflictReason = "Runtime CLEO detectado (scripts .cs podem acionar cameras SCM).";
            }
        }

        static void ScanDirectoryForAsi(const char* subDir) {
            char searchPattern[MAX_PATH];
            snprintf(searchPattern, sizeof(searchPattern), "%s*.asi", subDir);

            WIN32_FIND_DATAA fd;
            HANDLE hFind = FindFirstFileA(searchPattern, &fd);
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                        HMODULE hLoaded = GetModuleHandleA(fd.cFileName);
                        bool bAlreadyListed = false;
                        for (size_t i = 0; i < g_LoadedAsiPlugins.size(); ++i) {
                            if (_stricmp(g_LoadedAsiPlugins[i].fileName.c_str(), fd.cFileName) == 0) {
                                bAlreadyListed = true;
                                break;
                            }
                        }
                        if (!bAlreadyListed && hLoaded != nullptr) {
                            LoadedAsiInfo info;
                            info.fileName = fd.cFileName;
                            info.fullPath = std::string(subDir) + fd.cFileName;
                            info.baseAddress = reinterpret_cast<uintptr_t>(hLoaded);
                            info.imageSize = fd.nFileSizeLow;
                            AnalyzeConflictRisk(info);
                            g_LoadedAsiPlugins.push_back(info);
                        } else if (!bAlreadyListed && hLoaded == nullptr) {
                            Logger::Warn("Arquivo .ASI encontrado no disco mas NAO carregado em memoria: %s%s", subDir, fd.cFileName);
                        }
                    }
                } while (FindNextFileA(hFind, &fd));
                FindClose(hFind);
            }
        }
    };

    // ========================================================================
    // 6. PERSISTÊNCIA DE CONFIGURAÇÃO (.INI)
    // ========================================================================
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

            g_Config.vKeyModifier = GetPrivateProfileIntA("Controls", "KeyModifier", VK_MENU, ini.c_str());
            g_Config.vKeyRotateLeft = GetPrivateProfileIntA("Controls", "KeyRotateLeft", 'Q', ini.c_str());
            g_Config.vKeyRotateRight = GetPrivateProfileIntA("Controls", "KeyRotateRight", 'E', ini.c_str());

            Logger::Info("Configuracao carregada (%s): ModoVeiculo=%d, Distancia=%.1fm, Quadrante=%d",
                         PathFindFileNameA(ini.c_str()),
                         static_cast<int>(g_Config.eVehicleMode),
                         g_Config.fViewDistance,
                         static_cast<int>(g_Config.eCurrentQuadrant));
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

            snprintf(buf, sizeof(buf), "%d", g_Config.vKeyModifier);
            WritePrivateProfileStringA("Controls", "KeyModifier", buf, ini.c_str());

            snprintf(buf, sizeof(buf), "%d", g_Config.vKeyRotateLeft);
            WritePrivateProfileStringA("Controls", "KeyRotateLeft", buf, ini.c_str());

            snprintf(buf, sizeof(buf), "%d", g_Config.vKeyRotateRight);
            WritePrivateProfileStringA("Controls", "KeyRotateRight", buf, ini.c_str());
        }
    };

    // ========================================================================
    // 7. MATEMÁTICA DE INTERPOLAÇÃO E VERIFICAÇÃO DE VERSÃO (1.0 US)
    // ========================================================================
    static bool VerifyGameVersion10US() {
        // Verifica assinatura no cabeçalho/código do GTA SA 1.0 US (0x53E968 contém 'CALL CCamera::Process' = 0xE8)
        uint8_t byteAtHook = *reinterpret_cast<uint8_t*>(0x53C09A);
        if (byteAtHook == 0xE8) {
            Logger::Info("Assinatura de memoria validada: GTA San Andreas v1.0 US detectado.");
            return true;
        }
        Logger::Error("Assinatura em 0x53C09A invalida (0x%02X)! Certifique-se de usar o gta_sa.exe v1.0 US.", byteAtHook);
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

    // ========================================================================
    // 8. DETECÇÃO DE CUTSCENES, SCRIPTS, MIRA E ADAPTAÇÃO A INTERIORES
    // ========================================================================
    static bool ShouldYieldToNativeCamera(CPlaceable* pPlayer) {
        if (!pPlayer) return true;

        if (GameSA::IsMenuActive() || GameSA::IsUserPaused() || GameSA::IsCodePaused()) {
            return true;
        }

        if (GameSA::IsCutsceneRunning() || GameSA::IsWidescreenOn() || GameSA::ArePlayerControlsDisabled()) {
            if (!g_bWasInCutsceneOrScriptCam) {
                Logger::Info("Cutscene ou Widescreen de script detectado. Pausando camera isometrica temporariamente.");
                g_bWasInCutsceneOrScriptCam = true;
            }
            return true;
        }

        uint16_t activeMode = GameSA::GetActiveCam().m_nMode;
        if (activeMode == MODE_SNIPER || activeMode == MODE_ROCKETLAUNCHER ||
            activeMode == MODE_ROCKETLAUNCHER_HS || activeMode == MODE_CAMERA ||
            activeMode == MODE_AIMWEAPON || activeMode == MODE_AIMWEAPON_FROMCAR ||
            activeMode == MODE_AIMWEAPON_ATTACHED) {
            return true;
        }

        if (g_bWasInCutsceneOrScriptCam) {
            Logger::Info("Cutscene/Script encerrado. Retomando camera isometrica com transicao suave.");
            g_bWasInCutsceneOrScriptCam = false;
            g_CurrentCamPos = GameSA::GetCameraPlaceable()->GetPosition();
        }

        return false;
    }

    static bool IsPlayerInInterior(CPlaceable* pPlayer) {
        int currArea = GameSA::GetCurrentInteriorArea();
        bool inInterior = (currArea != 0) || (GameSA::GetEntityAreaCode(pPlayer) != 0);
        if (inInterior != g_bWasInInterior) {
            Logger::Info("Mudanca de ambiente detectada: %s (AreaID=%d)",
                         inInterior ? "Entrou em Interior" : "Saiu para Exterior", currArea);
            g_bWasInInterior = inInterior;
        }
        return inInterior;
    }

    // ========================================================================
    // 9. CONTROLE DO BOTÃO DE CÂMERA (V) E COMBINAÇÕES DE 4 LADOS
    // ========================================================================
    static void HandleCameraControlsAndCycling(CPlaceable* pPlayer, CPlaceable* pVehicle) {
        if (GameSA::IsMenuActive()) return;

        bool bInVehicle = (pVehicle != nullptr);

        static bool bPrevRotLeft = false;
        static bool bPrevRotRight = false;

        bool bModKeyDown = (GetAsyncKeyState(g_Config.vKeyModifier) & 0x8000) != 0 || GameSA::IsChangeViewButtonHeld();
        bool bRotLeftDown = bModKeyDown && (
            (GetAsyncKeyState(g_Config.vKeyRotateLeft) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0
        );
        bool bRotRightDown = bModKeyDown && (
            (GetAsyncKeyState(g_Config.vKeyRotateRight) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0
        );

        if (g_Config.bIsometricEnabled) {
            if (bRotLeftDown && !bPrevRotLeft) {
                int nextQ = (static_cast<int>(g_Config.eCurrentQuadrant) + 1) % 4;
                g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>(nextQ);
                ConfigManager::Save();

                char msg[96];
                snprintf(msg, sizeof(msg), "~y~Perspectiva Isometrica:~w~ %s", GetQuadrantName(g_Config.eCurrentQuadrant));
                GameSA::AddHudMessage(msg, 1500);
                Logger::Info("Quadrante isometrico alterado para: %s", GetQuadrantName(g_Config.eCurrentQuadrant));
            }
            if (bRotRightDown && !bPrevRotRight) {
                int prevQ = (static_cast<int>(g_Config.eCurrentQuadrant) + 3) % 4;
                g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>(prevQ);
                ConfigManager::Save();

                char msg[96];
                snprintf(msg, sizeof(msg), "~y~Perspectiva Isometrica:~w~ %s", GetQuadrantName(g_Config.eCurrentQuadrant));
                GameSA::AddHudMessage(msg, 1500);
                Logger::Info("Quadrante isometrico alterado para: %s", GetQuadrantName(g_Config.eCurrentQuadrant));
            }

            if (bModKeyDown) {
                float dt = GameSA::GetTimeStep() / 50.0f;
                if (GetAsyncKeyState(g_Config.vKeyZoomIn) & 0x8000) {
                    g_Config.fViewDistance = ClampVal(g_Config.fViewDistance - 12.0f * dt, g_Config.fMinDistance, g_Config.fMaxDistance);
                } else if (GetAsyncKeyState(g_Config.vKeyZoomOut) & 0x8000) {
                    g_Config.fViewDistance = ClampVal(g_Config.fViewDistance + 12.0f * dt, g_Config.fMinDistance, g_Config.fMaxDistance);
                }
            }
        }
        bPrevRotLeft = bRotLeftDown;
        bPrevRotRight = bRotRightDown;

        // Integração com o botão nativo de mudar câmera (V)
        if (GameSA::CycleCameraModeDownJustDown()) {
            if (g_Config.bIsometricEnabled) {
                g_Config.bIsometricEnabled = false;
                GameSA::RestoreCameraWithJumpCut();
                GameSA::AddHudMessage("~w~Camera Padrao Restaurada", 1200);
                Logger::Info("Botao de camera pressionado: Saindo do modo Isometrico.");
            } else {
                if (!bInVehicle && GameSA::GetPedZoomIndicator() >= 2) {
                    g_Config.bIsometricEnabled = true;
                    g_bCamInitialized = false;
                    GameSA::AddHudMessage("~g~Camera Isometrica Ativada~w~ (Alt+Q/E gira os 4 lados)", 2200);
                    Logger::Info("Botao de camera pressionado a pe: Modo Isometrico ATIVADO.");
                } else if (bInVehicle && GameSA::GetCarZoomIndicator() >= 5) {
                    g_Config.bIsometricEnabled = true;
                    g_bCamInitialized = false;
                    GameSA::AddHudMessage("~g~Camera Isometrica (Veiculo) Ativada", 2000);
                    Logger::Info("Botao de camera pressionado em veiculo: Modo Isometrico ATIVADO.");
                }
            }
        }
    }

    // ========================================================================
    // 10. CÁLCULO E APLICAÇÃO DA CÂMERA ISOMÉTRICA (CORE ENGINE)
    // ========================================================================
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
        } else if (bInVehicle) {
            float speedMag = GameSA::GetEntityMoveSpeed(pVehicle).Magnitude();
            desiredDist += ClampVal(speedMag * 6.5f, 0.0f, 6.0f);
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

        // RayCast para adaptação em interiores e prevenção de colisão com tetos/paredes
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
        if (vecForward.Magnitude() < 0.001f) {
            vecForward = CVector(0.0f, 1.0f, -0.5f);
        }
        vecForward.Normalise();

        CVector worldUp(0.0f, 0.0f, 1.0f);
        CVector vecRight = CrossProduct(worldUp, vecForward);
        if (vecRight.Magnitude() < 0.001f) {
            vecRight = CVector(1.0f, 0.0f, 0.0f);
        }
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

    // ========================================================================
    // 11. INTEGRAÇÃO AO MENU DE PAUSA -> SESSÃO CONTROLES (ID 37)
    // ========================================================================
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
        } else if (mouseY >= row2PosY - (4.0f * scaleY) && mouseY < row2PosY + (22.0f * scaleY)) {
            g_MenuSelectedCustomRow = 2;
        }

        static bool bPrevLeft = false, bPrevRight = false, bPrevEnter = false, bPrevTab = false;
        bool bLeftDown = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 || (GetAsyncKeyState('A') & 0x8000) != 0;
        bool bRightDown = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 || (GetAsyncKeyState('D') & 0x8000) != 0;
        bool bEnterDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0 || (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool bTabDown = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;

        if (bTabDown && !bPrevTab) {
            g_MenuSelectedCustomRow = (g_MenuSelectedCustomRow + 1) % 3;
        }
        bPrevTab = bTabDown;

        if (g_MenuSelectedCustomRow == 0) {
            if ((bLeftDown && !bPrevLeft) || (bRightDown && !bPrevRight) || (bEnterDown && !bPrevEnter)) {
                g_Config.eVehicleMode = (g_Config.eVehicleMode == VehicleIsoMode::FLEXIBLE_BEHIND_CAR)
                    ? VehicleIsoMode::FIXED_PERSPECTIVE
                    : VehicleIsoMode::FLEXIBLE_BEHIND_CAR;
                ConfigManager::Save();
                Logger::Info("Menu Controles: Modo de camera de veiculo alterado para %d", static_cast<int>(g_Config.eVehicleMode));
            }
        } else if (g_MenuSelectedCustomRow == 1) {
            if (bLeftDown && !bPrevLeft) {
                g_Config.fViewDistance = ClampVal(g_Config.fViewDistance - 2.5f, g_Config.fMinDistance, g_Config.fMaxDistance);
                ConfigManager::Save();
            } else if ((bRightDown && !bPrevRight) || (bEnterDown && !bPrevEnter)) {
                g_Config.fViewDistance += 2.5f;
                if (g_Config.fViewDistance > g_Config.fMaxDistance) g_Config.fViewDistance = g_Config.fMinDistance;
                ConfigManager::Save();
            }
        } else if (g_MenuSelectedCustomRow == 2) {
            if ((bLeftDown && !bPrevLeft) || (bRightDown && !bPrevRight) || (bEnterDown && !bPrevEnter)) {
                int delta = (bLeftDown && !bPrevLeft) ? 3 : 1;
                g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>((static_cast<int>(g_Config.eCurrentQuadrant) + delta) % 4);
                ConfigManager::Save();
            }
        }

        bPrevLeft = bLeftDown;
        bPrevRight = bRightDown;
        bPrevEnter = bEnterDown;

        GameSA::FontSetBackground(false, false);
        GameSA::FontSetWrapx(620.0f * scaleX);
        GameSA::FontSetFontStyle(2); // FONT_MENU
        GameSA::FontSetProportional(true);
        GameSA::FontSetDropShadowPosition(1);
        GameSA::FontSetDropColor(CRGBA(0, 0, 0, 220));
        GameSA::FontSetScale(0.42f * scaleX, 0.88f * scaleY);

        // Linha 0: Modo de Câmera de Veículo
        GameSA::FontSetColor(g_MenuSelectedCustomRow == 0 ? CRGBA(225, 225, 255, 255) : CRGBA(145, 165, 190, 235));
        GameSA::FontSetOrientation(1); // ALIGN_LEFT
        GameSA::FontPrintString(basePosX, row0PosY, "CAM. VEICULO ISOMETRICA");
        GameSA::FontSetOrientation(2); // ALIGN_RIGHT
        GameSA::FontPrintString(valuePosX, row0PosY,
            g_Config.eVehicleMode == VehicleIsoMode::FLEXIBLE_BEHIND_CAR
                ? "PERSPECTIVA FLEXIVEL (ATRAS)"
                : "PERSPECTIVA FIXA (A PE)");

        // Linha 1: Distância de Visão
        GameSA::FontSetColor(g_MenuSelectedCustomRow == 1 ? CRGBA(225, 225, 255, 255) : CRGBA(145, 165, 190, 235));
        GameSA::FontSetOrientation(1);
        GameSA::FontPrintString(basePosX, row1PosY, "DISTANCIA DE VISAO ISOMETRICA");
        char distText[64];
        int numBars = static_cast<int>(((g_Config.fViewDistance - g_Config.fMinDistance) / (g_Config.fMaxDistance - g_Config.fMinDistance)) * 10.0f);
        snprintf(distText, sizeof(distText), "< %.1f METROS [Barra %d/10] >", g_Config.fViewDistance, numBars);
        GameSA::FontSetOrientation(2);
        GameSA::FontPrintString(valuePosX, row1PosY, distText);

        // Linha 2: Lado da Perspectiva (4 Quadrantes)
        GameSA::FontSetColor(g_MenuSelectedCustomRow == 2 ? CRGBA(225, 225, 255, 255) : CRGBA(145, 165, 190, 235));
        GameSA::FontSetOrientation(1);
        GameSA::FontPrintString(basePosX, row2PosY, "LADO DA PERSPECTIVA (ALT+Q/E)");
        GameSA::FontSetOrientation(2);
        GameSA::FontPrintString(valuePosX, row2PosY, GetQuadrantName(g_Config.eCurrentQuadrant));
    }

    // ========================================================================
    // 12. SISTEMA DE HOOKS NATIVOS X86 (SEM DEPENDÊNCIAS EXTERNAS)
    // ========================================================================
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
        // Aguarda o loop principal do GTA SA 1.0 US inicializar
        Sleep(1500);

        Logger::Init(g_hThisModule);
        g_VehHandle = AddVectoredExceptionHandler(1, VectoredCrashHandler);

        if (!VerifyGameVersion10US()) {
            return 0;
        }

        ConfigManager::Load();
        AsiScanner::ScanLoadedModules();

        // 1. Hook na chamada de CCamera::Process dentro de CGame::Process (0x53C09A no GTA SA 1.0 US)
        g_OrigCameraProcess = reinterpret_cast<fnCCameraProcess>(
            InstallRelativeCallHook(0x53C09A, reinterpret_cast<uintptr_t>(&Hooked_CCamera_Process))
        );

        // 2. Hook na chamada de CMenuManager::DrawFrontEnd (0x53EB9D no GTA SA 1.0 US)
        g_OrigMenuDrawFrontEnd = reinterpret_cast<fnMenuDrawFrontEnd>(
            InstallRelativeCallHook(0x53EB9D, reinterpret_cast<uintptr_t>(&Hooked_CMenuManager_DrawFrontEnd))
        );

        Logger::Info("Hooks x86 instalados com sucesso (CCamera::Process=0x%08X, DrawFrontEnd=0x%08X).",
                     reinterpret_cast<uintptr_t>(g_OrigCameraProcess),
                     reinterpret_cast<uintptr_t>(g_OrigMenuDrawFrontEnd));
        return 0;
    }

} // namespace IsometricCamMod

// Ponto de entrada padrão da DLL (.asi) no Windows
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
