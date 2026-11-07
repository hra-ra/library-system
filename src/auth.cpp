#include "auth.h"

#include "exceptions.h"
#include "util.h"

std::string AuthService::login(const std::string& username, const std::string& password) {
    auto user = repo_.findByUsername(username);
    // same error for "no such user" and "wrong password"
    if (!user || util::sha256Hex(user->salt() + password) != user->passwordHash())
        throw AuthException("Wrong username or password");

    std::lock_guard<std::mutex> lock(mutex_);
    std::string token = util::randomHex(24);
    sessions_[token] = Session{user->id(), user->name(), user->username()};
    return token;
}

void AuthService::logout(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(token);
}

std::optional<Session> AuthService::validate(const std::string& token) {
    if (token.empty()) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}

int AuthService::createLibrarian(const std::string& name, const std::string& email, const std::string& username,
                                 const std::string& password) {
    if (password.size() < 6) throw ValidationException("Password must be at least 6 characters");
    std::string salt = util::randomHex(8);
    Librarian l(0, name, email, "", username, salt, util::sha256Hex(salt + password));
    return repo_.add(l);
}
