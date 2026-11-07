#include "library_service.h"

#include <algorithm>
#include <cctype>

#include "util.h"

LibraryService::LibraryService(Database& db)
    : db_(db), students_(db), books_(db), issues_(db), settings_(db), emailLog_(db), librarians_(db) {
    db_.initSchema();
    settings_.setDefaultsIfMissing();
}

// ---------- validation ----------
static bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

void LibraryService::validateStudent(const Student& s) {
    if (s.name().empty()) throw ValidationException("Name is required");
    if (s.enrollmentNo().empty()) throw ValidationException("Enrollment number is required");
    auto at = s.email().find('@');
    if (at == std::string::npos || at == 0 || s.email().find('.', at) == std::string::npos)
        throw ValidationException("Enter a valid email address");
    if (!allDigits(s.phone()) || s.phone().size() < 10 || s.phone().size() > 12)
        throw ValidationException("Phone number should be 10 digits");
    if (s.year() < 1 || s.year() > 6) throw ValidationException("Year should be between 1 and 6");
    if (s.department().empty()) throw ValidationException("Department is required");
}

void LibraryService::validateBook(const Book& b) {
    if (b.title().empty()) throw ValidationException("Title is required");
    if (b.author().empty()) throw ValidationException("Author is required");
    if (b.price() < 0) throw ValidationException("Price cannot be negative");
}

// ---------- students ----------
int LibraryService::addStudent(const Student& s) {
    validateStudent(s);
    if (students_.findByEnrollment(s.enrollmentNo()))
        throw DuplicateException("A student with enrollment number " + s.enrollmentNo() + " already exists");
    return students_.add(s);
}

void LibraryService::updateStudent(const Student& s) {
    validateStudent(s);
    if (!students_.findById(s.id())) throw NotFoundException("Student not found");
    auto clash = students_.findByEnrollment(s.enrollmentNo());
    if (clash && clash->id() != s.id())
        throw DuplicateException("Another student already has enrollment number " + s.enrollmentNo());
    students_.update(s);
}

void LibraryService::deleteStudent(int id) {
    if (!students_.findById(id)) throw NotFoundException("Student not found");
    if (issues_.studentHistoryCount(id) > 0)
        throw ValidationException("This student has issue history, so the record can't be deleted. Mark them inactive instead.");
    students_.remove(id);
}

// ---------- books ----------
int LibraryService::addBook(const Book& b, int copies) {
    validateBook(b);
    if (copies < 1 || copies > 100) throw ValidationException("Copies should be between 1 and 100");
    return books_.addWithCopies(b, copies);
}

void LibraryService::updateBook(const Book& b) {
    validateBook(b);
    if (!books_.findById(b.id())) throw NotFoundException("Book not found");
    books_.update(b);
}

void LibraryService::deleteBook(int id) {
    auto stock = books_.stockFor(id);
    if (!stock) throw NotFoundException("Book not found");
    if (stock->availableCopies != stock->totalCopies)
        throw ValidationException("Some copies of this book are still issued");
    for (const auto& v : issues_.views("all"))
        if (v.bookId == id) throw ValidationException("This book has issue history and can't be deleted");
    books_.remove(id);
}

// ---------- policy & fines ----------
std::unique_ptr<IFinePolicy> LibraryService::currentPolicy() {
    return makeFinePolicy(settings_.get("fine_policy", "capped"), settings_.getDouble("fine_per_day", 2));
}

int LibraryService::daysOverdue(const IssueView& v) const {
    const std::string end = v.record.isReturned() ? v.record.returnedOn() : util::today();
    return std::max(0, util::daysBetween(v.record.dueOn(), end));
}

double LibraryService::fineFor(const IssueView& v) {
    if (v.record.isReturned()) return v.record.fineAmount();
    return currentPolicy()->calculate(daysOverdue(v), v.bookPrice);
}

