// Fills a fresh database with demo data: one admin login, students, books and a few loans.
//   ./seed_data [admin_password]
#include <cstdlib>
#include <iostream>

#include "auth.h"
#include "library_service.h"
#include "util.h"

namespace {

struct S { const char *no, *name, *email, *phone, *dept; int year; };
struct B { const char *title, *author, *isbn, *cat; double price; int copies; };

const S kStudents[] = {
    {"03514802724", "Aarav Sharma", "aarav.sharma@student.example.edu", "9810234561", "IT", 2},
    {"03614802724", "Ishita Verma", "ishita.verma@student.example.edu", "9899045612", "IT", 2},
    {"03214802724", "Rohan Malhotra", "rohan.malhotra@student.example.edu", "9953127780", "CSE", 3},
    {"03314802724", "Kavya Nair", "kavya.nair@student.example.edu", "9711456723", "CSE", 3},
    {"04114802723", "Mohit Chauhan", "mohit.chauhan@student.example.edu", "9818765432", "ECE", 3},
    {"04214802723", "Simran Kaur", "simran.kaur@student.example.edu", "9871203345", "ECE", 3},
    {"03914802722", "Harshit Gupta", "harshit.gupta@student.example.edu", "9654321876", "EEE", 4},
    {"03814802722", "Ananya Iyer", "ananya.iyer@student.example.edu", "9205567811", "IT", 4},
    {"04014802724", "Dev Tyagi", "dev.tyagi@student.example.edu", "9990012345", "MAE", 2},
    {"04314802725", "Nikita Bansal", "nikita.bansal@student.example.edu", "9582233910", "CSE", 1},
    {"03714802725", "Yash Rawat", "yash.rawat@student.example.edu", "9717894561", "IT", 1},
    {"04414802725", "Tanvi Saxena", "tanvi.saxena@student.example.edu", "9811567204", "ECE", 1},
    {"03414802723", "Kunal Mehra", "kunal.mehra@student.example.edu", "9136754021", "MAE", 3},
    {"03114802724", "Priya Dahiya", "priya.dahiya@student.example.edu", "9873345609", "EEE", 2},
};

const B kBooks[] = {
    {"Introduction to Algorithms", "Thomas H. Cormen", "9780262033848", "Computer Science", 950, 3},
    {"The C++ Programming Language", "Bjarne Stroustrup", "", "Computer Science", 780, 2},
    {"Object-Oriented Programming with C++", "E. Balagurusamy", "", "Computer Science", 560, 4},
    {"Effective Modern C++", "Scott Meyers", "9781491903995", "Computer Science", 640, 1},
    {"Clean Code", "Robert C. Martin", "9780132350884", "Computer Science", 520, 2},
    {"Design Patterns", "Erich Gamma et al.", "9780201633610", "Computer Science", 700, 2},
    {"Database System Concepts", "Abraham Silberschatz", "", "Computer Science", 840, 3},
    {"Operating System Concepts", "Abraham Silberschatz", "", "Computer Science", 880, 3},
    {"Computer Networks", "Andrew S. Tanenbaum", "", "Computer Science", 720, 2},
    {"Let Us C", "Yashavant Kanetkar", "", "Computer Science", 350, 5},
    {"Data Structures Using C", "Reema Thareja", "", "Computer Science", 480, 3},
    {"Artificial Intelligence: A Modern Approach", "Stuart Russell, Peter Norvig", "", "Computer Science", 990, 2},
    {"Discrete Mathematics and Its Applications", "Kenneth H. Rosen", "", "Mathematics", 650, 2},
    {"Higher Engineering Mathematics", "B. S. Grewal", "", "Mathematics", 540, 4},
    {"Digital Design", "M. Morris Mano", "", "Electronics", 460, 3},
    {"Microelectronic Circuits", "Sedra and Smith", "", "Electronics", 820, 2},
    {"Engineering Mechanics", "S. S. Bhavikatti", "", "Mechanical", 420, 2},
    {"Wings of Fire", "A. P. J. Abdul Kalam", "", "Biography", 250, 3},
    {"The Discovery of India", "Jawaharlal Nehru", "", "History", 380, 2},
    {"Godan", "Premchand", "", "Fiction", 220, 2},
    {"Malgudi Days", "R. K. Narayan", "", "Fiction", 260, 2},
    {"The White Tiger", "Aravind Adiga", "", "Fiction", 340, 2},
    {"Ignited Minds", "A. P. J. Abdul Kalam", "", "Non-fiction", 230, 2},
    {"Rich Dad Poor Dad", "Robert T. Kiyosaki", "", "Non-fiction", 300, 2},
};

// Backdated loan straight into the tables, so the demo has overdue and returned items.
void addLoan(LibraryService& lib, const std::string& enrollment, int bookId, int issuedDaysAgo,
             int returnedDaysAgo /* -1 = still out */) {
    auto student = lib.students().findByEnrollment(enrollment);
    auto copy = lib.books().findAvailableCopy(bookId);
    auto book = lib.books().findById(bookId);
    if (!student || !copy || !book) return;

    const std::string today = util::today();
    const std::string issued = util::addDays(today, -issuedDaysAgo);
    const std::string due = util::addDays(issued, lib.settings().getInt("loan_days", 14));
    std::string returned;
    double fine = 0;
    bool paid = true;
    if (returnedDaysAgo >= 0) {
        returned = util::addDays(today, -returnedDaysAgo);
        fine = lib.currentPolicy()->calculate(std::max(0, util::daysBetween(due, returned)), book->price());
        paid = fine <= 0;
    }
    lib.issues().add(IssueRecord(0, student->id(), copy->id(), issued, due, returned, fine, paid));
    if (returnedDaysAgo < 0) lib.books().setCopyStatus(copy->id(), "issued");
}

}  // namespace

