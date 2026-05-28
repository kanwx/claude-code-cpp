#pragma once

#include <string>
#include <optional>

namespace claude {

using String = std::string;  // standalone inclusion — same as Types.hpp

// ========== Error handling ==========

/// Result type (simplified expected)
template<typename T>
class Result {
public:
    struct ErrorTag {};
    Result(T value) : value_(std::move(value)), ok_(true) {}
    Result(ErrorTag, String error) : error_(std::move(error)), ok_(false) {}

    static Result<T> success(T value) { return Result(std::move(value)); }
    static Result<T> err(String error) { return Result(ErrorTag{}, std::move(error)); }

    bool ok() const { return ok_; }
    bool isErr() const { return !ok_; }

    const T& value() const { return value_.value(); }
    const String& error() const { return error_; }
    T& value() { return value_.value(); }

    operator bool() const { return ok_; }

private:
    std::optional<T> value_;
    String error_;
    bool ok_;
};

/// Specialization for String result (avoid T=String constructor ambiguity)
template<>
class Result<String> {
public:
    struct ErrorTag {};
    Result(String value) : value_(std::move(value)), ok_(true) {}
    Result(ErrorTag, String error) : error_(std::move(error)), ok_(false) {}

    static Result<String> success(String value) { return Result(std::move(value)); }
    static Result<String> err(String error) { return Result(ErrorTag{}, std::move(error)); }

    bool ok() const { return ok_; }
    bool isErr() const { return !ok_; }

    const String& value() const { return value_; }
    const String& error() const { return error_; }
    String& value() { return value_; }

    operator bool() const { return ok_; }

private:
    String value_;
    String error_;
    bool ok_;
};

/// Specialization for void result
template<>
class Result<void> {
public:
    struct ErrorTag {};
    Result() : ok_(true) {}
    Result(ErrorTag, String error) : error_(std::move(error)), ok_(false) {}

    static Result<void> success() { return Result(); }
    static Result<void> err(String error) { return Result(ErrorTag{}, std::move(error)); }

    bool ok() const { return ok_; }
    bool isErr() const { return !ok_; }
    const String& error() const { return error_; }

    operator bool() const { return ok_; }

private:
    String error_;
    bool ok_;
};

} // namespace claude
