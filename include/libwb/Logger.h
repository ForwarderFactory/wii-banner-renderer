/*
Copyright (c) 2026 - Jacob Nilsson

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/

#pragma once

#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <stdexcept>
#include <cstddef>

namespace wb {
    enum class LogType {
        Notice,
        Warning,
        Error,
    };

    inline std::ostream& operator<<(std::ostream& os, LogType type) {
        switch (type) {
            case LogType::Notice:  return os << "Notice";
            case LogType::Warning: return os << "Warning";
            case LogType::Error:   return os << "Error";
        }

        return os << "Unknown";
    }

    struct LogEntry {
        LogType type{LogType::Notice};
        std::string text;
    };

    class Logger {
        std::vector<LogEntry> log;

    public:
        Logger() = default;

        [[nodiscard]] const LogEntry& top() const {
            if (log.empty()) {
                throw std::out_of_range("logger: empty");
            }

            return log.back();
        }

        [[nodiscard]] const LogEntry& at(std::size_t index) const {
            return log.at(index);
        }

        const LogEntry& operator[](std::size_t index) const {
            return log[index];
        }

        std::size_t size() const {
            return log.size();
        }

        void push_back(LogEntry entry) {
            log.push_back(std::move(entry));
        }

        void clear() {
            log.clear();
        }

        template<typename... Args>
        void printf(LogType type, const char* fmt, Args... args) {
            const int length = std::snprintf(nullptr, 0, fmt, args...);

            if (length < 0) {
                throw std::runtime_error("logger: formatting failed");
            }

            std::vector<char> buffer(static_cast<std::size_t>(length) + 1);

            std::snprintf(buffer.data(), buffer.size(), fmt, args...);

            push_back({type, std::string(buffer.data(), length)});
        }

        template<typename... Args>
        void printf(const char* fmt, Args... args) {
            printf(LogType::Notice, fmt, args...);
        }

        using container_type = std::vector<LogEntry>;
        using value_type = container_type::value_type;
        using size_type = container_type::size_type;
        using iterator = container_type::iterator;
        using const_iterator = container_type::const_iterator;

        iterator begin() noexcept {
            return log.begin();
        }

        iterator end() noexcept {
            return log.end();
        }

        const_iterator begin() const noexcept {
            return log.begin();
        }

        const_iterator end() const noexcept {
            return log.end();
        }

        const_iterator cbegin() const noexcept {
            return log.cbegin();
        }

        const_iterator cend() const noexcept {
            return log.cend();
        }

        auto rbegin() noexcept {
            return log.rbegin();
        }

        auto rend() noexcept {
            return log.rend();
        }

        auto rbegin() const noexcept {
            return log.rbegin();
        }

        auto rend() const noexcept {
            return log.rend();
        }

        iterator erase(const_iterator pos) {
            return log.erase(pos);
        }

        iterator erase(const_iterator first, const_iterator last) {
            return log.erase(first, last);
        }

        void erase(size_type index) {
            log.erase(log.begin() + static_cast<container_type::difference_type>(index));
        }

        class LogStream {
            Logger& logger;
            LogType type;
            std::ostringstream stream;

        public:
            LogStream(Logger& logger, LogType type)
                : logger(logger), type(type) {}

            LogStream(const LogStream&) = delete;
            LogStream& operator=(const LogStream&) = delete;

            LogStream(LogStream&& other) noexcept
                : logger(other.logger),
                  type(other.type),
                  stream(std::move(other.stream)),
                  active(std::exchange(other.active, false)) {}

            template<typename T>
            LogStream& operator<<(T&& value) {
                stream << std::forward<T>(value);
                return *this;
            }

            using Manipulator = std::ostream& (*)(std::ostream&);
            using IOSManipulator = std::ios& (*)(std::ios&);
            using IOSBaseManipulator = std::ios_base& (*)(std::ios_base&);

            LogStream& operator<<(Manipulator manip) {
                manip(stream);
                return *this;
            }

            LogStream& operator<<(IOSManipulator manip) {
                manip(stream);
                return *this;
            }

            LogStream& operator<<(IOSBaseManipulator manip) {
                manip(stream);
                return *this;
            }

            ~LogStream() {
                if (active) {
                    logger.push_back({
                        type,
                        std::move(stream).str()
                    });
                }
            }

        private:
            bool active{true};
        };

        LogStream operator<<(LogType type) {
            return LogStream(*this, type);
        }

        LogStream operator<<(const std::string& text) {
            LogStream stream(*this, LogType::Notice);
            stream << text;
            return stream;
        }

        template<typename T>
        LogStream operator<<(T&& value) {
            LogStream stream(*this, LogType::Notice);
            stream << std::forward<T>(value);
            return stream;
        }
    };
}
