#include "Log.hpp"
#include <ctime>
#include <stdexcept>
#include <string>

namespace maptool
{
namespace
{
std::tm localTime()
{
    const auto now = std::time(nullptr);
    std::tm local{};
    if (localtime_s(&local, &now) != 0) throw std::runtime_error("failed to read the local time");
    return local;
}

std::string formatTime(const std::tm& local, const char* pattern)
{
    char text[16];
    if (std::strftime(text, sizeof text, pattern, &local) == 0)
        throw std::runtime_error("failed to format the log time");
    return text;
}

std::string locationText(const std::source_location& location)
{
    std::string file = location.file_name();
    for (char& letter : file)
        if (letter == '\\') letter = '/';
    const auto source = file.rfind("src/");
    if (source != std::string::npos) file.erase(0, source);
    return file + ":" + std::to_string(location.line()) + " " + location.function_name();
}
}

Log& Log::instance()
{
    static Log log;
    return log;
}

void Log::openFile(const std::string& date)
{
    if (file_.is_open() && date == date_) return;
    const auto path = directory_ / (date + ".log");
    if (file_.is_open()) file_.close();
    file_.clear();
    file_.open(path, std::ios::app);
    if (!file_) throw std::runtime_error("failed to open " + path.generic_string());
    date_ = date;
}

void Log::open(const std::filesystem::path& directory)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) throw std::runtime_error("failed to create " + directory.generic_string());
    auto& log = instance();
    std::lock_guard lock(log.mutex_);
    log.directory_ = directory;
    log.openFile(formatTime(localTime(), "%y%m%d"));
    log.enabled_ = true;
}

void Log::info(const std::string& message, const std::source_location& location)
{
    write("info", message, location);
}

void Log::warning(const std::string& message, const std::source_location& location)
{
    write("warning", message, location);
}

void Log::error(const std::string& message, const std::source_location& location)
{
    write("error", message, location);
}

void Log::write(const char* level, const std::string& message, const std::source_location& location)
{
    auto& log = instance();
    std::lock_guard lock(log.mutex_);
    if (!log.enabled_) return;
    const auto local = localTime();
    log.openFile(formatTime(local, "%y%m%d"));
    log.file_ << '[' << formatTime(local, "%y-%m-%d %H:%M:%S") << "] [" << level << "] [" << locationText(location) << "] " << message << '\n';
    log.file_.flush();
}

bool Log::enabled()
{
    return instance().enabled_;
}
}
