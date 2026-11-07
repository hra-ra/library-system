#pragma once
#include <sqlite3.h>
#include <string>

// RAII wrapper: the connection opens in the constructor and always closes
// in the destructor, even if an exception is thrown in between.
class Database {
public:
    explicit Database(const std::string& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* handle() const { return db_; }
    void exec(const std::string& sql);
    long long lastInsertId() const;
    void initSchema();

private:
    sqlite3* db_ = nullptr;
};

// Thin wrapper over a prepared statement (also RAII).
class Statement {
public:
    Statement(Database& db, const std::string& sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int idx, const std::string& v);
    Statement& bind(int idx, const char* v);
    Statement& bind(int idx, int v);
    Statement& bind(int idx, double v);
    Statement& bindNull(int idx);

    bool step();  // true while a row is available
    std::string text(int col) const;
    int integer(int col) const;
    double real(int col) const;

private:
    sqlite3_stmt* stmt_ = nullptr;
    sqlite3* db_ = nullptr;
};
