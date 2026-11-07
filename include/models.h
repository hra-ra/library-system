#pragma once
#include <ostream>
#include <string>

// ---------- People ----------
// Person is abstract: you can't create one directly, only a Student or Librarian.
class Person {
public:
    Person(int id, std::string name, std::string email, std::string phone)
        : id_(id), name_(std::move(name)), email_(std::move(email)), phone_(std::move(phone)) {}
    virtual ~Person() = default;

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& email() const { return email_; }
    const std::string& phone() const { return phone_; }

    virtual std::string role() const = 0;
    virtual std::string summary() const { return name_ + " <" + email_ + ">"; }

private:
    int id_;
    std::string name_, email_, phone_;
};

class Student : public Person {
public:
    Student(int id, std::string name, std::string email, std::string phone,
            std::string enrollmentNo, std::string department, int year, std::string status = "active")
        : Person(id, std::move(name), std::move(email), std::move(phone)),
          enrollmentNo_(std::move(enrollmentNo)), department_(std::move(department)),
          year_(year), status_(std::move(status)) {}

    const std::string& enrollmentNo() const { return enrollmentNo_; }
    const std::string& department() const { return department_; }
    int year() const { return year_; }
    const std::string& status() const { return status_; }
    bool isActive() const { return status_ == "active"; }

    std::string role() const override { return "student"; }
    std::string summary() const override { return enrollmentNo_ + " - " + Person::summary(); }

private:
    std::string enrollmentNo_, department_;
    int year_;
    std::string status_;
};

class Librarian : public Person {
public:
    Librarian(int id, std::string name, std::string email, std::string phone,
              std::string username, std::string salt, std::string passwordHash)
        : Person(id, std::move(name), std::move(email), std::move(phone)),
          username_(std::move(username)), salt_(std::move(salt)), passwordHash_(std::move(passwordHash)) {}

    const std::string& username() const { return username_; }
    const std::string& salt() const { return salt_; }
    const std::string& passwordHash() const { return passwordHash_; }

    std::string role() const override { return "librarian"; }

private:
    std::string username_, salt_, passwordHash_;
};

// ---------- Books ----------
class Book {
public:
    Book(int id, std::string title, std::string author, std::string isbn,
         std::string category, double price)
        : id_(id), title_(std::move(title)), author_(std::move(author)), isbn_(std::move(isbn)),
          category_(std::move(category)), price_(price) {}

    int id() const { return id_; }
    const std::string& title() const { return title_; }
    const std::string& author() const { return author_; }
    const std::string& isbn() const { return isbn_; }
    const std::string& category() const { return category_; }
    double price() const { return price_; }

private:
    int id_;
    std::string title_, author_, isbn_, category_;
    double price_;
};

// A Book with its stock numbers, used for the catalogue screen.
struct BookStock {
    Book book;
    int totalCopies;
    int availableCopies;
};

class BookCopy {
public:
    BookCopy(int id, int bookId, int copyNo, std::string status)
        : id_(id), bookId_(bookId), copyNo_(copyNo), status_(std::move(status)) {}
    int id() const { return id_; }
    int bookId() const { return bookId_; }
    int copyNo() const { return copyNo_; }
    const std::string& status() const { return status_; }
    bool isAvailable() const { return status_ == "available"; }

private:
    int id_, bookId_, copyNo_;
    std::string status_;
};

// ---------- Issue records ----------
class IssueRecord {
public:
    IssueRecord(int id, int studentId, int copyId, std::string issuedOn, std::string dueOn,
                std::string returnedOn, double fineAmount, bool finePaid)
        : id_(id), studentId_(studentId), copyId_(copyId), issuedOn_(std::move(issuedOn)),
          dueOn_(std::move(dueOn)), returnedOn_(std::move(returnedOn)),
          fineAmount_(fineAmount), finePaid_(finePaid) {}

    int id() const { return id_; }
    int studentId() const { return studentId_; }
    int copyId() const { return copyId_; }
    const std::string& issuedOn() const { return issuedOn_; }
    const std::string& dueOn() const { return dueOn_; }
    const std::string& returnedOn() const { return returnedOn_; }
    double fineAmount() const { return fineAmount_; }
    bool finePaid() const { return finePaid_; }
    bool isReturned() const { return !returnedOn_.empty(); }

private:
    int id_, studentId_, copyId_;
    std::string issuedOn_, dueOn_, returnedOn_;
    double fineAmount_;
    bool finePaid_;
};

inline std::ostream& operator<<(std::ostream& os, const IssueRecord& r) {
    os << "Issue#" << r.id() << " [student " << r.studentId() << ", copy " << r.copyId()
       << "] issued " << r.issuedOn() << ", due " << r.dueOn()
       << (r.isReturned() ? ", returned " + r.returnedOn() : std::string(", still out"));
    return os;
}

// Issue record joined with the student and book it refers to (for screens and emails).
struct IssueView {
    IssueRecord record;
    std::string studentName, enrollmentNo, studentEmail;
    std::string bookTitle, bookAuthor;
    int bookId;
    int copyNo;
    double bookPrice;
};