int main(int argc, char** argv) {
    const char* dbEnv = std::getenv("LIBRARY_DB");
    const std::string adminPassword = argc > 1 ? argv[1] : "admin123";
    try {
        Database db(dbEnv ? dbEnv : "data/library.db");
        LibraryService lib(db);
        if (lib.students().count() > 0) {
            std::cout << "Database already has data, not seeding again.\n";
            return 0;
        }

        AuthService auth(lib.librarians());
        auth.createLibrarian("Mrs. Sunita Arora", "library@college.example.edu", "admin", adminPassword);

        for (const auto& s : kStudents)
            lib.addStudent(Student(0, s.name, s.email, s.phone, s.no, s.dept, s.year));
        for (const auto& b : kBooks)
            lib.addBook(Book(0, b.title, b.author, b.isbn, b.cat, b.price), b.copies);

        // loans: some on time, some due soon, some overdue, a few already returned
        addLoan(lib, "03514802724", 3, 4, -1);    // Aarav: OOP with C++, 4 days ago
        addLoan(lib, "03514802724", 5, 12, -1);   // Aarav: Clean Code, due in 2 days
        addLoan(lib, "03614802724", 1, 13, -1);   // Ishita: Algorithms, due tomorrow
        addLoan(lib, "03214802724", 8, 20, -1);   // Rohan: OS Concepts, 6 days overdue
        addLoan(lib, "03314802724", 7, 15, -1);   // Kavya: DBMS, 1 day overdue
        addLoan(lib, "04114802723", 15, 14, -1);  // Mohit: Digital Design, due today
        addLoan(lib, "03814802722", 12, 26, -1);  // Ananya: AI textbook, 12 days overdue
        addLoan(lib, "03914802722", 16, 9, -1);   // Harshit: Microelectronic Circuits
        addLoan(lib, "04214802723", 18, 30, 12);  // Simran: Wings of Fire, returned late, fine unpaid
        addLoan(lib, "03414802723", 17, 25, 8);   // Kunal: Mechanics, returned late
        addLoan(lib, "03714802725", 10, 40, 30);  // Yash: Let Us C, returned on time
        addLoan(lib, "04314802725", 21, 35, 22);  // Nikita: White Tiger, returned on time

        // one of those late returns has been paid already
        for (const auto& v : lib.issues().unpaidFines())
            if (v.enrollmentNo == "03414802723") lib.markFinePaid(v.record.id());

        std::cout << "Seeded " << lib.students().count() << " students, " << lib.books().all().size()
                  << " titles. Admin login: admin / " << adminPassword << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Seeding failed: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
