/**
 * ============================================================================
 * Mod: Isometric Camera Mod for GTA San Andreas (v1.0 US Hoodlum / Compact)
 * File: IsometricCameraSA.cpp
 * Target: .asi (Dynamic Link Library - x86 32-bit)
 * Framework: DK22Pac's plugin-sdk + Windows API + Direct3D9 / RenderWare
 * ============================================================================
 *
 * Funcionalidades implementadas:
 * 1. Integração ao botão nativo de câmera (V / Cycle Camera Mode): adiciona a
 *    câmera isométrica ao final do ciclo nativo (a pé e em veículos).
 * 2. Câmera fixa acompanhando o Player com interpolação suave (Exponential Lerp
 *    & Slerp angular) independente de framerate (usando CTimer::ms_fTimeStep).
 * 3. Rotação entre os 4 quadrantes isométricos (45°, 135°, 225°, 315°) via
 *    combinação de teclas configurável (Padrão: Segurar V + Q/E ou Alt + Setas).
 * 4. Integração nativa ao Menu de Pausa -> Opções -> Configuração de Controles
 *    (MENUPAGE_CONTROLLER_PC, ID 37) injetando entradas de menu customizadas e
 *    processando Left/Right/Enter para:
 *      - Modo de Câmera Isométrica em Veículo:
 *          * Perspectiva Fixa (mantém o quadrante global de 45° igual a pé)
 *          * Perspectiva Flexível (mantém a altura/pitch isométrico, mas alinha
 *            o yaw suavemente atrás do veículo)
 *      - Distância de Visão Isométrica (Zoom ajustável de 10m a 45m).
 * 5. Prevenção de conflitos com Cutscenes, Widescreen de Scripts (SCM),
 *    Mira (Aiming/Sniper/ Câmera Fotográfica) e transições de entrada/saída.
 * 6. Adaptação automática para Interiores (CGame::currArea != 0 || CEntryExitManager):
 *    reduz a distância focal, ajusta o ângulo de inclinação (pitch) e executa
 *    SphereCast/RayCast via CWorld::ProcessLineOfSight para evitar que a câmera
 *    fique presa acima do teto ou atrás de paredes.
 * 7. Sistema de Log completo (IsometricCameraSA.log) com timestamps, captura de
 *    exceções (Vectored Exception Handler), estado de memória e diagnóstico de
 *    conflitos.
 * 8. Scanner de Módulos .ASI (EnumProcessModules + Varredura de diretório):
 *    detecta todos os plugins .asi carregados simultaneamente no processo
 *    gta_sa.exe e alerta sobre potenciais conflitos de câmera/menu (ex:
 *    FirstPerson, CamHunt, V_HUD, SkyGfx, SilentPatch, etc.).
 * ============================================================================
 */

#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <psapi.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <ctime>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>

// Cabeçalhos oficiais do plugin-sdk (GTA San Andreas 1.0 US)
#include "plugin.h"
#include "CCamera.h"
#include "CWorld.h"
#include "CGame.h"
#include "CTimer.h"
#include "CPad.h"
#include "CMenuManager.h"
#include "CFont.h"
#include "CText.h"
#include "CHud.h"
#include "CMessages.h"
#include "CTheScripts.h"
#include "CEntryExitManager.h"
#include "CPlayerPed.h"
#include "CVehicle.h"
#include "CColPoint.h"
#include "CEntity.h"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace plugin;

namespace IsometricCamMod {

    // ========================================================================
    // 1. CONSTANTES E ENUMS DE CONFIGURAÇÃO
    // ========================================================================
    constexpr float PI = 3.14159265358979323846f;
    constexpr float DEG2RAD = PI / 180.0f;
    constexpr float RAD2DEG = 180.0f / PI;

    // Ângulo de inclinação isométrica clássica (arctan(1 / sqrt(2)) ≈ 35.264° ou 45° estilizado)
    constexpr float ISO_DEFAULT_PITCH_DEG = 38.0f;
    constexpr float ISO_INTERIOR_PITCH_DEG = 52.0f; // Mais vertical em interiores para evitar paredes

    enum class VehicleIsoMode : int {
        FIXED_PERSPECTIVE = 0,    // Mantém o ângulo cardinal isométrico fixo (igual a pé)
        FLEXIBLE_BEHIND_CAR = 1   // Mantém a altura/inclinação isométrica, mas gira atrás do veículo
    };

    enum class IsoQuadrant : int {
        NORTH_EAST = 0, // 45°
        NORTH_WEST = 1, // 135°
        SOUTH_WEST = 2, // 225°
        SOUTH_EAST = 3, // 315°
        COUNT = 4
    };

    struct ModConfig {
        bool bIsometricEnabled = false;
        VehicleIsoMode eVehicleMode = VehicleIsoMode::FLEXIBLE_BEHIND_CAR;
        IsoQuadrant eCurrentQuadrant = IsoQuadrant::NORTH_EAST;

        // Distância de visão configurável no menu (Padrão: 22.0m, Min: 10.0m, Max: 45.0m)
        float fViewDistance = 22.0f;
        float fMinDistance = 10.0f;
        float fMaxDistance = 45.0f;
        float fInteriorDistanceScale = 0.48f; // Escala reduzida automática para interiores

