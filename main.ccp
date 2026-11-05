/*
    ========================================================================
     IsoCam.cpp  -  Camera Isometrica para GTA San Andreas 1.0 US
    ========================================================================

    O que este mod faz:
      - Adiciona um modo de camera ISOMETRICA ao ciclo do botao nativo
        "Mudar Camera" do jogo (a pe e dentro de veiculo).
      - Camera fixa sobre o player, com 4 lados (N/E/S/W) trocaveis por
        combinacao de teclas, com transicao suave (interpolada).
      - No veiculo, duas opcoes configuraveis pelo menu de pausa:
          * Flexivel: mantem a mesma altura/angulo, mas gira acompanhando
                      a traseira do carro (como uma camera de perseguicao).
          * Fixa:     mantem a mesma direcao isometrica do mundo que o
                      player tinha a pe, independente da direcao do carro.
      - Ajuste de distancia de visao da camera isometrica pelo menu.
      - Adaptacao automatica em interiores (reduz distancia para evitar
        atravessar paredes).
      - Protecao para nao brigar com cutscenes/camera de script.
      - Sistema de log completo (arquivo IsoCam.log) para depuracao.
      - Deteccao de outros plugins .asi carregados junto no processo.

    Base tecnica:
      Este mod foi escrito para ser compilado com o plugin-sdk
      (https://github.com/DK22Pac/plugin-sdk), que ja resolve os
      enderecos de memoria da versao 1.0 US para as classes do jogo
      (CCamera, CPad, CGame, CMenuManager, etc). Alguns valores que o
      plugin-sdk nao expõe por nome (os bytes de "modo de camera atual",
      a pe e em veiculo) sao acessados por endereco direto, confirmados
      a partir de engenharia reversa publica da comunidade (MTA:SA /
      CCameraSA.h) para a build 1.0 US:
          VAR_PedCameraView     = 0x00B6F0F0  (BYTE)
          VAR_VehicleCameraView = 0x00B6F0DC  (BYTE)

    IMPORTANTE - LEIA O README.md:
      Alguns nomes/valores abaixo (contagem de modos nativos de camera,
      ID da pagina "Controles" do menu, o parametro de modo da funcao
      TakeControlNoEntity, o sinal usado para calcular o heading do
      veiculo) dependem de pequenas variacoes entre copias do plugin-sdk
      e devem ser CALIBRADOS testando no jogo com o log ativado. Isso e
      normal em engenharia reversa de jogos antigos: nenhum mod deste
      tipo funciona 100% "de primeira" sem um teste in-game. O log foi
      construido exatamente para te ajudar nesse ajuste fino.

    ========================================================================
*/

#include "plugin.h"

#include <game_sa/common.h>
#include <game_sa/CCamera.h>
#include <game_sa/CPad.h>
#include <game_sa/CGame.h>
#include <game_sa/CTimer.h>
#include <game_sa/CPed.h>
#include <game_sa/CVehicle.h>
#include <game_sa/CEntity.h>
#include <game_sa/CVector.h>
#include <game_sa/CMenuManager.h>
#include <game_sa/CCutsceneMgr.h>

// NOTA DE COMPILACAO: CRect, CRGBA e o global "FrontEndMenuManager" (usados
// na SECAO 7) normalmente ja vêm junto de <game_sa/CMenuManager.h> acima.
// Se o compilador reclamar de "CRect"/"CRGBA" nao declarados, adicione aqui
// o include correspondente da sua copia do plugin-sdk (em algumas versoes
// fica em "shared/CRGBA.h" e "shared/CRect.h", em outras em "game_sa/").
// Se "FrontEndMenuManager" nao for encontrado, procure pelo nome exato do
// ponteiro/referencia global de CMenuManager no seu CMenuManager.h local
// (ex.: pode ser "*FrontEndMenuManager", um ponteiro, em vez de referencia -
// nesse caso troque os "." por "->" nas chamadas da SECAO 7).

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <string>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <map>

using namespace plugin;

// ========================================================================
//  SECAO 1 - CONSTANTES, ENDERECOS CONFIRMADOS E PEQUENOS UTILITARIOS
// ========================================================================

namespace IsoAddr
{
    // Bytes de "modo de camera atual" usados pelo jogo (1.0 US).
    // O jogo incrementa esses bytes sozinho sempre que o player aperta o
    // botao nativo de mudar camera (seja qual for a tecla/botao que ele
    // tiver configurado). Nos apenas OBSERVAMOS a mudanca de valor para
    // saber que o botao foi pressionado; nunca escrevemos nesses bytes,
    // para nao arriscar corromper o estado interno do jogo.
    static volatile BYTE* const PedCameraMode     = reinterpret_cast<volatile BYTE*>(0x00B6F0F0);
    static volatile BYTE* const VehicleCameraMode = reinterpret_cast<volatile BYTE*>(0x00B6F0DC);
}

