#include "db.h"
#include "exceptions.h"

Database::Database(const std::string& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        throw LibraryException("Could not open database: " + msg);
    }
    sqlite3_busy_timeout(db_, 5000);  // the reminder job may be reading at the same time
    exec("PRAGMA foreign_keys = ON;");
    exec("PRAGMA journal_mode = WAL;");
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw LibraryException("Database error: " + msg);
    }
}

long long Database::lastInsertId() const { return sqlite3_last_insert_rowid(db_); }

void Database::initSchema() {
    exec(R"SQL(
CREATE TABLE IF NOT EXISTS librarians (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    phone TEXT NOT NULL DEFAULT '',
    username TEXT NOT NULL UNIQUE,
    salt TEXT NOT NULL,
    password_hash TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS students (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    enrollment_no TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL,
    email TEXT NOT NULL,
    phone TEXT NOT NULL,
    department TEXT NOT NULL,
    year INTEGER NOT NULL,
    status TEXT NOT NULL DEFAULT 'active'
);
CREATE TABLE IF NOT EXISTS books (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    title TEXT NOT NULL,
    author TEXT NOT NULL,
    isbn TEXT NOT NULL DEFAULT '',
    category TEXT NOT NULL DEFAULT '',
    price REAL NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS book_copies (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    book_id INTEGER NOT NULL REFERENCES books(id) ON DELETE CASCADE,
    copy_no INTEGER NOT NULL,
    status TEXT NOT NULL DEFAULT 'available',
    UNIQUE (book_id, copy_no)
);
CREATE TABLE IF NOT EXISTS issues (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    student_id INTEGER NOT NULL REFERENCES students(id),
    copy_id INTEGER NOT NULL REFERENCES book_copies(id),
    issued_on TEXT NOT NULL,
    due_on TEXT NOT NULL,
    returned_on TEXT NOT NULL DEFAULT '',
    fine_amount REAL NOT NULL DEFAULT 0,
    fine_paid INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS email_log (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    issue_id INTEGER NOT NULL REFERENCES issues(id),
    kind TEXT NOT NULL,
    sent_on TEXT NOT NULL,
    recipient TEXT NOT NULL,
    status TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_issues_student ON issues(student_id);
CREATE INDEX IF NOT EXISTS idx_issues_open ON issues(returned_on);
)SQL");
}

// ---------- Statement ----------
Statement::Statement(Database& db, const std::string& sql) : db_(db.handle()) {
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK)
        throw LibraryException(std::string("Bad SQL: ") + sqlite3_errmsg(db_));
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement& Statement::bind(int idx, const std::string& v) {
    sqlite3_bind_text(stmt_, idx, v.c_str(), -1, SQLITE_TRANSIENT);
    return *this;
}
Statement& Statement::bind(int idx, const char* v) {
    sqlite3_bind_text(stmt_, idx, v, -1, SQLITE_TRANSIENT);
    return *this;
}
Statement& Statement::bind(int idx, int v) {
    sqlite3_bind_int(stmt_, idx, v);
    return *this;
}
Statement& Statement::bind(int idx, double v) {
    sqlite3_bind_double(stmt_, idx, v);
    return *this;
}
Statement& Statement::bindNull(int idx) {
    sqlite3_bind_null(stmt_, idx);
    return *this;
}

bool Statement::step() {
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    if (rc == SQLITE_CONSTRAINT) throw DuplicateException("That record already exists or is still in use");
    throw LibraryException(std::string("Database error: ") + sqlite3_errmsg(db_));
}

std::string Statement::text(int col) const {
    const unsigned char* t = sqlite3_column_text(stmt_, col);
    return t ? reinterpret_cast<const char*>(t) : "";
}
int Statement::integer(int col) const { return sqlite3_column_int(stmt_, col); }
double Statement::real(int col) const { return sqlite3_column_double(stmt_, col); }
