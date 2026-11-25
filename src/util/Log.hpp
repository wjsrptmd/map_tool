#pragma once
#include <filesystem>
#include <fstream>
#include <mutex>
#include <source_location>
#include <string>

namespace maptool
{
// Daily log file used while debugMode is on. Calls do nothing until open().
class Log
{
public:
    static void open(const std::filesystem::path& directory);
    static void info(const std::string& message, const std::source_location& location = std::source_location::current());
    static void warning(const std::string& message, const std::source_location& location = std::source_location::current());
    static void error(const std::string& message, const std::source_location& location = std::source_location::current());
    static bool enabled();

private:
    static Log& instance();
    static void write(const char* level, const std::string& message, const std::source_location& location);
    void openFile(const std::string& date);

    std::mutex mutex_;
    std::filesystem::path directory_;
    std::string date_;
    std::ofstream file_;
    bool enabled_ = false;
};
}