// Pagina do menu de pausa correspondente a aba "Controles" (CONFIGURATION /
// Redefine Controls) no PC. Confirmado no enum eMenuScreen do plugin-sdk
// (MENUPAGE_CONTROLLER_PC = 36). As paginas seguintes (37-40) sao as
// sub-telas de redefinicao de controles; tratamos todas como "estamos na
// secao de controles" para decidir quando desenhar nosso painel.
static const int MENUPAGE_CONTROLLER_PC_ID      = 36;
static const int MENUPAGE_REDEFINE_CONTROLS_ID  = 37;
static const int MENUPAGE_KEYBOARD_CONTROLS_ID  = 38;
static const int MENUPAGE_MOUSE_SETTINGS_ID     = 39;
static const int MENUPAGE_JOYPAD_SETTINGS_ID    = 40;

static inline bool IsOnControlsPage(int currScreen)
{
    return currScreen == MENUPAGE_CONTROLLER_PC_ID     ||
           currScreen == MENUPAGE_REDEFINE_CONTROLS_ID ||
           currScreen == MENUPAGE_KEYBOARD_CONTROLS_ID ||
           currScreen == MENUPAGE_MOUSE_SETTINGS_ID    ||
           currScreen == MENUPAGE_JOYPAD_SETTINGS_ID;
}

static const float PI_F = 3.14159265358979323846f;
static inline float DegToRad(float d) { return d * (PI_F / 180.0f); }
static inline float RadToDeg(float r) { return r * (180.0f / PI_F); }

static inline float Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

// Interpola angulos (graus) sempre pelo caminho mais curto, evitando que
// o giro de 350 graus para 10 graus passe "a volta toda" em vez de +20.
static inline float LerpAngleDeg(float current, float target, float t)
{
    float diff = std::fmod(target - current + 540.0f, 360.0f) - 180.0f;
    return current + diff * t;
}

static inline float ClampF(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}


// ========================================================================
//  SECAO 2 - LOG COMPLETO (arquivo IsoCam.log na pasta do mod)
// ========================================================================

enum class LogLevel { LDEBUG, LINFO, LWARN, LERROR };

class Logger
{
public:
    void Init(const std::string& path, bool verbose)
    {
        m_verbose = verbose;
        m_file.open(path, std::ios::out | std::ios::app);
        Info("========================================================");
        Info("IsoCam iniciado. Log aberto em: %s", path.c_str());
    }

    void SetVerbose(bool verbose) { m_verbose = verbose; }

    void Debug(const char* fmt, ...) { if (m_verbose) { va_list a; va_start(a, fmt); Write(LogLevel::LDEBUG, fmt, a); va_end(a); } }
    void Info (const char* fmt, ...) { va_list a; va_start(a, fmt); Write(LogLevel::LINFO,  fmt, a); va_end(a); }
    void Warn (const char* fmt, ...) { va_list a; va_start(a, fmt); Write(LogLevel::LWARN,  fmt, a); va_end(a); }
    void Error(const char* fmt, ...) { va_list a; va_start(a, fmt); Write(LogLevel::LERROR, fmt, a); va_end(a); }

private:
    void Write(LogLevel lvl, const char* fmt, va_list args)
    {
        if (!m_file.is_open())
            return;

        char buf[1024];
        vsnprintf(buf, sizeof(buf), fmt, args);

        SYSTEMTIME st;
        GetLocalTime(&st);

        const char* tag =
            lvl == LogLevel::LDEBUG ? "DEBUG" :
            lvl == LogLevel::LINFO  ? "INFO " :
            lvl == LogLevel::LWARN  ? "AVISO" : "ERRO ";

        m_file << "[" << std::setfill('0')
               << std::setw(2) << st.wHour   << ":"
               << std::setw(2) << st.wMinute << ":"
               << std::setw(2) << st.wSecond << "."
               << std::setw(3) << st.wMilliseconds
               << "] [" << tag << "] " << buf << "\n";
        m_file.flush(); // flush imediato: se o jogo crashar, nao perdemos a ultima linha
    }

    std::ofstream m_file;
    bool m_verbose = true;
};

static Logger g_log;


// ========================================================================
//  SECAO 3 - DETECCAO DE OUTROS PLUGINS .ASI RODANDO JUNTO
// ========================================================================

static std::string GetModuleDirectory(HMODULE hModule)
{
    char path[MAX_PATH] = { 0 };
    GetModuleFileNameA(hModule, path, MAX_PATH);
    std::string s(path);
    size_t pos = s.find_last_of("\\/");
    return (pos == std::string::npos) ? std::string(".") : s.substr(0, pos);
}

// Varre os modulos (DLLs/ASI) atualmente carregados no processo do jogo e
// registra no log todos os arquivos .asi encontrados em memoria. Isso
// ajuda a identificar conflitos com outros mods (ex: dois mods tentando
// controlar a camera ao mesmo tempo).
static void LogLoadedAsiModules()
{
    g_log.Info("Escaneando modulos carregados no processo (procurando .asi)...");

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE)
    {
        g_log.Warn("Nao foi possivel tirar snapshot dos modulos (Toolhelp32). Erro: %lu", GetLastError());
        return;
    }

    MODULEENTRY32 me;
    me.dwSize = sizeof(me);

    int totalAsi = 0;
    if (Module32First(snap, &me))
    {
        do
        {
            std::string name = me.szModule;
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });

            if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".asi") == 0)
            {
                totalAsi++;
                g_log.Info("  [ASI em memoria] %-28s base=0x%p  caminho=%s",
                           me.szModule, (void*)me.modBaseAddr, me.szExePath);
            }
        } while (Module32Next(snap, &me));
    }
    else
    {
        g_log.Warn("Module32First falhou. Erro: %lu", GetLastError());
    }

    CloseHandle(snap);
    g_log.Info("Total de plugins .asi detectados em memoria: %d", totalAsi);
}

