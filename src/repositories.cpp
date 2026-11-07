#include "repositories.h"
#include "exceptions.h"

// ======================= Students =======================
static const char* kStudentCols = "id, name, email, phone, enrollment_no, department, year, status";

Student StudentRepository::fromRow(Statement& st) {
    return Student(st.integer(0), st.text(1), st.text(2), st.text(3), st.text(4), st.text(5),
                   st.integer(6), st.text(7));
}

std::vector<Student> StudentRepository::all() {
    Statement st(db_, std::string("SELECT ") + kStudentCols + " FROM students ORDER BY name");
    std::vector<Student> out;
    while (st.step()) out.push_back(fromRow(st));
    return out;
}

std::vector<Student> StudentRepository::search(const std::string& q) {
    if (q.empty()) return all();
    Statement st(db_, std::string("SELECT ") + kStudentCols +
                          " FROM students WHERE name LIKE ?1 OR enrollment_no LIKE ?1 OR email LIKE ?1 "
                          "OR department LIKE ?1 ORDER BY name");
    st.bind(1, "%" + q + "%");
    std::vector<Student> out;
    while (st.step()) out.push_back(fromRow(st));
    return out;
}

std::optional<Student> StudentRepository::findById(int id) {
    Statement st(db_, std::string("SELECT ") + kStudentCols + " FROM students WHERE id = ?1");
    st.bind(1, id);
    if (st.step()) return fromRow(st);
    return std::nullopt;
}

std::optional<Student> StudentRepository::findByEnrollment(const std::string& no) {
    Statement st(db_, std::string("SELECT ") + kStudentCols + " FROM students WHERE enrollment_no = ?1");
    st.bind(1, no);
    if (st.step()) return fromRow(st);
    return std::nullopt;
}

int StudentRepository::add(const Student& s) {
    Statement st(db_, "INSERT INTO students (enrollment_no, name, email, phone, department, year, status) "
                      "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)");
    st.bind(1, s.enrollmentNo()).bind(2, s.name()).bind(3, s.email()).bind(4, s.phone())
      .bind(5, s.department()).bind(6, s.year()).bind(7, s.status());
    st.step();
    return static_cast<int>(db_.lastInsertId());
}

void StudentRepository::update(const Student& s) {
    Statement st(db_, "UPDATE students SET enrollment_no=?1, name=?2, email=?3, phone=?4, department=?5, "
                      "year=?6, status=?7 WHERE id=?8");
    st.bind(1, s.enrollmentNo()).bind(2, s.name()).bind(3, s.email()).bind(4, s.phone())
      .bind(5, s.department()).bind(6, s.year()).bind(7, s.status()).bind(8, s.id());
    st.step();
}

void StudentRepository::remove(int id) {
    Statement st(db_, "DELETE FROM students WHERE id = ?1");
    st.bind(1, id);
    st.step();
}

int StudentRepository::count() {
    Statement st(db_, "SELECT COUNT(*) FROM students");
    st.step();
    return st.integer(0);
}

// ======================= Books =======================
std::vector<Book> BookRepository::all() {
    Statement st(db_, "SELECT id, title, author, isbn, category, price FROM books ORDER BY title");
    std::vector<Book> out;
    while (st.step())
        out.emplace_back(st.integer(0), st.text(1), st.text(2), st.text(3), st.text(4), st.real(5));
    return out;
}

std::optional<Book> BookRepository::findById(int id) {
    Statement st(db_, "SELECT id, title, author, isbn, category, price FROM books WHERE id = ?1");
    st.bind(1, id);
    if (st.step())
        return Book(st.integer(0), st.text(1), st.text(2), st.text(3), st.text(4), st.real(5));
    return std::nullopt;
}

int BookRepository::add(const Book& b) {
    Statement st(db_, "INSERT INTO books (title, author, isbn, category, price) VALUES (?1,?2,?3,?4,?5)");
    st.bind(1, b.title()).bind(2, b.author()).bind(3, b.isbn()).bind(4, b.category()).bind(5, b.price());
    st.step();
    return static_cast<int>(db_.lastInsertId());
}

void BookRepository::update(const Book& b) {
    Statement st(db_, "UPDATE books SET title=?1, author=?2, isbn=?3, category=?4, price=?5 WHERE id=?6");
    st.bind(1, b.title()).bind(2, b.author()).bind(3, b.isbn()).bind(4, b.category()).bind(5, b.price())
      .bind(6, b.id());
    st.step();
}

