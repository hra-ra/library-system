// Plain-C++ tests (no framework needed). Run with: ctest   or   ./build/library_tests
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>

#include "auth.h"
#include "email_service.h"
#include "library_service.h"
#include "reminder_service.h"
#include "util.h"

static int passed = 0, failed = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (cond) { ++passed; }                                                       \
        else { ++failed; std::cerr << "  FAILED line " << __LINE__ << ": " #cond "\n"; } \
    } while (0)

template <typename Ex>
static bool throwsType(const std::function<void()>& fn) {
    try { fn(); } catch (const Ex&) { return true; } catch (...) { return false; }
    return false;
}

static void setToday(const char* d) { setenv("LIBRARY_TODAY", d, 1); }

struct TestDb {
    std::string path;
    TestDb() : path(std::filesystem::temp_directory_path().string() + "/lib_test_" + util::randomHex(4) + ".db") {}
    ~TestDb() {
        for (const char* ext : {"", "-wal", "-shm"}) std::remove((path + ext).c_str());
    }
};

static Student makeStudent(const std::string& no, const std::string& name) {
    return Student(0, name, name.substr(0, 3) + "@college.edu", "9876543210", no, "IT", 2);
}

static void testDates() {
    std::cout << "dates\n";
    CHECK(util::daysBetween("2026-02-27", "2026-03-02") == 3);   // 2026 is not a leap year
    CHECK(util::daysBetween("2028-02-27", "2028-03-02") == 4);   // 2028 is
    CHECK(util::addDays("2026-12-30", 5) == "2027-01-04");
    CHECK(util::addDays("2026-03-01", -1) == "2026-02-28");
}

static void testFinePolicies() {
    std::cout << "fine policies\n";
    PerDayFinePolicy perDay(2);
    CappedFinePolicy capped(2);
    CHECK(perDay.calculate(0, 100) == 0);
    CHECK(perDay.calculate(-3, 100) == 0);
    CHECK(perDay.calculate(10, 100) == 20);
    CHECK(perDay.calculate(80, 100) == 160);   // no cap
    CHECK(capped.calculate(80, 100) == 100);   // capped at book price
    CHECK(capped.calculate(10, 100) == 20);
    CHECK(makeFinePolicy("capped", 2)->name() == "capped");
    CHECK(makeFinePolicy("anything", 2)->name() == "per_day");
}

static void testStudentsAndBooks() {
    std::cout << "students and books\n";
    TestDb t;
    Database db(t.path);
    LibraryService lib(db);

    int id = lib.addStudent(makeStudent("E1", "Asha Rao"));
    CHECK(id > 0);
    CHECK(throwsType<DuplicateException>([&] { lib.addStudent(makeStudent("E1", "Someone Else")); }));
    CHECK(throwsType<ValidationException>([&] {
        lib.addStudent(Student(0, "No Mail", "nomail", "9876543210", "E2", "IT", 1));
    }));
    CHECK(throwsType<ValidationException>([&] {
        lib.addStudent(Student(0, "Bad Phone", "a@b.com", "123", "E3", "IT", 1));
    }));
    CHECK(throwsType<ValidationException>([&] { lib.addBook(Book(0, "", "Author", "", "", 100), 1); }));
    CHECK(throwsType<ValidationException>([&] { lib.addBook(Book(0, "T", "A", "", "", 100), 0); }));

    int bid = lib.addBook(Book(0, "Test Book", "Writer", "", "CS", 300), 3);
    auto stock = lib.books().stockFor(bid);
    CHECK(stock && stock->totalCopies == 3 && stock->availableCopies == 3);
    lib.books().addCopies(bid, 2);
    CHECK(lib.books().stockFor(bid)->totalCopies == 5);
    CHECK(lib.students().search("asha").size() == 1);
    CHECK(lib.students().search("zzz").empty());
}

static void testIssueReturnAndFines() {
    std::cout << "issue, return, fines\n";
    TestDb t;
    Database db(t.path);
    LibraryService lib(db);
    setToday("2026-11-01");

    lib.addStudent(makeStudent("S1", "Rhea Kapoor"));
    int single = lib.addBook(Book(0, "One Copy Book", "A. Writer", "", "CS", 500), 1);
    int many = lib.addBook(Book(0, "Many Copy Book", "B. Writer", "", "CS", 40), 4);

    IssueView v = lib.issueBook("S1", single);
    CHECK(v.record.issuedOn() == "2026-11-01");
    CHECK(v.record.dueOn() == "2026-11-15");           // 14-day default loan
    CHECK(lib.books().stockFor(single)->availableCopies == 0);

    lib.addStudent(makeStudent("S2", "Kabir Singh"));
    CHECK(throwsType<BookUnavailableException>([&] { lib.issueBook("S2", single); }));
    CHECK(throwsType<NotFoundException>([&] { lib.issueBook("NOPE", single); }));

    // return 5 days late: 5 * Rs.2 = Rs.10
    setToday("2026-11-20");
    CHECK(lib.daysOverdue(*lib.issues().findViewById(v.record.id())) == 5);
    IssueView back = lib.returnBook(v.record.id());
    CHECK(back.record.isReturned());
    CHECK(back.record.fineAmount() == 10);
    CHECK(!back.record.finePaid());
    CHECK(lib.books().stockFor(single)->availableCopies == 1);
    CHECK(throwsType<ValidationException>([&] { lib.returnBook(v.record.id()); }));

    // unpaid fine blocks new issues until it's paid
    CHECK(throwsType<UnpaidFineException>([&] { lib.issueBook("S1", many); }));
    lib.markFinePaid(v.record.id());
    CHECK(lib.issueBook("S1", many).record.id() > 0);

    // on-time return has no fine
    IssueView ontime = lib.issueBook("S2", many);
    setToday("2026-11-25");
    CHECK(lib.returnBook(ontime.record.id()).record.fineAmount() == 0);
    CHECK(throwsType<ValidationException>([&] { lib.markFinePaid(ontime.record.id()); }));

    // overdue book blocks new issues
    setToday("2026-12-30");
    CHECK(throwsType<UnpaidFineException>([&] { lib.issueBook("S1", many); }));

    // capped policy never exceeds the price (book costs Rs.40; 45 days late would be Rs.90)
    DashboardStats s = lib.stats();
    CHECK(s.overdue == 1);
    CHECK(s.accruingFineTotal == 40);
}