// Alem de olhar o que esta carregado em memoria, listamos tambem os
// arquivos .asi presentes na raiz da pasta do jogo (podem existir mods
// desativados, com extensao trocada, etc. - util para comparar).
static void LogAsiFilesOnDisk(const std::string& gameDir)
{
    g_log.Info("Listando arquivos .asi na pasta do jogo: %s", gameDir.c_str());

    std::string pattern = gameDir + "\\*.asi";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);

    int total = 0;
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            g_log.Info("  [arquivo em disco] %s", fd.cFileName);
            total++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    g_log.Info("Total de arquivos .asi na pasta do jogo: %d", total);
}


// ========================================================================
//  SECAO 4 - CONFIGURACAO (IsoCam.ini ao lado do .asi)
// ========================================================================

struct IsoCamConfig
{
    bool  enabled               = true;   // liga/desliga o mod inteiro

    // Geometria da camera isometrica
    float distanceOnFoot        = 12.0f;  // distancia da camera ao player, a pe
    float distanceInVehicle     = 18.0f;  // distancia da camera ao veiculo
    float minDistance           = 5.0f;
    float maxDistance           = 40.0f;
    float distanceStep          = 1.5f;   // quanto cada toque +/- altera
    float elevationDeg          = 35.264f;// angulo de elevacao (isometrico "verdadeiro")
    float smoothSpeed           = 8.0f;   // velocidade da interpolacao (maior = mais rapido)

    // Modo da camera isometrica dentro do veiculo
    //   0 = Flexivel (acompanha a traseira do carro)
    //   1 = Fixa     (mantem a direcao do mundo escolhida a pe)
    int   vehicleCamMode        = 0;

    // Quantidade de modos de camera NATIVOS do jogo antes do nosso modo
    // extra entrar no ciclo. Precisa ser calibrado: ative o log, jogue,
    // va apertando o botao de mudar camera e veja no log quantos valores
    // diferentes aparecem para PedCameraMode e VehicleCameraMode antes de
    // o ciclo nativo repetir. Veja o README.md.
    int   nativeFootCamCount    = 2;
    int   nativeVehicleCamCount = 3;

    // Interiores
    bool  interiorAutoAdjust    = true;
    float interiorDistanceScale = 0.45f;

    // Janela de tempo (ms) em que ignoramos mudancas no byte de modo de
    // camera logo apos entrar/sair de um veiculo (o jogo realinha esse
    // valor sozinho nessas horas, o que nao deve contar como "apertou o
    // botao").
    int   ignoreWindowMs        = 350;

    // Pagina do menu de pausa correspondente a secao "Controles"
    int   menuPageId            = MENUPAGE_CONTROLLER_PC_ID;

    // Teclas (Virtual-Key codes do Windows)
    int   keyToggleMod          = VK_F6;      // liga/desliga o mod inteiro (emergencia)
    int   keyRotateNorth        = VK_NUMPAD8;
    int   keyRotateEast         = VK_NUMPAD6;
    int   keyRotateSouth        = VK_NUMPAD2;
    int   keyRotateWest         = VK_NUMPAD4;
    int   keyDistanceIn         = VK_NUMPAD7; // aproxima a camera
    int   keyDistanceOut        = VK_NUMPAD9; // afasta a camera
    int   keyToggleVehicleMode  = VK_F7;      // alterna Flexivel/Fixa (tambem ajustavel no menu)

    bool  verboseLog            = true;
};

static IsoCamConfig g_cfg;

static int ReadIntINI(const char* section, const char* key, int def, const char* file)
{
    return GetPrivateProfileIntA(section, key, def, file);
}

static float ReadFloatINI(const char* section, const char* key, float def, const char* file)
{
    char buf[64];
    char defStr[64];
    snprintf(defStr, sizeof(defStr), "%.3f", def);
    GetPrivateProfileStringA(section, key, defStr, buf, sizeof(buf), file);
    return (float)atof(buf);
}

static bool ReadBoolINI(const char* section, const char* key, bool def, const char* file)
{
    return ReadIntINI(section, key, def ? 1 : 0, file) != 0;
}

static void WriteIntINI(const char* section, const char* key, int value, const char* file)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    WritePrivateProfileStringA(section, key, buf, file);
}

static void WriteFloatINI(const char* section, const char* key, float value, const char* file)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", value);
    WritePrivateProfileStringA(section, key, buf, file);
}

static std::string g_iniPath;

