#pragma once
#include <optional>
#include <string>
#include <vector>

#include "db.h"
#include "models.h"

// Generic interface every repository follows. The rest of the program
// talks to these classes and never writes SQL itself.
template <typename T>
class Repository {
public:
    virtual ~Repository() = default;
    virtual std::vector<T> all() = 0;
    virtual std::optional<T> findById(int id) = 0;
    virtual int add(const T& item) = 0;
    virtual void update(const T& item) = 0;
    virtual void remove(int id) = 0;
};

class StudentRepository : public Repository<Student> {
public:
    explicit StudentRepository(Database& db) : db_(db) {}
    std::vector<Student> all() override;
    std::optional<Student> findById(int id) override;
    int add(const Student& s) override;
    void update(const Student& s) override;
    void remove(int id) override;

    std::vector<Student> search(const std::string& query);
    std::optional<Student> findByEnrollment(const std::string& enrollmentNo);
    int count();

private:
    static Student fromRow(Statement& st);
    Database& db_;
};

class BookRepository : public Repository<Book> {
public:
    explicit BookRepository(Database& db) : db_(db) {}
    std::vector<Book> all() override;
    std::optional<Book> findById(int id) override;
    int add(const Book& b) override;  // adds the title only, no copies
    void update(const Book& b) override;
    void remove(int id) override;

    int addWithCopies(const Book& b, int copies);
    void addCopies(int bookId, int count);
    std::vector<BookStock> search(const std::string& query);
    std::optional<BookStock> stockFor(int bookId);
    std::optional<BookCopy> findAvailableCopy(int bookId);
    std::optional<BookCopy> findCopy(int copyId);
    void setCopyStatus(int copyId, const std::string& status);
    int totalCopies();
    int copiesOut();

private:
    Database& db_;
};

class IssueRepository : public Repository<IssueRecord> {
public:
    explicit IssueRepository(Database& db) : db_(db) {}
    std::vector<IssueRecord> all() override;
    std::optional<IssueRecord> findById(int id) override;
    int add(const IssueRecord& r) override;
    void update(const IssueRecord& r) override;
    void remove(int id) override;

    std::optional<IssueView> findViewById(int id);
    // filter: "open", "returned" or "all"; newest first
    std::vector<IssueView> views(const std::string& filter, const std::string& search = "");
    std::vector<IssueView> openDueBetween(const std::string& from, const std::string& to);
    std::vector<IssueView> openOverdue(const std::string& today);
    std::vector<IssueView> unpaidFines();
    int openCountForStudent(int studentId);
    int studentHistoryCount(int studentId);

private:
    std::vector<IssueView> query(const std::string& where, const std::vector<std::string>& params,
                                 const std::string& order);
    Database& db_;
};

class LibrarianRepository {
public:
    explicit LibrarianRepository(Database& db) : db_(db) {}
    std::optional<Librarian> findByUsername(const std::string& username);
    int add(const Librarian& l);
    int count();

private:
    Database& db_;
};

class SettingsRepository {
public:
    explicit SettingsRepository(Database& db) : db_(db) {}
    std::string get(const std::string& key, const std::string& fallback = "");
    int getInt(const std::string& key, int fallback);
    double getDouble(const std::string& key, double fallback);
    void set(const std::string& key, const std::string& value);
    void setDefaultsIfMissing();

private:
    Database& db_;
};

struct EmailLogEntry {
    int id;
    int issueId;
    std::string kind, sentOn, recipient, status;
    std::string studentName, bookTitle;
};

class EmailLogRepository {
public:
    explicit EmailLogRepository(Database& db) : db_(db) {}
    void record(int issueId, const std::string& kind, const std::string& sentOn,
                const std::string& recipient, const std::string& status);
    bool alreadySent(int issueId, const std::string& kind, const std::string& sinceDate = "");
    std::vector<EmailLogEntry> recent(int limit);

private:
    Database& db_;
};
