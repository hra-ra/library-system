#pragma once
#include <memory>

#include "db.h"
#include "exceptions.h"
#include "fine_policy.h"
#include "repositories.h"

struct DashboardStats {
    int students = 0;
    int titles = 0;
    int copies = 0;
    int copiesOut = 0;
    int overdue = 0;
    int dueSoon = 0;
    int unpaidFineCount = 0;
    double unpaidFineTotal = 0;   // fines on books already returned
    double accruingFineTotal = 0; // fines building up on overdue books still out
};

// All the library rules live here (limits, due dates, fines). The web layer
// and the reminder job both go through this class.
class LibraryService {
public:
    explicit LibraryService(Database& db);

    StudentRepository& students() { return students_; }
    BookRepository& books() { return books_; }
    IssueRepository& issues() { return issues_; }
    SettingsRepository& settings() { return settings_; }
    EmailLogRepository& emailLog() { return emailLog_; }
    LibrarianRepository& librarians() { return librarians_; }

    int addStudent(const Student& s);
    void updateStudent(const Student& s);
    void deleteStudent(int id);

    int addBook(const Book& b, int copies);
    void updateBook(const Book& b);
    void deleteBook(int id);

    IssueView issueBook(const std::string& enrollmentNo, int bookId);
    IssueView returnBook(int issueId);
    void markFinePaid(int issueId);

    int daysOverdue(const IssueView& v) const;
    double fineFor(const IssueView& v);  // live figure for open issues, stored figure for returned ones
    DashboardStats stats();
    std::unique_ptr<IFinePolicy> currentPolicy();

private:
    void validateStudent(const Student& s);
    void validateBook(const Book& b);

    Database& db_;
    StudentRepository students_;
    BookRepository books_;
    IssueRepository issues_;
    SettingsRepository settings_;
    EmailLogRepository emailLog_;
    LibrarianRepository librarians_;
};