static void LoadConfig(const std::string& iniPath)
{
    const char* f = iniPath.c_str();

    g_cfg.enabled               = ReadBoolINI ("IsoCam", "Enabled",               g_cfg.enabled,               f);
    g_cfg.distanceOnFoot        = ReadFloatINI("IsoCam", "DistanceOnFoot",        g_cfg.distanceOnFoot,        f);
    g_cfg.distanceInVehicle     = ReadFloatINI("IsoCam", "DistanceInVehicle",     g_cfg.distanceInVehicle,     f);
    g_cfg.minDistance           = ReadFloatINI("IsoCam", "MinDistance",           g_cfg.minDistance,           f);
    g_cfg.maxDistance           = ReadFloatINI("IsoCam", "MaxDistance",           g_cfg.maxDistance,           f);
    g_cfg.distanceStep          = ReadFloatINI("IsoCam", "DistanceStep",          g_cfg.distanceStep,          f);
    g_cfg.elevationDeg          = ReadFloatINI("IsoCam", "ElevationDeg",          g_cfg.elevationDeg,          f);
    g_cfg.smoothSpeed           = ReadFloatINI("IsoCam", "SmoothSpeed",           g_cfg.smoothSpeed,           f);
    g_cfg.vehicleCamMode        = ReadIntINI  ("IsoCam", "VehicleCamMode",        g_cfg.vehicleCamMode,        f);
    g_cfg.nativeFootCamCount    = ReadIntINI  ("IsoCam", "NativeFootCamCount",    g_cfg.nativeFootCamCount,    f);
    g_cfg.nativeVehicleCamCount = ReadIntINI  ("IsoCam", "NativeVehicleCamCount", g_cfg.nativeVehicleCamCount, f);
    g_cfg.interiorAutoAdjust    = ReadBoolINI ("IsoCam", "InteriorAutoAdjust",    g_cfg.interiorAutoAdjust,    f);
    g_cfg.interiorDistanceScale = ReadFloatINI("IsoCam", "InteriorDistanceScale", g_cfg.interiorDistanceScale, f);
    g_cfg.ignoreWindowMs        = ReadIntINI  ("IsoCam", "IgnoreWindowMs",        g_cfg.ignoreWindowMs,        f);
    g_cfg.menuPageId            = ReadIntINI  ("IsoCam", "MenuPageId",           g_cfg.menuPageId,            f);

    g_cfg.keyToggleMod          = ReadIntINI("Keys", "ToggleMod",         g_cfg.keyToggleMod,         f);
    g_cfg.keyRotateNorth        = ReadIntINI("Keys", "RotateNorth",       g_cfg.keyRotateNorth,       f);
    g_cfg.keyRotateEast         = ReadIntINI("Keys", "RotateEast",        g_cfg.keyRotateEast,        f);
    g_cfg.keyRotateSouth        = ReadIntINI("Keys", "RotateSouth",       g_cfg.keyRotateSouth,       f);
    g_cfg.keyRotateWest         = ReadIntINI("Keys", "RotateWest",        g_cfg.keyRotateWest,        f);
    g_cfg.keyDistanceIn         = ReadIntINI("Keys", "DistanceIn",        g_cfg.keyDistanceIn,        f);
    g_cfg.keyDistanceOut        = ReadIntINI("Keys", "DistanceOut",       g_cfg.keyDistanceOut,       f);
    g_cfg.keyToggleVehicleMode  = ReadIntINI("Keys", "ToggleVehicleMode", g_cfg.keyToggleVehicleMode, f);

    g_cfg.verboseLog            = ReadBoolINI("IsoCam", "VerboseLog", g_cfg.verboseLog, f);

    g_log.Info("Config carregada de %s", iniPath.c_str());
    g_log.Info("  DistanceOnFoot=%.2f DistanceInVehicle=%.2f ElevationDeg=%.2f SmoothSpeed=%.2f",
               g_cfg.distanceOnFoot, g_cfg.distanceInVehicle, g_cfg.elevationDeg, g_cfg.smoothSpeed);
    g_log.Info("  VehicleCamMode=%d (0=Flexivel,1=Fixa) NativeFootCamCount=%d NativeVehicleCamCount=%d",
               g_cfg.vehicleCamMode, g_cfg.nativeFootCamCount, g_cfg.nativeVehicleCamCount);
}

static void SaveVehicleCamMode()
{
    WriteIntINI("IsoCam", "VehicleCamMode", g_cfg.vehicleCamMode, g_iniPath.c_str());
}

static void SaveDistanceInVehicle()
{
    WriteFloatINI("IsoCam", "DistanceInVehicle", g_cfg.distanceInVehicle, g_iniPath.c_str());
}

static void SaveDistanceOnFoot()
{
    WriteFloatINI("IsoCam", "DistanceOnFoot", g_cfg.distanceOnFoot, g_iniPath.c_str());
}


// ========================================================================
//  SECAO 5 - DEBOUNCE SIMPLES DE TECLAS (evita repetir 60x por segundo)
// ========================================================================

class KeyDebouncer
{
public:
    // Retorna true apenas na transicao "solta -> pressionada", respeitando
    // um cooldown minimo em milissegundos entre disparos repetidos.
    bool JustPressed(int vk, DWORD cooldownMs = 150)
    {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        DWORD now = GetTickCount();

        State& st = m_state[vk];
        bool fired = false;

        if (down && !st.wasDown && (now - st.lastFire) >= cooldownMs)
        {
            fired = true;
            st.lastFire = now;
        }
        st.wasDown = down;
        return fired;
    }

private:
    struct State { bool wasDown = false; DWORD lastFire = 0; };
    std::map<int, State> m_state;
};