        // Fatores de suavização (interpolação exponencial)
        float fPositionSmoothSpeed = 7.5f;
        float fRotationSmoothSpeed = 6.0f;
        float fCollisionPullInSpeed = 18.0f;
        float fFOV = 55.0f; // FOV mais fechado para reduzir distorção de perspectiva e simular projeção isométrica

        // Combinações de teclas customizáveis via INI
        int vKeyModifier = VK_MENU;     // ALT (ou segurar o próprio botão de câmera)
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

    // Estado global do Mod
    static ModConfig g_Config;
    static std::vector<LoadedAsiInfo> g_LoadedAsiPlugins;
    static FILE* g_LogFile = nullptr;
    static PVOID g_VehHandle = nullptr;

    // Estado interno de interpolação e ciclo da câmera
    static CVector g_CurrentCamPos = { 0.0f, 0.0f, 0.0f };
    static CVector g_CurrentLookAt = { 0.0f, 0.0f, 0.0f };
    static float g_CurrentYawRad = 45.0f * DEG2RAD;
    static float g_CurrentPitchRad = ISO_DEFAULT_PITCH_DEG * DEG2RAD;
    static float g_CurrentDist = 22.0f;
    static bool g_bCamInitialized = false;
    static bool g_bWasInCutsceneOrScriptCam = false;
    static bool g_bWasInInterior = false;

    // Rastreamento do ciclo do botão nativo de câmera (V)
    static short g_LastOnFootCamMode = MODE_FOLLOWPED;
    static short g_LastVehicleCamMode = MODE_CAM_ON_A_STRING;
    static bool g_bCycleKeyHeld = false;
    static DWORD g_dwCycleKeyPressTime = 0;
    static bool g_bRotatedWhileHoldingCycle = false;

    // Menu de Pausa (Controles) - Estado da UI customizada
    static int g_MenuSelectedCustomRow = -1; // -1 = fora, 0 = Modo Veículo, 1 = Distância de Visão

    // ========================================================================
    // 2. SISTEMA DE LOG COMPLETO E DIAGNÓSTICO DE ERROS
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