void BookRepository::remove(int id) {
    Statement st(db_, "DELETE FROM books WHERE id = ?1");
    st.bind(1, id);
    st.step();
}

void BookRepository::addCopies(int bookId, int count) {
    Statement mx(db_, "SELECT COALESCE(MAX(copy_no), 0) FROM book_copies WHERE book_id = ?1");
    mx.bind(1, bookId);
    mx.step();
    int next = mx.integer(0) + 1;
    for (int i = 0; i < count; ++i) {
        Statement st(db_, "INSERT INTO book_copies (book_id, copy_no, status) VALUES (?1, ?2, 'available')");
        st.bind(1, bookId).bind(2, next + i);
        st.step();
    }
}

int BookRepository::addWithCopies(const Book& b, int copies) {
    db_.exec("BEGIN");
    try {
        int id = add(b);
        addCopies(id, copies);
        db_.exec("COMMIT");
        return id;
    } catch (...) {
        db_.exec("ROLLBACK");
        throw;
    }
}

static const char* kStockSql =
    "SELECT b.id, b.title, b.author, b.isbn, b.category, b.price, "
    "COUNT(c.id), COALESCE(SUM(c.status = 'available'), 0) "
    "FROM books b LEFT JOIN book_copies c ON c.book_id = b.id ";

std::vector<BookStock> BookRepository::search(const std::string& q) {
    std::string sql = kStockSql;
    if (!q.empty()) sql += "WHERE b.title LIKE ?1 OR b.author LIKE ?1 OR b.isbn LIKE ?1 OR b.category LIKE ?1 ";
    sql += "GROUP BY b.id ORDER BY b.title";
    Statement st(db_, sql);
    if (!q.empty()) st.bind(1, "%" + q + "%");
    std::vector<BookStock> out;
    while (st.step())
        out.push_back({Book(st.integer(0), st.text(1), st.text(2), st.text(3), st.text(4), st.real(5)),
                       st.integer(6), st.integer(7)});
    return out;
}

std::optional<BookStock> BookRepository::stockFor(int bookId) {
    Statement st(db_, std::string(kStockSql) + "WHERE b.id = ?1 GROUP BY b.id");
    st.bind(1, bookId);
    if (st.step())
        return BookStock{Book(st.integer(0), st.text(1), st.text(2), st.text(3), st.text(4), st.real(5)),
                         st.integer(6), st.integer(7)};
    return std::nullopt;
}

std::optional<BookCopy> BookRepository::findAvailableCopy(int bookId) {
    Statement st(db_, "SELECT id, book_id, copy_no, status FROM book_copies "
                      "WHERE book_id = ?1 AND status = 'available' ORDER BY copy_no LIMIT 1");
    st.bind(1, bookId);
    if (st.step()) return BookCopy(st.integer(0), st.integer(1), st.integer(2), st.text(3));
    return std::nullopt;
}

std::optional<BookCopy> BookRepository::findCopy(int copyId) {
    Statement st(db_, "SELECT id, book_id, copy_no, status FROM book_copies WHERE id = ?1");
    st.bind(1, copyId);
    if (st.step()) return BookCopy(st.integer(0), st.integer(1), st.integer(2), st.text(3));
    return std::nullopt;
}

void BookRepository::setCopyStatus(int copyId, const std::string& status) {
    Statement st(db_, "UPDATE book_copies SET status = ?1 WHERE id = ?2");
    st.bind(1, status).bind(2, copyId);
    st.step();
}

int BookRepository::totalCopies() {
    Statement st(db_, "SELECT COUNT(*) FROM book_copies");
    st.step();
    return st.integer(0);
}

int BookRepository::copiesOut() {
    Statement st(db_, "SELECT COUNT(*) FROM book_copies WHERE status = 'issued'");
    st.step();
    return st.integer(0);
}

// ======================= Issues =======================
static IssueRecord recordFromRow(Statement& st) {
    return IssueRecord(st.integer(0), st.integer(1), st.integer(2), st.text(3), st.text(4), st.text(5),
                       st.real(6), st.integer(7) != 0);
}

std::vector<IssueRecord> IssueRepository::all() {
    Statement st(db_, "SELECT id, student_id, copy_id, issued_on, due_on, returned_on, fine_amount, fine_paid "
                      "FROM issues ORDER BY id DESC");
    std::vector<IssueRecord> out;
    while (st.step()) out.push_back(recordFromRow(st));
    return out;
}