static KeyDebouncer g_keys;


// ========================================================================
//  SECAO 6 - ESTADO E LOGICA DA CAMERA ISOMETRICA
// ========================================================================

enum IsoSide : int { ISO_NORTH = 0, ISO_EAST = 90, ISO_SOUTH = 180, ISO_WEST = 270 };

struct IsoCamState
{
    bool  modEnabled        = true;   // controlado pelo keyToggleMod

    // "Extended index" dos ciclos de camera (a pe / veiculo). Quando o
    // indice chega no ultimo valor (== contagem nativa), estamos no modo
    // isometrico. Ver comentario na SECAO 1 sobre PedCameraMode/VehicleCameraMode.
    int   footExtIndex      = 0;
    int   vehExtIndex       = 0;
    bool  isoActiveFoot     = false;
    bool  isoActiveVehicle  = false;

    BYTE  lastPedByte       = 0;
    BYTE  lastVehByte       = 0;
    bool  bytesInitialised  = false;

    // Direcao isometrica "do mundo" escolhida pelo player (a pe e no modo
    // veicular "Fixa" compartilham esse valor).
    float worldAzimuthTarget  = (float)ISO_SOUTH; // olhando o player de frente por padrao
    float currentAzimuth      = (float)ISO_SOUTH;
    float currentDistance     = 12.0f;

    DWORD lastTickMs         = 0;

    // Controle de "janela de ignorar" ao entrar/sair de veiculo
    DWORD vehicleTransitionAt = 0;
    bool  wasInVehicleLastFrame = false;

    bool  loggedEnterIsoFoot = false;
    bool  loggedEnterIsoVeh  = false;
};

static IsoCamState g_state;

// Calcula o vetor (a partir do alvo) de onde a camera deve ficar, dado um
// azimute (0=Norte,90=Leste,180=Sul,270=Oeste), uma elevacao em graus e
// uma distancia.
static CVector ComputeIsoOffset(float azimuthDeg, float elevationDeg, float distance)
{
    float az = DegToRad(azimuthDeg);
    float el = DegToRad(elevationDeg);

    CVector dir;
    dir.x = std::sin(az) * std::cos(el);
    dir.y = std::cos(az) * std::cos(el);
    dir.z = std::sin(el);

    return CVector(dir.x * distance, dir.y * distance, dir.z * distance);
}

// Tenta obter o heading (graus) de um veiculo a partir da matriz de
// orientacao (vetor "at"/frente). O sinal/eixo pode precisar de ajuste
// fino conforme a convencao exata do CMatrix usado no seu plugin-sdk;
// se a camera "Flexivel" girar para o lado errado, inverta o sinal do
// atan2 abaixo (troque -at.x por at.x).
static float GetVehicleHeadingDeg(CVehicle* veh)
{
    if (!veh || !veh->m_matrix)
        return 0.0f;

    const CVector& at = veh->m_matrix->at; // vetor "para frente" do veiculo
    return RadToDeg(std::atan2(-at.x, at.y));
}

// Verifica se, neste exato frame, e seguro mexer na camera (sem cutscene
// em andamento e sem o menu de pausa aberto). Mantemos a regra simples e
// conservadora de proposito: qualquer uma dessas duas condicoes faz a
// camera isometrica "congelar" na ultima posicao calculada e devolver o
// quadro para o comportamento nativo do jogo, exatamente como a camera
// normal tambem se comporta nessas situacoes. As teclas de ajuste (giro,
// distancia, Flexivel/Fixa) continuam funcionando mesmo com o menu
// aberto - e assim que o player consegue ajustar as opcoes enquanto ve o
// painel na secao Controles (SECAO 7); so a reposicao da camera em si
// fica pausada nesse meio tempo.
static bool IsSafeToOverrideCamera()
{
    if (CCutsceneMgr::IsCutsceneProcessing())
        return false;

    if (FrontEndMenuManager.m_bMenuActive)
        return false;

    return true;
}

// Aplica de fato a posicao/orientacao calculada na camera do jogo.
static void ApplyIsoCamera(const CVector& targetPos, float azimuthDeg, float distance)
{
    CVector offset = ComputeIsoOffset(azimuthDeg, g_cfg.elevationDeg, distance);
    CVector camPos = targetPos + offset;
    CVector upOffset(0.0f, 0.0f, 0.0f);

    // Posiciona a camera (modo "fixo" nativo do jogo, o mesmo mecanismo
    // usado por scripts de missao para cutscenes com camera parada).
    TheCamera.SetCamPositionForFixedMode(camPos, upOffset);

    // Aponta a camera para o alvo (player/veiculo). Usamos "jump cut" (1)
    // porque QUEM faz a suavizacao somos nos mesmos, interpolando azimute
    // e distancia antes de chegar aqui - deixar o motor interpolar de
    // novo por cima so deixaria o movimento mais lento/estranho.
    const int SWITCHTYPE_JUMPCUT = 1; // confira o enum SWITCHTYPE no seu CCamera.h se notar comportamento estranho
    const int LOOKAT_MODE        = 1;
    CVector lookAt = targetPos;
    TheCamera.TakeControlNoEntity(&lookAt, SWITCHTYPE_JUMPCUT, LOOKAT_MODE);
}

