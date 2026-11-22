#pragma once

#include <optional>
#include <string>
#include <utility>

namespace shiftxor {

enum class StatusCode {
    kOk,
    kInvalidArgument,
    kInvalidMatrix,
    kUnsupported,
    kInsufficientBlocks,
    kBufferTooSmall,
};

class Status {
public:
    Status() = default;
    Status(StatusCode code, std::string message = {})
        : code_(code), message_(std::move(message)) {}

    static Status ok() { return {}; }
    static Status error(StatusCode code, std::string message) {
        return Status(code, std::move(message));
    }

    StatusCode code() const { return code_; }
    const std::string& message() const { return message_; }
    explicit operator bool() const { return code_ == StatusCode::kOk; }

private:
    StatusCode code_ = StatusCode::kOk;
    std::string message_;
};

template <typename T>
class Result {
public:
    Result(const T& value) : value_(value), status_() {}
    Result(T&& value) : value_(std::move(value)), status_() {}
    Result(Status status) : value_(std::nullopt), status_(std::move(status)) {}

    explicit operator bool() const { return static_cast<bool>(status_); }
    const Status& status() const { return status_; }
    Status& status() { return status_; }
    const T& value() const { return *value_; }
    T& value() { return *value_; }

private:
    std::optional<T> value_;
    Status status_{};
};

}