std::optional<IssueRecord> IssueRepository::findById(int id) {
    Statement st(db_, "SELECT id, student_id, copy_id, issued_on, due_on, returned_on, fine_amount, fine_paid "
                      "FROM issues WHERE id = ?1");
    st.bind(1, id);
    if (st.step()) return recordFromRow(st);
    return std::nullopt;
}

int IssueRepository::add(const IssueRecord& r) {
    Statement st(db_, "INSERT INTO issues (student_id, copy_id, issued_on, due_on, returned_on, fine_amount, fine_paid) "
                      "VALUES (?1,?2,?3,?4,?5,?6,?7)");
    st.bind(1, r.studentId()).bind(2, r.copyId()).bind(3, r.issuedOn()).bind(4, r.dueOn())
      .bind(5, r.returnedOn()).bind(6, r.fineAmount()).bind(7, r.finePaid() ? 1 : 0);
    st.step();
    return static_cast<int>(db_.lastInsertId());
}

void IssueRepository::update(const IssueRecord& r) {
    Statement st(db_, "UPDATE issues SET returned_on=?1, fine_amount=?2, fine_paid=?3, due_on=?4 WHERE id=?5");
    st.bind(1, r.returnedOn()).bind(2, r.fineAmount()).bind(3, r.finePaid() ? 1 : 0).bind(4, r.dueOn())
      .bind(5, r.id());
    st.step();
}

void IssueRepository::remove(int id) {
    Statement st(db_, "DELETE FROM issues WHERE id = ?1");
    st.bind(1, id);
    st.step();
}

static const char* kViewSql =
    "SELECT i.id, i.student_id, i.copy_id, i.issued_on, i.due_on, i.returned_on, i.fine_amount, i.fine_paid, "
    "s.name, s.enrollment_no, s.email, b.title, b.author, b.id, c.copy_no, b.price "
    "FROM issues i JOIN students s ON s.id = i.student_id "
    "JOIN book_copies c ON c.id = i.copy_id JOIN books b ON b.id = c.book_id ";

std::vector<IssueView> IssueRepository::query(const std::string& where, const std::vector<std::string>& params,
                                              const std::string& order) {
    Statement st(db_, std::string(kViewSql) + (where.empty() ? "" : "WHERE " + where + " ") + "ORDER BY " + order);
    for (size_t i = 0; i < params.size(); ++i) st.bind(static_cast<int>(i) + 1, params[i]);
    std::vector<IssueView> out;
    while (st.step())
        out.push_back({recordFromRow(st), st.text(8), st.text(9), st.text(10), st.text(11), st.text(12),
                       st.integer(13), st.integer(14), st.real(15)});
    return out;
}

std::optional<IssueView> IssueRepository::findViewById(int id) {
    auto rows = query("i.id = ?1", {std::to_string(id)}, "i.id");
    if (rows.empty()) return std::nullopt;
    return rows.front();
}

std::vector<IssueView> IssueRepository::views(const std::string& filter, const std::string& search) {
    std::string where;
    std::vector<std::string> params;
    if (filter == "open") where = "i.returned_on = ''";
    else if (filter == "returned") where = "i.returned_on <> ''";
    if (!search.empty()) {
        if (!where.empty()) where += " AND ";
        params.push_back("%" + search + "%");
        where += "(s.name LIKE ?1 OR s.enrollment_no LIKE ?1 OR b.title LIKE ?1)";
    }
    return query(where, params, "i.id DESC");
}

std::vector<IssueView> IssueRepository::openDueBetween(const std::string& from, const std::string& to) {
    return query("i.returned_on = '' AND i.due_on >= ?1 AND i.due_on <= ?2", {from, to}, "i.due_on, s.name");
}

std::vector<IssueView> IssueRepository::openOverdue(const std::string& today) {
    return query("i.returned_on = '' AND i.due_on < ?1", {today}, "i.due_on, s.name");
}

std::vector<IssueView> IssueRepository::unpaidFines() {
    return query("i.returned_on <> '' AND i.fine_amount > 0 AND i.fine_paid = 0", {}, "i.returned_on DESC");
}

int IssueRepository::openCountForStudent(int studentId) {
    Statement st(db_, "SELECT COUNT(*) FROM issues WHERE student_id = ?1 AND returned_on = ''");
    st.bind(1, studentId);
    st.step();
    return st.integer(0);
}