// Le os bytes nativos de modo de camera e decide se o player "apertou" o
// botao de mudar camera (a pe ou em veiculo), devolvendo true quando uma
// mudanca de estado isometrico (entrar/sair) acabou de acontecer.
static void UpdateCameraModeCycling(bool playerInVehicle)
{
    BYTE pedByte = *IsoAddr::PedCameraMode;
    BYTE vehByte = *IsoAddr::VehicleCameraMode;

    if (!g_state.bytesInitialised)
    {
        g_state.lastPedByte = pedByte;
        g_state.lastVehByte = vehByte;
        g_state.bytesInitialised = true;
        return;
    }

    DWORD now = GetTickCount();
    bool insideIgnoreWindow = (now - g_state.vehicleTransitionAt) < (DWORD)g_cfg.ignoreWindowMs;

    if (!playerInVehicle)
    {
        if (pedByte != g_state.lastPedByte && !insideIgnoreWindow)
        {
            int cycleLen = g_cfg.nativeFootCamCount + 1; // +1 = nosso slot isometrico
            g_state.footExtIndex = (g_state.footExtIndex + 1) % cycleLen;
            bool wasActive = g_state.isoActiveFoot;
            g_state.isoActiveFoot = (g_state.footExtIndex == g_cfg.nativeFootCamCount);

            if (g_state.isoActiveFoot && !wasActive)
                g_log.Info("[A PE] Camera isometrica ATIVADA (footExtIndex=%d).", g_state.footExtIndex);
            else if (!g_state.isoActiveFoot && wasActive)
            {
                g_log.Info("[A PE] Camera isometrica DESATIVADA, devolvendo controle ao jogo.");
                TheCamera.RestoreWithJumpCut();
            }
        }
    }
    else
    {
        if (vehByte != g_state.lastVehByte && !insideIgnoreWindow)
        {
            int cycleLen = g_cfg.nativeVehicleCamCount + 1;
            g_state.vehExtIndex = (g_state.vehExtIndex + 1) % cycleLen;
            bool wasActive = g_state.isoActiveVehicle;
            g_state.isoActiveVehicle = (g_state.vehExtIndex == g_cfg.nativeVehicleCamCount);

            if (g_state.isoActiveVehicle && !wasActive)
                g_log.Info("[VEICULO] Camera isometrica ATIVADA (vehExtIndex=%d, modo=%s).",
                           g_state.vehExtIndex, g_cfg.vehicleCamMode == 0 ? "Flexivel" : "Fixa");
            else if (!g_state.isoActiveVehicle && wasActive)
            {
                g_log.Info("[VEICULO] Camera isometrica DESATIVADA, devolvendo controle ao jogo.");
                TheCamera.RestoreWithJumpCut();
            }
        }
    }

    g_state.lastPedByte = pedByte;
    g_state.lastVehByte = vehByte;
}

// Le as teclas de rotacao (4 lados) e de distancia, atualizando os alvos
// de interpolacao. Nao aplica nada diretamente na camera aqui.
static void UpdateHotkeys(bool playerInVehicle, bool isoActiveNow)
{
    // Toggle geral do mod - sempre ativo, mesmo com a camera isometrica
    // desligada, para servir como "botao de panico".
    if (g_keys.JustPressed(g_cfg.keyToggleMod, 400))
    {
        g_state.modEnabled = !g_state.modEnabled;
        g_log.Info("Mod IsoCam %s via tecla de atalho.", g_state.modEnabled ? "ATIVADO" : "DESATIVADO");
        if (!g_state.modEnabled)
            TheCamera.RestoreWithJumpCut();
    }

    if (!g_state.modEnabled)
        return;

    // Giro entre os 4 lados (funciona a pe sempre; em veiculo so quando o
    // modo "Fixa" esta selecionado, ja que no "Flexivel" quem manda na
    // direcao e o proprio carro).
    bool canRotateManually = !playerInVehicle || g_cfg.vehicleCamMode == 1;
    if (canRotateManually)
    {
        if (g_keys.JustPressed(g_cfg.keyRotateNorth, 150)) { g_state.worldAzimuthTarget = (float)ISO_NORTH; g_log.Debug("Lado isometrico -> NORTE"); }
        if (g_keys.JustPressed(g_cfg.keyRotateEast , 150)) { g_state.worldAzimuthTarget = (float)ISO_EAST;  g_log.Debug("Lado isometrico -> LESTE");  }
        if (g_keys.JustPressed(g_cfg.keyRotateSouth, 150)) { g_state.worldAzimuthTarget = (float)ISO_SOUTH; g_log.Debug("Lado isometrico -> SUL");   }
        if (g_keys.JustPressed(g_cfg.keyRotateWest , 150)) { g_state.worldAzimuthTarget = (float)ISO_WEST;  g_log.Debug("Lado isometrico -> OESTE"); }
    }

    // Distancia de visao (+ / -). Funciona a qualquer momento (inclusive
    // com o menu de pausa aberto na secao Controles), para que o player
    // veja os valores do painel (SECAO 7) mudando em tempo real.
    {
        float* dist = playerInVehicle ? &g_cfg.distanceInVehicle : &g_cfg.distanceOnFoot;
        bool changed = false;

        if (g_keys.JustPressed(g_cfg.keyDistanceIn, 90))
        {
            *dist = ClampF(*dist - g_cfg.distanceStep, g_cfg.minDistance, g_cfg.maxDistance);
            changed = true;
        }
        if (g_keys.JustPressed(g_cfg.keyDistanceOut, 90))
        {
            *dist = ClampF(*dist + g_cfg.distanceStep, g_cfg.minDistance, g_cfg.maxDistance);
            changed = true;
        }

        if (changed)
        {
            g_log.Debug("Distancia isometrica (%s) -> %.2f", playerInVehicle ? "veiculo" : "a pe", *dist);
            playerInVehicle ? SaveDistanceInVehicle() : SaveDistanceOnFoot();
        }
    }

    // Alterna Flexivel/Fixa (tambem disponivel como opcao no menu de pausa, secao Controles)
    if (playerInVehicle && g_keys.JustPressed(g_cfg.keyToggleVehicleMode, 250))
    {
        g_cfg.vehicleCamMode = 1 - g_cfg.vehicleCamMode;
        SaveVehicleCamMode();
        g_log.Info("Modo de camera veicular isometrica alterado para: %s",
                   g_cfg.vehicleCamMode == 0 ? "Flexivel" : "Fixa");
    }
}