    // Captura de exceções críticas (Crash Handler) para registrar se um conflito de memória ocorrer
    static LONG CALLBACK VectoredCrashHandler(PEXCEPTION_POINTERS pExceptionInfo) {
        if (!pExceptionInfo || !pExceptionInfo->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;

        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
        // Ignora exceções benignas de debug/thread naming da WinAPI
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
            Logger::Error("Estado da Camera no momento da falha: Ativa=%d, Interior=%d, ModoJogo=%d",
                          g_Config.bIsometricEnabled ? 1 : 0,
                          CGame::currArea,
                          TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // ========================================================================
    // 3. LEITURA E AUDITORIA DE OUTROS SCRIPTS .ASI RODANDO NO JOGO
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

                            // Filtra apenas arquivos .asi ou loaders conhecidos (CLEO.asi, modloader.asi, etc.)
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
            } else {
                Logger::Warn("EnumProcessModules falhou (Erro Win32: %lu). Executando fallback por FindFirstFile...", GetLastError());
            }

            // Também verifica arquivos .asi nas pastas raiz, /scripts e /plugins que possam carregar tardiamente
            ScanDirectoryForAsi("");
            ScanDirectoryForAsi("scripts\\");
            ScanDirectoryForAsi("plugins\\");
            ScanDirectoryForAsi("modloader\\");

            Logger::Info("Total de plugins .ASI ativos detectados em memoria: %zu", g_LoadedAsiPlugins.size());
            for (const auto& plugin : g_LoadedAsiPlugins) {
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
            } else if (lowerName.find("widescreen") != std::string::npos || lowerName.find("skygrad") != std::string::npos) {
                info.bPotentialCameraConflict = false;
                info.conflictReason = "Mod grafico/FOV compativel (verificar escalonamento de FOV).";
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
                        for (const auto& existing : g_LoadedAsiPlugins) {
                            if (_stricmp(existing.fileName.c_str(), fd.cFileName) == 0) {
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
    // 4. PERSISTÊNCIA DE CONFIGURAÇÃO (.INI)
    // ========================================================================
    class ConfigManager {
    public:
        static std::string GetIniPath() {
            char path[MAX_PATH] = { 0 };
            HMODULE hMod = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(&ConfigManager::GetIniPath), &hMod);
            GetModuleFileNameA(hMod, path, MAX_PATH);
            PathRenameExtensionA(path, ".ini");
            return std::string(path);
        }

        static void Load() {
            std::string ini = GetIniPath();
            g_Config.eVehicleMode = static_cast<VehicleIsoMode>(
                GetPrivateProfileIntA("IsometricCamera", "VehicleCameraMode", 1, ini.c_str()));
            
            char distBuf[32] = { 0 };
            GetPrivateProfileStringA("IsometricCamera", "ViewDistance", "22.0", distBuf, sizeof(distBuf), ini.c_str());
            g_Config.fViewDistance = std::clamp(static_cast<float>(atof(distBuf)), g_Config.fMinDistance, g_Config.fMaxDistance);

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
    // 5. MATEMÁTICA DE INTERPOLAÇÃO SUAVE E VERIFICAÇÃO DE VERSÃO (1.0 US)
    // ========================================================================
    static bool VerifyGameVersion10US() {
        // No GTA SA 1.0 US (Hoodlum / Compact), o endereço 0x82457C contém 0x94BF
        // ou podemos checar o EntryPoint/Cabeçalho PE via plugin::GetGameVersion()
        int gameVer = plugin::GetGameVersion();
        if (gameVer == GAME_10US_COMPACT || gameVer == GAME_10US_HOODLUM) {
            Logger::Info("Versao do executavel validada com sucesso: GTA SA 1.0 US (%s)",
                         gameVer == GAME_10US_COMPACT ? "Compact" : "Hoodlum");
            return true;
        }
        Logger::Error("Versao incompativel do gta_sa.exe detectada (ID=%d)! Este mod requer GTA SA v1.0 US.", gameVer);
        return false;
    }

    // Normaliza um ângulo em radianos para o intervalo [-PI, PI]
    static float NormalizeAngleRad(float angle) {
        while (angle > PI) angle -= 2.0f * PI;
        while (angle < -PI) angle += 2.0f * PI;
        return angle;
    }

    // Interpolação angular suave pelo menor arco (evita giro de 360° ao cruzar -PI/PI)
    static float LerpAngleRad(float current, float target, float factor) {
        float diff = NormalizeAngleRad(target - current);
        return NormalizeAngleRad(current + diff * std::clamp(factor, 0.0f, 1.0f));
    }

    // Interpolação vetorial suave independente de FPS
    static CVector SmoothLerpVector(const CVector& current, const CVector& target, float speed, float dtSeconds) {
        float alpha = 1.0f - std::exp(-speed * dtSeconds);
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        return CVector(
            current.x + (target.x - current.x) * alpha,
            current.y + (target.y - current.y) * alpha,
            current.z + (target.z - current.z) * alpha
        );
    }

    // Retorna o ângulo Yaw em radianos correspondente a cada um dos 4 lados da perspectiva isométrica
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
    // 6. DETECÇÃO DE CUTSCENES, SCRIPTS, MIRA E ADAPTAÇÃO A INTERIORES
    // ========================================================================
    static bool ShouldYieldToNativeCamera(CPlayerPed* pPlayer) {
        if (!pPlayer) return true;

        // 1. Pausa ou Menu FrontEnd ativo
        if (FrontEndMenuManager.m_bMenuActive || CTimer::m_UserPause || CTimer::m_CodePause) {
            return true;
        }

        // 2. Cutscene oficial do jogo ou Widescreen de missão SCM ativo
        if (CCutsceneMgr::ms_running || CCutsceneMgr::ms_cutsceneProcessing ||
            TheCamera.m_bWidescreenOn || TheCamera.m_bUseScriptZoomToCam) {
            if (!g_bWasInCutsceneOrScriptCam) {
                Logger::Info("Cutscene ou Widescreen de script detectado. Pausando camera isometrica temporariamente.");
                g_bWasInCutsceneOrScriptCam = true;
            }
            return true;
        }

        // 3. Verifica se um script SCM assumiu o controle fixo da câmera (ex: comando 015F SET_FIXED_CAMERA_POSITION)
        // Na estrutura CCamera do GTA SA 1.0 US, m_bUseTransitionBeta ou quando o player está com controles travados por script
        CPad* pad = CPad::GetPad(0);
        if (pad && pad->DisablePlayerControls && TheCamera.GetPosition().z != 0.0f) {
            // Se os controles estão desativados por script e não estamos apenas entrando em um carro
            if (pPlayer->m_nPedState != PEDSTATE_ENTER_CAR && pPlayer->m_nPedState != PEDSTATE_CARJACK &&
                pPlayer->m_nPedState != PEDSTATE_EXIT_CAR) {
                g_bWasInCutsceneOrScriptCam = true;
                return true;
            }
        }

        // 4. Verifica se o jogador está mirando com arma que exige câmera nativa (Sniper, RPG, Câmera Fotográfica, Mira Livre)
        short activeMode = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode;
        if (activeMode == MODE_SNIPER || activeMode == MODE_ROCKETLAUNCHER ||
            activeMode == MODE_ROCKETLAUNCHER_HS || activeMode == MODE_CAMERA ||
            activeMode == MODE_SYPHON || activeMode == MODE_AIMWEAPON ||
            activeMode == MODE_AIMWEAPON_FROMCAR || activeMode == MODE_AIMWEAPON_ATTACHED) {
            return true;
        }

        if (g_bWasInCutsceneOrScriptCam) {
            Logger::Info("Cutscene/Script encerrado. Retomando camera isometrica com transicao suave.");
            g_bWasInCutsceneOrScriptCam = false;
            // Sincroniza a posição inicial da interpolação com a câmera atual do jogo para retornar suavemente
            g_CurrentCamPos = TheCamera.GetPosition();
        }

        return false;
    }

    static bool IsPlayerInInterior(CPlayerPed* pPlayer) {
        // CGame::currArea (0xB72914) é > 0 quando o jogador está em um interior mapeado (ex: CJ's House, Ammu-Nation, Cassinos)
        // Também verificamos m_nAreaCode na entidade do Player e se o CEntryExitManager está em transição
        bool inInterior = (CGame::currArea != 0) || (pPlayer && pPlayer->m_nAreaCode != 0);
        if (inInterior != g_bWasInInterior) {
            Logger::Info("Mudanca de ambiente detectada: %s (AreaID=%d)",
                         inInterior ? "Entrou em Interior" : "Saiu para Exterior", CGame::currArea);
            g_bWasInInterior = inInterior;
        }
        return inInterior;
    }

    // ========================================================================
    // 7. CONTROLE DO BOTÃO DE CÂMERA (V) E COMBINAÇÕES DE 4 LADOS
    // ========================================================================
    static void HandleCameraControlsAndCycling(CPlayerPed* pPlayer) {
        CPad* pad = CPad::GetPad(0);
        if (!pad || FrontEndMenuManager.m_bMenuActive) return;

        bool bInVehicle = (pPlayer->m_nPedFlags.bInVehicle && pPlayer->m_pVehicle != nullptr);
        short currentNativeMode = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_nMode;

        // --------------------------------------------------------------------
        // A) Combinação de botões para girar os 4 lados da perspectiva isométrica
        //    Funciona com:
        //    1) Segurar o botão de Mudar Câmera (V) + Q / E (ou Olhar Esquerda / Direita)
        //    2) Segurar Tecla Modificadora (ALT) + Q / E ou Setas Esquerda / Direita
        // --------------------------------------------------------------------
        static bool bPrevRotLeft = false;
        static bool bPrevRotRight = false;

        bool bCycleBtnDown = (pad->NewState.ChangeView != 0);
        bool bModKeyDown = (GetAsyncKeyState(g_Config.vKeyModifier) & 0x8000) != 0 || bCycleBtnDown;

        bool bRotLeftDown = bModKeyDown && (
            (GetAsyncKeyState(g_Config.vKeyRotateLeft) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 ||
            pad->GetLookLeft()
        );
        bool bRotRightDown = bModKeyDown && (
            (GetAsyncKeyState(g_Config.vKeyRotateRight) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 ||
            pad->GetLookRight()
        );

        if (g_Config.bIsometricEnabled) {
            if (bRotLeftDown && !bPrevRotLeft) {
                int nextQ = (static_cast<int>(g_Config.eCurrentQuadrant) + 1) % 4;
                g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>(nextQ);
                g_bRotatedWhileHoldingCycle = true;
                ConfigManager::Save();

                char msg[96];
                snprintf(msg, sizeof(msg), "~y~Perspectiva Isometrica:~w~ %s", GetQuadrantName(g_Config.eCurrentQuadrant));
                CMessages::AddMessageJumpQ(msg, 1500, 0, false);
                Logger::Info("Quadrante isometrico alterado pelo jogador para: %s", GetQuadrantName(g_Config.eCurrentQuadrant));
            }
            if (bRotRightDown && !bPrevRotRight) {
                int prevQ = (static_cast<int>(g_Config.eCurrentQuadrant) + 3) % 4;
                g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>(prevQ);
                g_bRotatedWhileHoldingCycle = true;
                ConfigManager::Save();

                char msg[96];
                snprintf(msg, sizeof(msg), "~y~Perspectiva Isometrica:~w~ %s", GetQuadrantName(g_Config.eCurrentQuadrant));
                CMessages::AddMessageJumpQ(msg, 1500, 0, false);
                Logger::Info("Quadrante isometrico alterado pelo jogador para: %s", GetQuadrantName(g_Config.eCurrentQuadrant));
            }

            // Atalho opcional de Zoom In/Out in-game (Modificador + Plus/Minus)
            if (bModKeyDown) {
                float dt = CTimer::ms_fTimeStep / 50.0f;
                if (GetAsyncKeyState(g_Config.vKeyZoomIn) & 0x8000) {
                    g_Config.fViewDistance = std::clamp(g_Config.fViewDistance - 12.0f * dt, g_Config.fMinDistance, g_Config.fMaxDistance);
                } else if (GetAsyncKeyState(g_Config.vKeyZoomOut) & 0x8000) {
                    g_Config.fViewDistance = std::clamp(g_Config.fViewDistance + 12.0f * dt, g_Config.fMinDistance, g_Config.fMaxDistance);
                }
            }
        }
        bPrevRotLeft = bRotLeftDown;
        bPrevRotRight = bRotRightDown;

        // --------------------------------------------------------------------
        // B) Integração com o botão nativo de Mudar Câmera (V / CycleCameraMode)
        //    No GTA SA:
        //    - A pé: o jogo normalmente alterna distâncias (Perto -> Médio -> Longe).
        //      Adicionamos a Câmera Isométrica após a distância Longe (fPedZoomValue).
        //    - Em veículos: o jogo cicla (Perto -> Médio -> Longe -> Dinâmica -> Para-choque -> Cinematográfica).
        //      Adicionamos a Câmera Isométrica após a câmera Cinematográfica, antes de voltar para Perto.
        // --------------------------------------------------------------------
        if (pad->CycleCameraModeDownJustDown()) {
            g_bRotatedWhileHoldingCycle = false;

            if (g_Config.bIsometricEnabled) {
                // Jogador estava na câmera isométrica e apertou V novamente -> Volta ao ciclo normal do jogo
                g_Config.bIsometricEnabled = false;
                TheCamera.RestoreWithJumpCut();
                CMessages::AddMessageJumpQ("~w~Camera Padrao Restaurada", 1200, 0, false);
                Logger::Info("Botao de camera pressionado: Saindo do modo Isometrico para camera padrao.");
            } else {
                // Verifica se o jogador chegou ao fim do ciclo nativo de câmeras para entrar no modo Isométrico
                if (!bInVehicle) {
                    // A pé: TheCamera.m_nPedZoom (0 = Perto, 1 = Médio, 2 = Longe, 3 = Front/Fim)
                    if (TheCamera.m_nPedZoom >= 2) {
                        g_Config.bIsometricEnabled = true;
                        g_bCamInitialized = false;
                        CMessages::AddMessageJumpQ("~g~Camera Isometrica Ativada~w~ (Alt+Q/E gira os 4 lados)", 2200, 0, false);
                        Logger::Info("Botao de camera pressionado a pe: Modo Isometrico ATIVADO.");
                    }
                } else {
                    // Em veículo: m_nCarZoom (0..5, onde 5 é Cinematic Camera)
                    if (TheCamera.m_nCarZoom >= 5 || g_LastVehicleCamMode == MODE_CAM_ON_A_STRING) {
                        g_Config.bIsometricEnabled = true;
                        g_bCamInitialized = false;
                        CMessages::AddMessageJumpQ("~g~Camera Isometrica (Veiculo) Ativada", 2000, 0, false);
                        Logger::Info("Botao de camera pressionado em veiculo: Modo Isometrico ATIVADO.");
                    }
                }
            }
        }

        if (!bInVehicle) {
            g_LastOnFootCamMode = currentNativeMode;
        } else {
            g_LastVehicleCamMode = currentNativeMode;
        }
    }

    // ========================================================================
    // 8. CÁLCULO E APLICAÇÃO DA CÂMERA ISOMÉTRICA (CORE ENGINE)
    // ========================================================================
    static void ProcessIsometricCamera() {
        CPlayerPed* pPlayer = FindPlayerPed(0);
        if (!pPlayer) return;

        // 1. Processa entradas de teclado/controle (Botão V e rotação de quadrantes)
        HandleCameraControlsAndCycling(pPlayer);

        // 2. Se o modo isométrico estiver desativado ou se uma cutscene/mira estiver ativa, cede controle
        if (!g_Config.bIsometricEnabled || ShouldYieldToNativeCamera(pPlayer)) {
            return;
        }

        // Delta time em segundos baseado no CTimer::ms_fTimeStep do GTA SA (50.0f = 1 segundo)
        float dtSeconds = std::clamp(CTimer::ms_fTimeStep / 50.0f, 0.001f, 0.1f);

        bool bInVehicle = (pPlayer->m_nPedFlags.bInVehicle && pPlayer->m_pVehicle != nullptr);
        bool bInInterior = IsPlayerInInterior(pPlayer);

        // 3. Determina o ponto alvo (Target LookAt) - Fixo no Player ou no Veículo conduzido
        CEntity* pFocusEntity = bInVehicle ? static_cast<CEntity*>(pPlayer->m_pVehicle) : static_cast<CEntity*>(pPlayer);
        CVector targetPos = pFocusEntity->GetPosition();

        // Pequeno offset vertical para focar no torso do CJ / teto do veículo
        targetPos.z += bInVehicle ? 0.85f : 0.65f;

        // 4. Determina o ângulo horizontal (Yaw) e vertical (Pitch)
        float targetYawRad = GetQuadrantYawRadians(g_Config.eCurrentQuadrant);
        float targetPitchRad = (bInInterior ? ISO_INTERIOR_PITCH_DEG : ISO_DEFAULT_PITCH_DEG) * DEG2RAD;

        if (bInVehicle && g_Config.eVehicleMode == VehicleIsoMode::FLEXIBLE_BEHIND_CAR) {
            // MODO PERSPECTIVA FLEXÍVEL EM VEÍCULO:
            // A câmera mantém a altura e inclinação isométrica, mas se posiciona suavemente atrás do carro!
            CVehicle* pVeh = pPlayer->m_pVehicle;
            CVector vehForward = pVeh->GetMatrix()->up; // No RW/GTA SA, o eixo 'up' (Y) da matriz é o vetor Forward!
            float vehHeadingRad = std::atan2(-vehForward.x, vehForward.y);

            // Posiciona o vetor da câmera na direção oposta ao forward (atrás do veículo)
            targetYawRad = NormalizeAngleRad(vehHeadingRad + PI);
        }

        // 5. Determina a distância de visão ideal (adaptando automaticamente para interiores)
        float desiredDist = g_Config.fViewDistance;
        if (bInInterior) {
            desiredDist = std::clamp(desiredDist * g_Config.fInteriorDistanceScale, 4.5f, 12.0f);
        } else if (bInVehicle) {
            // Em veículos em alta velocidade, adiciona um leve afastamento dinâmico suave
            float speedMag = pPlayer->m_pVehicle->m_vecMoveSpeed.Magnitude();
            desiredDist += std::clamp(speedMag * 6.5f, 0.0f, 6.0f);
        }

        // Inicialização na primeira ativação para partir da câmera atual do jogo e fazer transição suave
        if (!g_bCamInitialized) {
            g_CurrentCamPos = TheCamera.GetPosition();
            g_CurrentLookAt = targetPos;
            g_CurrentYawRad = targetYawRad;
            g_CurrentPitchRad = targetPitchRad;
            g_CurrentDist = desiredDist;
            g_bCamInitialized = true;
        }

        // 6. Interpolação suave de Ângulos e Distância
        float rotLerpFactor = 1.0f - std::exp(-g_Config.fRotationSmoothSpeed * dtSeconds);
        g_CurrentYawRad = LerpAngleRad(g_CurrentYawRad, targetYawRad, rotLerpFactor);
        g_CurrentPitchRad = LerpAngleRad(g_CurrentPitchRad, targetPitchRad, rotLerpFactor);
        g_CurrentDist = g_CurrentDist + (desiredDist - g_CurrentDist) * rotLerpFactor;

        // 7. Calcula o offset esférico isométrico a partir do ângulo Pitch e Yaw interpolados
        float horizontalRadius = g_CurrentDist * std::cos(g_CurrentPitchRad);
        float verticalHeight = g_CurrentDist * std::sin(g_CurrentPitchRad);

        CVector idealCamOffset(
            std::sin(g_CurrentYawRad) * horizontalRadius,
            -std::cos(g_CurrentYawRad) * horizontalRadius,
            verticalHeight
        );

        CVector idealCamPos = targetPos + idealCamOffset;

        // 8. Adaptação para Interiores e Colisão de Cenário (RayCast via CWorld::ProcessLineOfSight)
        // Evita que a câmera atravesse tetos em interiores ou fique atrás de prédios altos
        CColPoint colPoint;
        CEntity* pColEntity = nullptr;

        // Dispara um raio do Player até a posição ideal da câmera checando edifícios e objetos estáticos
        bool bHitObstacle = CWorld::ProcessLineOfSight(
            targetPos,
            idealCamPos,
            colPoint,
            pColEntity,
            true,   // buildings (paredes, tetos de interiores, prédios)
            false,  // vehicles (ignora carros para não dar solavanco no trânsito)
            false,  // peds
            true,   // objects (portas, móveis grandes de interiores)
            false,  // dummies
            true,   // doSeeThroughCheck (ignora grades/vidros transparentes)
            true,   // doCameraIgnoreCheck
            false   // doShootThroughCheck
        );

        if (bHitObstacle) {
            // Se houver teto ou parede no caminho (muito comum em interiores), aproxima a câmera
            // deixando uma margem de segurança de 0.45m da superfície colidida
            CVector hitVec = colPoint.m_vecPoint - targetPos;
            float hitDist = hitVec.Magnitude();
            if (hitDist > 1.8f) {
                hitVec.Normalise();
                idealCamPos = targetPos + hitVec * (hitDist - 0.45f);
            } else {
                idealCamPos = colPoint.m_vecPoint;
            }
        }

        // 9. Interpolação suave final da Posição da Câmera e do Ponto de Foco (LookAt)
        float posSpeed = bHitObstacle ? g_Config.fCollisionPullInSpeed : g_Config.fPositionSmoothSpeed;
        g_CurrentCamPos = SmoothLerpVector(g_CurrentCamPos, idealCamPos, posSpeed, dtSeconds);
        g_CurrentLookAt = SmoothLerpVector(g_CurrentLookAt, targetPos, g_Config.fPositionSmoothSpeed * 1.4f, dtSeconds);

        // 10. Constrói os vetores ortonormais (Forward, Right, Up) da Matriz da Câmera
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

        // 11. Aplica os parâmetros diretamente na CCam ativa e na CMatrix de TheCamera
        CCam& activeCam = TheCamera.m_aCams[TheCamera.m_nActiveCam];
        activeCam.m_vecSource = g_CurrentCamPos;
        activeCam.m_vecFront = vecForward;
        activeCam.m_vecUp = vecUp;
        activeCam.m_vecTargetCoorsForFudgeInter = g_CurrentLookAt;
        activeCam.m_fFOV = g_Config.fFOV;

        // Sincroniza a matriz RenderWare / CPlaceable da TheCamera para garantir que o Frustum Culling
        // e o streaming de setores (LODs/Texturas) renderizem a direção correta da visão isométrica!
        CMatrix* pCamMat = TheCamera.GetMatrix();
        if (pCamMat) {
            pCamMat->right = vecRight;
            pCamMat->up = vecForward;   // Eixo Y no GTA SA = Vetor Front/Forward
            pCamMat->at = vecUp;        // Eixo Z no GTA SA = Vetor Up
            pCamMat->pos = g_CurrentCamPos;
            pCamMat->UpdateRW();
        }
        TheCamera.SetRwCamPosition();
        TheCamera.CalculateDerivedValues(false, true);

        // 12. Garante que o Player possa girar e caminhar em 360° livremente a pé usando a orientação da câmera
        TheCamera.m_fOrientation = g_CurrentYawRad;
    }

    // ========================================================================
    // 9. INTEGRAÇÃO AO MENU DE PAUSA -> SESSÃO CONTROLES (CMenuManager)
    // ========================================================================
    /**
     * No GTA SA 1.0 US, FrontEndMenuManager.m_nCurrentMenuPage indica a página atual:
     * - MENUPAGE_CONTROLLER_PC (ID 37): Menu principal de "Configuração de Controles" (Options -> Controller Setup)
     * - MENUPAGE_MOUSE_SETTINGS (ID 40): Configurações de Mouse/Direção dentro de Controles
     *
     * Renderizamos as opções diretamente na sessão Controles com o estilo tipográfico nativo
     * (CFont) do Menu FrontEnd e interceptamos a navegação (Seta Cima/Baixo/Esquerda/Direita/Enter/Mouse)
     * para alterar em tempo real:
     *   1) CAM ISO VEICULO: [ PERSPECTIVA FLEXIVEL (ATRAS) / PERSPECTIVA FIXA ]
     *   2) DISTANCIA CAM ISO: [ < 10m -------- 45m > ]
     */
    static void ProcessPauseMenuControlsInjection() {
        if (!FrontEndMenuManager.m_bMenuActive) {
            g_MenuSelectedCustomRow = -1;
            return;
        }

        // Verifica se o jogador está na sessão Controles (MENUPAGE_CONTROLLER_PC = 37)
        if (FrontEndMenuManager.m_nCurrentMenuPage != MENUPAGE_CONTROLLER_PC) {
            g_MenuSelectedCustomRow = -1;
            return;
        }

        // Coordenadas proporcionais à resolução atual (640x448 base virtual do GTA SA)
        float scaleX = static_cast<float>(RsGlobal.maximumWidth) / 640.0f;
        float scaleY = static_cast<float>(RsGlobal.maximumHeight) / 448.0f;

        float basePosX = 68.0f * scaleX;
        float valuePosX = 572.0f * scaleX;
        float row0PosY = 308.0f * scaleY; // Abaixo das opções nativas de Controller Setup
        float row1PosY = 334.0f * scaleY;
        float row2PosY = 360.0f * scaleY;

        // Detecta hover do mouse ou seleção por teclado nas nossas linhas extras
        float mouseY = static_cast<float>(FrontEndMenuManager.m_nMousePosY);
        if (mouseY >= row0PosY - (6.0f * scaleY) && mouseY < row1PosY - (4.0f * scaleY)) {
            g_MenuSelectedCustomRow = 0;
        } else if (mouseY >= row1PosY - (4.0f * scaleY) && mouseY < row2PosY - (4.0f * scaleY)) {
            g_MenuSelectedCustomRow = 1;
        } else if (mouseY >= row2PosY - (4.0f * scaleY) && mouseY < row2PosY + (22.0f * scaleY)) {
            g_MenuSelectedCustomRow = 2;
        }

        // Processa inputs de Esquerda / Direita / Enter para modificar as opções no menu
        static bool bPrevLeft = false, bPrevRight = false, bPrevEnter = false;
        bool bLeftDown = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 || (GetAsyncKeyState('A') & 0x8000) != 0;
        bool bRightDown = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0 || (GetAsyncKeyState('D') & 0x8000) != 0;
        bool bEnterDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0 || (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        // Permite também alternar com Tab se o usuário não usar o mouse na tela de controles
        static bool bPrevTab = false;
        bool bTabDown = (GetAsyncKeyState(VK_TAB) & 0x8000) != 0;
        if (bTabDown && !bPrevTab) {
            g_MenuSelectedCustomRow = (g_MenuSelectedCustomRow + 1) % 3;
        }
        bPrevTab = bTabDown;

        if (g_MenuSelectedCustomRow == 0) {
            // Opção 1: Modo de Câmera de Veículo em Isometria (Perspectiva Flexível vs Perspectiva Fixa)
            if ((bLeftDown && !bPrevLeft) || (bRightDown && !bPrevRight) || (bEnterDown && !bPrevEnter)) {
                if (g_Config.eVehicleMode == VehicleIsoMode::FLEXIBLE_BEHIND_CAR) {
                    g_Config.eVehicleMode = VehicleIsoMode::FIXED_PERSPECTIVE;
                } else {
                    g_Config.eVehicleMode = VehicleIsoMode::FLEXIBLE_BEHIND_CAR;
                }
                ConfigManager::Save();
                Logger::Info("Menu de Pausa (Controles): Modo de camera de veiculo alterado para %s",
                             g_Config.eVehicleMode == VehicleIsoMode::FLEXIBLE_BEHIND_CAR ? "Perspectiva Flexivel" : "Perspectiva Fixa");
            }
        } else if (g_MenuSelectedCustomRow == 1) {
            // Opção 2: Ajuste de Distância de Visão da Câmera Isométrica
            if (bLeftDown && !bPrevLeft) {
                g_Config.fViewDistance = std::clamp(g_Config.fViewDistance - 2.5f, g_Config.fMinDistance, g_Config.fMaxDistance);
                ConfigManager::Save();
                Logger::Info("Menu de Pausa (Controles): Distancia de visao reduzida para %.1fm", g_Config.fViewDistance);
            } else if ((bRightDown && !bPrevRight) || (bEnterDown && !bPrevEnter)) {
                g_Config.fViewDistance += 2.5f;
                if (g_Config.fViewDistance > g_Config.fMaxDistance) {
                    g_Config.fViewDistance = g_Config.fMinDistance;
                }
                ConfigManager::Save();
                Logger::Info("Menu de Pausa (Controles): Distancia de visao ajustada para %.1fm", g_Config.fViewDistance);
            }
        } else if (g_MenuSelectedCustomRow == 2) {
            // Opção 3: Alternar os 4 lados da perspectiva isométrica diretamente pelo menu
            if ((bLeftDown && !bPrevLeft) || (bRightDown && !bPrevRight) || (bEnterDown && !bPrevEnter)) {
                int delta = (bLeftDown && !bPrevLeft) ? 3 : 1;
                g_Config.eCurrentQuadrant = static_cast<IsoQuadrant>((static_cast<int>(g_Config.eCurrentQuadrant) + delta) % 4);
                ConfigManager::Save();
                Logger::Info("Menu de Pausa (Controles): Lado da perspectiva alterado para %s", GetQuadrantName(g_Config.eCurrentQuadrant));
            }
        }

        bPrevLeft = bLeftDown;
        bPrevRight = bRightDown;
        bPrevEnter = bEnterDown;

        // --------------------------------------------------------------------
        // Renderização com CFont (Estética nativa do Menu de Pausa do GTA SA)
        // --------------------------------------------------------------------
        CFont::SetBackground(false, false);
        CFont::SetWrapx(620.0f * scaleX);
        CFont::SetFontStyle(FONT_MENU);
        CFont::SetProportional(true);
        CFont::SetDropShadowPosition(1);
        CFont::SetDropColor(CRGBA(0, 0, 0, 220));

        // Linha 0: Modo de Câmera de Veículo em Isometria
        CRGBA colorRow0 = (g_MenuSelectedCustomRow == 0) ? CRGBA(225, 225, 255, 255) : CRGBA(145, 165, 190, 235);
        CFont::SetScale(0.42f * scaleX, 0.88f * scaleY);
        CFont::SetOrientation(ALIGN_LEFT);
        CFont::SetColor(colorRow0);
        CFont::PrintString(basePosX, row0PosY, "CAM. VEICULO ISOMETRICA");

        CFont::SetOrientation(ALIGN_RIGHT);
        const char* vehModeLabel = (g_Config.eVehicleMode == VehicleIsoMode::FLEXIBLE_BEHIND_CAR)
            ? "PERSPECTIVA FLEXIVEL (ATRAS)"
            : "PERSPECTIVA FIXA (A PE)";
        CFont::PrintString(valuePosX, row0PosY, vehModeLabel);

        // Linha 1: Ajuste de Distância de Visão da Câmera Isométrica
        CRGBA colorRow1 = (g_MenuSelectedCustomRow == 1) ? CRGBA(225, 225, 255, 255) : CRGBA(145, 165, 190, 235);
        CFont::SetOrientation(ALIGN_LEFT);
        CFont::SetColor(colorRow1);
        CFont::PrintString(basePosX, row1PosY, "DISTANCIA DE VISAO ISOMETRICA");

        char distText[64];
        int numBars = static_cast<int>(((g_Config.fViewDistance - g_Config.fMinDistance) / (g_Config.fMaxDistance - g_Config.fMinDistance)) * 10.0f);
        snprintf(distText, sizeof(distText), "< %.1f METROS [Barra %d/10] >", g_Config.fViewDistance, numBars);
        CFont::SetOrientation(ALIGN_RIGHT);
        CFont::PrintString(valuePosX, row1PosY, distText);

        // Linha 2: Lado da Perspectiva Isométrica (4 Quadrantes) + Dica de Teclas
        CRGBA colorRow2 = (g_MenuSelectedCustomRow == 2) ? CRGBA(225, 225, 255, 255) : CRGBA(145, 165, 190, 235);
        CFont::SetOrientation(ALIGN_LEFT);
        CFont::SetColor(colorRow2);
        CFont::PrintString(basePosX, row2PosY, "LADO DA PERSPECTIVA (ALT+Q/E)");

        CFont::SetOrientation(ALIGN_RIGHT);
        CFont::PrintString(valuePosX, row2PosY, GetQuadrantName(g_Config.eCurrentQuadrant));
    }

    // ========================================================================
    // 10. INICIALIZAÇÃO DO PLUGIN E HOOKS DE EVENTOS (plugin-sdk)
    // ========================================================================
    class IsometricCameraPlugin {
    public:
        IsometricCameraPlugin() {
            // Registra o callback de inicialização do motor do GTA SA
            Events::initRwEvent += []() {
                HMODULE hSelf = nullptr;
                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCSTR>(&VerifyGameVersion10US), &hSelf);

                Logger::Init(hSelf);
                g_VehHandle = AddVectoredExceptionHandler(1, VectoredCrashHandler);

                if (!VerifyGameVersion10US()) {
                    return;
                }

                ConfigManager::Load();
                AsiScanner::ScanLoadedModules();
                Logger::Info("Hooks de Camera e Menu de Pausa registrados com sucesso.");
            };

            // Quando um save é carregado ou um novo jogo começa, re-escaneia módulos tardios e reseta interpolação
            Events::initGameEvent += []() {
                g_bCamInitialized = false;
                Logger::Info("Sessao de jogo iniciada (initGameEvent). Re-verificando modulos .ASI...");
                AsiScanner::ScanLoadedModules();
            };

            // Hook no processamento de frame/scripts para calcular e aplicar a câmera isométrica
            // Executado logo após o processamento principal para sobrescrever TheCamera de forma limpa
            Events::processScriptsEvent.after += []() {
                ProcessIsometricCamera();
            };

            // Hook no desenho do Menu de Pausa (FrontEndMenuManager) para exibir as opções em Controles
            Events::drawMenuBackgroundEvent.after += []() {
                ProcessPauseMenuControlsInjection();
            };

            // Cleanup ao fechar o jogo
            Events::shutdownRwEvent += []() {
                ConfigManager::Save();
                if (g_VehHandle) {
                    RemoveVectoredExceptionHandler(g_VehHandle);
                    g_VehHandle = nullptr;
                }
                Logger::Shutdown();
            };
        }
    } g_IsometricCameraPluginInstance;

} // namespace IsometricCamMod