// ---------- issue / return ----------
IssueView LibraryService::issueBook(const std::string& enrollmentNo, int bookId) {
    auto student = students_.findByEnrollment(enrollmentNo);
    if (!student) throw NotFoundException("No student with enrollment number " + enrollmentNo);
    if (!student->isActive()) throw InactiveStudentException(student->name() + " is marked inactive");

    auto stock = books_.stockFor(bookId);
    if (!stock) throw NotFoundException("Book not found");

    const int maxBooks = settings_.getInt("max_books", 3);
    if (issues_.openCountForStudent(student->id()) >= maxBooks)
        throw IssueLimitException(student->name() + " already has " + std::to_string(maxBooks) +
                                  " books out, which is the limit");

    const std::string today = util::today();
    for (const auto& v : issues_.unpaidFines())
        if (v.record.studentId() == student->id())
            throw UnpaidFineException(student->name() + " has an unpaid fine. Collect it before issuing more books.");
    for (const auto& v : issues_.openOverdue(today))
        if (v.record.studentId() == student->id())
            throw UnpaidFineException(student->name() + " has an overdue book (" + v.bookTitle +
                                      "). It needs to come back first.");

    auto copy = books_.findAvailableCopy(bookId);
    if (!copy) throw BookUnavailableException("All copies of \"" + stock->book.title() + "\" are out right now");

    db_.exec("BEGIN");
    try {
        IssueRecord rec(0, student->id(), copy->id(), today, util::addDays(today, settings_.getInt("loan_days", 14)),
                        "", 0.0, true);
        int id = issues_.add(rec);
        books_.setCopyStatus(copy->id(), "issued");
        db_.exec("COMMIT");
        return *issues_.findViewById(id);
    } catch (...) {
        db_.exec("ROLLBACK");
        throw;
    }
}

IssueView LibraryService::returnBook(int issueId) {
    auto view = issues_.findViewById(issueId);
    if (!view) throw NotFoundException("Issue record not found");
    if (view->record.isReturned()) throw ValidationException("This book was already returned");

    const std::string today = util::today();
    const int late = std::max(0, util::daysBetween(view->record.dueOn(), today));
    const double fine = currentPolicy()->calculate(late, view->bookPrice);

    db_.exec("BEGIN");
    try {
        IssueRecord updated(view->record.id(), view->record.studentId(), view->record.copyId(),
                            view->record.issuedOn(), view->record.dueOn(), today, fine, fine <= 0.0);
        issues_.update(updated);
        books_.setCopyStatus(view->record.copyId(), "available");
        db_.exec("COMMIT");
    } catch (...) {
        db_.exec("ROLLBACK");
        throw;
    }
    return *issues_.findViewById(issueId);
}

void LibraryService::markFinePaid(int issueId) {
    auto rec = issues_.findById(issueId);
    if (!rec) throw NotFoundException("Issue record not found");
    if (!rec->isReturned()) throw ValidationException("Fine can be collected only after the book is returned");
    if (rec->fineAmount() <= 0) throw ValidationException("There is no fine on this record");
    IssueRecord updated(rec->id(), rec->studentId(), rec->copyId(), rec->issuedOn(), rec->dueOn(),
                        rec->returnedOn(), rec->fineAmount(), true);
    issues_.update(updated);
}

// ---------- dashboard ----------
DashboardStats LibraryService::stats() {
    DashboardStats s;
    const std::string today = util::today();
    s.students = students_.count();
    s.titles = static_cast<int>(books_.all().size());
    s.copies = books_.totalCopies();
    s.copiesOut = books_.copiesOut();
    auto overdue = issues_.openOverdue(today);
    s.overdue = static_cast<int>(overdue.size());
    for (const auto& v : overdue) s.accruingFineTotal += fineFor(v);
    s.dueSoon = static_cast<int>(issues_.openDueBetween(today, util::addDays(today, 3)).size());
    auto unpaid = issues_.unpaidFines();
    s.unpaidFineCount = static_cast<int>(unpaid.size());
    for (const auto& v : unpaid) s.unpaidFineTotal += v.record.fineAmount();
    return s;
}