static void testLimits() {
    std::cout << "issue limit and inactive students\n";
    TestDb t;
    Database db(t.path);
    LibraryService lib(db);
    setToday("2026-11-01");
    lib.addStudent(makeStudent("L1", "Limit Tester"));
    int b = lib.addBook(Book(0, "Plenty", "Author", "", "CS", 100), 10);
    for (int i = 0; i < 3; ++i) lib.issueBook("L1", b);
    CHECK(throwsType<IssueLimitException>([&] { lib.issueBook("L1", b); }));

    auto s = lib.students().findByEnrollment("L1");
    lib.updateStudent(Student(s->id(), s->name(), s->email(), s->phone(), s->enrollmentNo(), s->department(),
                              s->year(), "inactive"));
    CHECK(throwsType<InactiveStudentException>([&] { lib.issueBook("L1", b); }));
    CHECK(throwsType<ValidationException>([&] { lib.deleteStudent(s->id()); }));  // has history
}

static void testReminders() {
    std::cout << "reminders\n";
    TestDb t;
    Database db(t.path);
    LibraryService lib(db);
    std::string outbox = std::filesystem::temp_directory_path().string() + "/lib_outbox_" + util::randomHex(4);
    FileEmailService mailer(outbox);
    ReminderService reminders(lib, mailer);

    setToday("2026-11-01");
    lib.addStudent(makeStudent("R1", "Reminder Rani"));
    int b = lib.addBook(Book(0, "Reminder Book", "Author", "", "CS", 100), 2);
    lib.issueBook("R1", b);                       // due 2026-11-15

    setToday("2026-11-05");
    CHECK(reminders.run(false).empty());          // 10 days left: too early
    setToday("2026-11-12");
    auto r = reminders.run(false);                // 3 days left
    CHECK(r.size() == 1 && r[0].kind == "due_in_3" && r[0].sent);
    CHECK(reminders.run(false).empty());          // same day again: no duplicate
    setToday("2026-11-14");
    CHECK(reminders.run(true).size() == 1);       // dry run reports...
    CHECK(reminders.run(true).size() == 1);       // ...but doesn't log, so it still reports
    CHECK(reminders.run(false).size() == 1);      // due_in_1 sent for real
    setToday("2026-11-15");
    CHECK(reminders.run(false)[0].kind == "due_today");
    setToday("2026-11-17");
    r = reminders.run(false);
    CHECK(r.size() == 1 && r[0].kind == "overdue");
    setToday("2026-11-18");
    CHECK(reminders.run(false).empty());          // overdue notice repeats only every 3 days
    setToday("2026-11-20");
    CHECK(reminders.run(false).size() == 1);

    int files = 0;
    for (const auto& e : std::filesystem::directory_iterator(outbox)) { (void)e; ++files; }
    CHECK(files == 5);
    std::filesystem::remove_all(outbox);
}

static void testAuth() {
    std::cout << "auth\n";
    TestDb t;
    Database db(t.path);
    LibraryService lib(db);
    AuthService auth(lib.librarians());
    CHECK(throwsType<ValidationException>([&] { auth.createLibrarian("A", "a@b.c", "u", "short"); }));
    auth.createLibrarian("Admin", "a@b.com", "admin", "secret123");
    CHECK(throwsType<AuthException>([&] { auth.login("admin", "wrong"); }));
    CHECK(throwsType<AuthException>([&] { auth.login("ghost", "secret123"); }));
    std::string token = auth.login("admin", "secret123");
    CHECK(auth.validate(token).has_value());
    CHECK(!auth.validate("not-a-token").has_value());
    auth.logout(token);
    CHECK(!auth.validate(token).has_value());
}

int main() {
    testDates();
    testFinePolicies();
    testStudentsAndBooks();
    testIssueReturnAndFines();
    testLimits();
    testReminders();
    testAuth();
    std::cout << "\n" << passed << " checks passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
