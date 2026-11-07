#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "repositories.h"

struct Session {
    int librarianId;
    std::string name;
    std::string username;
};

class AuthService {
public:
    explicit AuthService(LibrarianRepository& repo) : repo_(repo) {}

    // Returns a session token, or throws AuthException.
    std::string login(const std::string& username, const std::string& password);
    void logout(const std::string& token);
    std::optional<Session> validate(const std::string& token);

    int createLibrarian(const std::string& name, const std::string& email, const std::string& username,
                        const std::string& password);

private:
    LibrarianRepository& repo_;
    std::unordered_map<std::string, Session> sessions_;
    std::mutex mutex_;
};
