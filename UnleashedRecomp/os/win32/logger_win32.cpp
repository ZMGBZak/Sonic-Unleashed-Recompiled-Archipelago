#include <os/logger.h>
#include <os/process.h>
#include <fstream>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>

#define FOREGROUND_WHITE  (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_YELLOW (FOREGROUND_RED | FOREGROUND_GREEN)

static HANDLE g_hStandardOutput;
static std::mutex g_apDeathLogMutex;
static std::ofstream g_apDeathLog;
static std::filesystem::path g_apSessionLogPath;

void os::logger::Init()
{
    g_hStandardOutput = GetStdHandle(STD_OUTPUT_HANDLE);

    wchar_t apExePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, apExePath, MAX_PATH))
    {
        g_apSessionLogPath =
            std::filesystem::path(apExePath).parent_path() /
            "AP_Session_Log.txt";
    }

    {
        std::lock_guard<std::mutex> lock(g_apDeathLogMutex);
        g_apDeathLog.open(g_apSessionLogPath,
            std::ios::out | std::ios::trunc);
    }
}

std::string os::logger::ExportBugReport()
{
    namespace fs = std::filesystem;

    try
    {
        std::lock_guard<std::mutex> lock(g_apDeathLogMutex);

        if (!g_apDeathLog.is_open())
            return {};

        g_apDeathLog.flush();

        wchar_t exePath[MAX_PATH] = {};
        if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH))
            return {};

        const fs::path gameDirectory = fs::path(exePath).parent_path();
        const fs::path reportDirectory = gameDirectory / "BugReports";

        fs::create_directories(reportDirectory);

        const auto now = std::chrono::system_clock::now();
        const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);

        std::tm localTime = {};
        localtime_s(&localTime, &timestamp);

        std::ostringstream filename;
        filename << "AP_BugReport_"
                 << std::put_time(&localTime, "%Y%m%d_%H%M%S")
                 << ".txt";

        fs::path destination = reportDirectory / filename.str();

        for (int index = 1; fs::exists(destination); ++index)
        {
            destination = reportDirectory /
                (fs::path(filename.str()).stem().string() +
                 "_" + std::to_string(index) + ".txt");
        }

        const fs::path source = g_apSessionLogPath;

        fs::copy_file(source, destination);

        return destination.string();
    }
    catch (...)
    {
        return {};
    }
}
void os::logger::Log(const std::string_view str, ELogType type, const char* func)
{

    {
        std::lock_guard<std::mutex> lock(g_apDeathLogMutex);
        if (g_apDeathLog.is_open())
        {
            if (func)
                g_apDeathLog << "[" << func << "] " << str << "\n";
            else
                g_apDeathLog << str << "\n";

            g_apDeathLog.flush();
        }
    }

    if (!os::process::g_consoleVisible)
        return;

    switch (type)
    {
        case ELogType::Utility:
            SetConsoleTextAttribute(g_hStandardOutput, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            break;

        case ELogType::Warning:
            SetConsoleTextAttribute(g_hStandardOutput, FOREGROUND_YELLOW | FOREGROUND_INTENSITY);
            break;

        case ELogType::Error:
            SetConsoleTextAttribute(g_hStandardOutput, FOREGROUND_RED | FOREGROUND_INTENSITY);
            break;

        default:
            SetConsoleTextAttribute(g_hStandardOutput, FOREGROUND_WHITE);
            break;
    }

    if (func)
    {
        fmt::println("[{}] {}", func, str);
    }
    else
    {
        fmt::println("{}", str);
    }

    SetConsoleTextAttribute(g_hStandardOutput, FOREGROUND_WHITE);
}