// Logica principal, chamada uma vez por frame, DEPOIS do processamento
// normal do jogo (Events::gameProcessEvent.after), ou seja, depois que a
// camera nativa ja calculou sua posicao "normal" para este frame. Assim
// nossa sobrescrita sempre tem a ultima palavra quando o modo isometrico
// esta ativo, sem brigar com o calculo nativo no meio do caminho.
static void UpdateInternal()
{
    DWORD now = GetTickCount();
    float dt = g_state.lastTickMs == 0 ? 0.016f : (now - g_state.lastTickMs) / 1000.0f;
    dt = ClampF(dt, 0.0f, 0.1f); // evita saltos grandes apos hitches/loading
    g_state.lastTickMs = now;

    CPed* player = FindPlayerPed();
    if (!player)
        return;

    CVehicle* vehicle = FindPlayerVehicle();
    bool playerInVehicle = (vehicle != nullptr);

    // Deteccao de transicao entrar/sair de veiculo, para abrir a "janela
    // de ignorar" mudancas espurias no byte de modo de camera.
    if (playerInVehicle != g_state.wasInVehicleLastFrame)
    {
        g_state.vehicleTransitionAt = now;
        g_log.Debug("Transicao de veiculo detectada (agora %s veiculo). Ignorando mudancas de modo de camera por %d ms.",
                    playerInVehicle ? "DENTRO do" : "FORA do", g_cfg.ignoreWindowMs);
    }
    g_state.wasInVehicleLastFrame = playerInVehicle;

    UpdateCameraModeCycling(playerInVehicle);

    bool isoActiveNow = playerInVehicle ? g_state.isoActiveVehicle : g_state.isoActiveFoot;

    UpdateHotkeys(playerInVehicle, isoActiveNow);

    if (!isoActiveNow || !g_state.modEnabled)
        return;

    if (!IsSafeToOverrideCamera())
    {
        g_log.Debug("Frame ignorado pela camera isometrica (cutscene/script camera ativa).");
        return;
    }

    // --- Define o alvo (posicao a olhar) e o azimute desejado ---
    CVector targetPos;
    float targetAzimuth;
    float targetDistance;
    bool inInterior = (CGame::currArea != 0);

    if (playerInVehicle)
    {
        targetPos = vehicle->GetPosition();
        targetPos.z += 1.0f; // mira um pouco acima do centro do veiculo

        if (g_cfg.vehicleCamMode == 0) // Flexivel: acompanha a traseira do carro
        {
            targetAzimuth = GetVehicleHeadingDeg(vehicle) + 180.0f;
        }
        else // Fixa: mantem a direcao do mundo escolhida pelo player
        {
            targetAzimuth = g_state.worldAzimuthTarget;
        }

        targetDistance = g_cfg.distanceInVehicle;

        if (!g_state.loggedEnterIsoVeh)
        {
            g_log.Info("Camera isometrica veicular comecou a renderizar (modo=%s).",
                       g_cfg.vehicleCamMode == 0 ? "Flexivel" : "Fixa");
            g_state.loggedEnterIsoVeh = true;
        }
        g_state.loggedEnterIsoFoot = false;
    }
    else
    {
        targetPos = player->GetPosition();
        targetPos.z += 0.9f; // mira na altura do torso/peito

        targetAzimuth = g_state.worldAzimuthTarget;
        targetDistance = g_cfg.distanceOnFoot;

        if (!g_state.loggedEnterIsoFoot)
        {
            g_log.Info("Camera isometrica a pe comecou a renderizar.");
            g_state.loggedEnterIsoFoot = true;
        }
        g_state.loggedEnterIsoVeh = false;
    }

    // Adaptacao automatica para interiores: reduz a distancia para evitar
    // atravessar paredes/tetos de comodos pequenos.
    if (inInterior && g_cfg.interiorAutoAdjust)
        targetDistance *= g_cfg.interiorDistanceScale;

    // --- Interpolacao suave em direcao ao alvo ---
    float t = ClampF(dt * g_cfg.smoothSpeed, 0.0f, 1.0f);
    g_state.currentAzimuth  = LerpAngleDeg(g_state.currentAzimuth, targetAzimuth, t);
    g_state.currentDistance = Lerp(g_state.currentDistance, targetDistance, t);

    ApplyIsoCamera(targetPos, g_state.currentAzimuth, g_state.currentDistance);
}