int IssueRepository::studentHistoryCount(int studentId) {
    Statement st(db_, "SELECT COUNT(*) FROM issues WHERE student_id = ?1");
    st.bind(1, studentId);
    st.step();
    return st.integer(0);
}

// ======================= Librarians =======================
std::optional<Librarian> LibrarianRepository::findByUsername(const std::string& username) {
    Statement st(db_, "SELECT id, name, email, phone, username, salt, password_hash FROM librarians "
                      "WHERE username = ?1");
    st.bind(1, username);
    if (st.step())
        return Librarian(st.integer(0), st.text(1), st.text(2), st.text(3), st.text(4), st.text(5), st.text(6));
    return std::nullopt;
}

int LibrarianRepository::add(const Librarian& l) {
    Statement st(db_, "INSERT INTO librarians (name, email, phone, username, salt, password_hash) "
                      "VALUES (?1,?2,?3,?4,?5,?6)");
    st.bind(1, l.name()).bind(2, l.email()).bind(3, l.phone()).bind(4, l.username()).bind(5, l.salt())
      .bind(6, l.passwordHash());
    st.step();
    return static_cast<int>(db_.lastInsertId());
}

int LibrarianRepository::count() {
    Statement st(db_, "SELECT COUNT(*) FROM librarians");
    st.step();
    return st.integer(0);
}

// ======================= Settings =======================
std::string SettingsRepository::get(const std::string& key, const std::string& fallback) {
    Statement st(db_, "SELECT value FROM settings WHERE key = ?1");
    st.bind(1, key);
    return st.step() ? st.text(0) : fallback;
}

int SettingsRepository::getInt(const std::string& key, int fallback) {
    try { return std::stoi(get(key, std::to_string(fallback))); } catch (...) { return fallback; }
}

double SettingsRepository::getDouble(const std::string& key, double fallback) {
    try { return std::stod(get(key, std::to_string(fallback))); } catch (...) { return fallback; }
}

void SettingsRepository::set(const std::string& key, const std::string& value) {
    Statement st(db_, "INSERT INTO settings (key, value) VALUES (?1, ?2) "
                      "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    st.bind(1, key).bind(2, value);
    st.step();
}

void SettingsRepository::setDefaultsIfMissing() {
    const std::pair<const char*, const char*> defaults[] = {
        {"library_name", "Central Library"},
        {"loan_days", "14"},
        {"fine_per_day", "2"},
        {"fine_policy", "capped"},
        {"max_books", "3"},
        {"reminder_days", "3,1,0"},
        {"overdue_repeat_days", "3"},
    };
    for (const auto& d : defaults) {
        Statement st(db_, "INSERT OR IGNORE INTO settings (key, value) VALUES (?1, ?2)");
        st.bind(1, d.first).bind(2, d.second);
        st.step();
    }
}

// ======================= Email log =======================
void EmailLogRepository::record(int issueId, const std::string& kind, const std::string& sentOn,
                                const std::string& recipient, const std::string& status) {
    Statement st(db_, "INSERT INTO email_log (issue_id, kind, sent_on, recipient, status) VALUES (?1,?2,?3,?4,?5)");
    st.bind(1, issueId).bind(2, kind).bind(3, sentOn).bind(4, recipient).bind(5, status);
    st.step();
}

bool EmailLogRepository::alreadySent(int issueId, const std::string& kind, const std::string& sinceDate) {
    Statement st(db_, "SELECT COUNT(*) FROM email_log WHERE issue_id = ?1 AND kind = ?2 AND status = 'sent' "
                      "AND sent_on >= ?3");
    st.bind(1, issueId).bind(2, kind).bind(3, sinceDate);
    st.step();
    return st.integer(0) > 0;
}

std::vector<EmailLogEntry> EmailLogRepository::recent(int limit) {
    Statement st(db_, "SELECT e.id, e.issue_id, e.kind, e.sent_on, e.recipient, e.status, s.name, b.title "
                      "FROM email_log e JOIN issues i ON i.id = e.issue_id JOIN students s ON s.id = i.student_id "
                      "JOIN book_copies c ON c.id = i.copy_id JOIN books b ON b.id = c.book_id "
                      "ORDER BY e.id DESC LIMIT ?1");
    st.bind(1, limit);
    std::vector<EmailLogEntry> out;
    while (st.step())
        out.push_back({st.integer(0), st.integer(1), st.text(2), st.text(3), st.text(4), st.text(5),
                       st.text(6), st.text(7)});
    return out;
}
