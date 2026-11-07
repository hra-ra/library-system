#pragma once
#include <stdexcept>
#include <string>

// All business-rule failures derive from LibraryException so the web layer
// can catch one type and translate it into a sensible HTTP response.
class LibraryException : public std::runtime_error {
public:
    explicit LibraryException(const std::string& msg) : std::runtime_error(msg) {}
};

class ValidationException : public LibraryException {
public:
    using LibraryException::LibraryException;
};

class NotFoundException : public LibraryException {
public:
    using LibraryException::LibraryException;
};

class DuplicateException : public LibraryException {
public:
    using LibraryException::LibraryException;
};

class BookUnavailableException : public LibraryException {
public:
    using LibraryException::LibraryException;
};

class IssueLimitException : public LibraryException {
public:
    using LibraryException::LibraryException;
};

class UnpaidFineException : public LibraryException {
public:
    using LibraryException::LibraryException;
};

class InactiveStudentException : public LibraryException {
public:
    using LibraryException::LibraryException;
};

class AuthException : public LibraryException {
public:
    using LibraryException::LibraryException;
};