// Wrapper com protecao contra excecoes estruturadas (SEH). Como lemos e
// escrevemos em enderecos de memoria "crus", um deslocamento de versao
// do jogo poderia causar um Access Violation; aqui capturamos isso,
// registramos no log e desligamos a camera isometrica neste frame em vez
// de derrubar o jogo inteiro.
static void SafeUpdate()
{
    __try
    {
        UpdateInternal();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DWORD code = GetExceptionCode();
        g_log.Error("EXCECAO capturada em IsoCam::UpdateInternal (codigo 0x%08lX). "
                     "A camera isometrica foi desativada neste frame por seguranca. "
                     "Isso geralmente indica que um endereco de memoria nao bate com "
                     "sua copia do jogo/plugin-sdk - revise o README.md.", code);
        g_state.isoActiveFoot = false;
        g_state.isoActiveVehicle = false;
    }
}


// ========================================================================
//  SECAO 7 - PAINEL NO MENU DE PAUSA (secao Controles)
// ========================================================================

// Desenha um pequeno painel informativo/editavel sobre a camera
// isometrica quando o player esta na pagina "Controles" do menu de pausa.
// Usamos teclas dedicadas (nao as setas nativas do menu) para nao brigar
// com a navegacao normal do jogo nessa tela.
static void DrawPauseMenuPanel()
{
    if (!FrontEndMenuManager.m_bMenuActive)
        return;

    if (!IsOnControlsPage(FrontEndMenuManager.m_nCurrScreen))
        return;

    char line1[128];
    char line2[128];
    char line3[128];
    snprintf(line1, sizeof(line1), "IsoCam - Camera Isometrica (F6 liga/desliga: %s)",
             g_state.modEnabled ? "ATIVO" : "DESATIVADO");
    snprintf(line2, sizeof(line2), "Veiculo: [F7] Modo = %s",
             g_cfg.vehicleCamMode == 0 ? "Flexivel (acompanha o carro)" : "Fixa (direcao do mundo)");
    snprintf(line3, sizeof(line3), "Distancia: [Numpad7/9] A pe=%.1f  Veiculo=%.1f  (min %.1f / max %.1f)",
             g_cfg.distanceOnFoot, g_cfg.distanceInVehicle, g_cfg.minDistance, g_cfg.maxDistance);

    // CRect(esquerda, topo, direita, base) em coordenadas de tela do menu.
    // Os valores abaixo posicionam o painel no canto inferior esquerdo;
    // ajuste conforme a resolucao/layout do seu menu se necessario.
    CRect box(20.0f, 400.0f, 420.0f, 470.0f);
    CRGBA bg(0, 0, 0, 180);

    FrontEndMenuManager.DrawWindow(box, const_cast<char*>(line1), 1, bg, 0, 0);
    // Linhas adicionais desenhadas como "janelas" extras logo abaixo da primeira.
    CRect box2(20.0f, 430.0f, 420.0f, 450.0f);
    CRect box3(20.0f, 450.0f, 420.0f, 470.0f);
    FrontEndMenuManager.DrawWindow(box2, const_cast<char*>(line2), 1, bg, 0, 0);
    FrontEndMenuManager.DrawWindow(box3, const_cast<char*>(line3), 1, bg, 0, 0);
}


// ========================================================================
//  SECAO 8 - INICIALIZACAO DO PLUGIN
// ========================================================================

class IsoCamPlugin
{
public:
    IsoCamPlugin()
    {
        Events::initGameEvent += [] { OnGameInit(); };
        Events::gameProcessEvent.after += [] { SafeUpdate(); DrawPauseMenuPanel(); };
    }

private:
    static void OnGameInit()
    {
        HMODULE hSelf = GetModuleHandleA("IsoCam.asi");
        std::string dir = GetModuleDirectory(hSelf);

        g_iniPath = dir + "\\IsoCam.ini";
        std::string logPath = dir + "\\IsoCam.log";

        // O log e aberto antes de carregar a config para podermos
        // registrar o proprio carregamento da config.
        g_log.Init(logPath, true);

        LoadConfig(g_iniPath);
        g_log.SetVerbose(g_cfg.verboseLog); // ajusta o nivel de log conforme a config, sem reabrir o arquivo

        g_state.currentDistance   = g_cfg.distanceOnFoot;
        g_state.currentAzimuth    = g_state.worldAzimuthTarget;
        g_state.modEnabled        = g_cfg.enabled;

        g_log.Info("GTA SA 1.0 US detectado pelo plugin-sdk. IsoCam pronto.");
        LogLoadedAsiModules();
        LogAsiFilesOnDisk(dir);
    }
};

static IsoCamPlugin g_isoCamPlugin;
