# Library Management System

A college library desk in C++: student records, book catalogue, issuing and returning,
late fines, and automatic email reminders before books fall due. Built for an OOP lab
project.

- **Backend:** C++17, [Crow](https://github.com/CrowCpp/Crow) web framework, SQLite, libcurl (SMTP)
- **Frontend:** plain HTML, CSS and JavaScript (no framework), served by the C++ server
- **Tests:** 60 checks in `tests/test_library.cpp`, run with `ctest`

## What it does

| Area | Details |
|---|---|
| Staff login | Salted SHA-256 password hashes, session cookie (HttpOnly) |
| Students | Enrollment no., name, email, phone, department, year, active/inactive. Add, edit, search, delete (blocked if they have loan history) |
| Books | Titles with individual copies, price, category, ISBN. Add more copies later |
| Issue & return | Enrollment number plus book. Due date = today + loan period. A student can't borrow if they hit the book limit, have an unpaid fine, or have an overdue book |
| Fines | Calculated on return, shown live while a book is overdue. Two rules: daily rate, or daily rate capped at the book's price. Marked paid at the counter |
| Reminders | Email 3, 1 and 0 days before the due date, then every 3 days once overdue. Every mail is logged, so nobody gets the same one twice |
| Settings | Library name, loan days, books per student, fine rate, fine rule, reminder days |

Not included yet (next steps): a student-facing login, reports/CSV export, renewals,
lost/damaged book handling.

## Quickest way: VS Code

1. Install the compiler tools and libraries once (Mac: `xcode-select --install`, then
   `brew install cmake sqlite curl openssl asio`).
2. Open the `library-system` folder in VS Code and accept the prompt to install the
   recommended extensions (C/C++ and CMake Tools).
3. Press **Cmd/Ctrl+Shift+P**, choose **Tasks: Run Task**, then run in order:
   **1. Build**, **2. Create demo data (once)**, **3. Run server**.
4. Open http://localhost:18080 and sign in with admin / admin123.

The same tasks menu also has **Run tests** and **Preview reminder emails**.
The sections below cover the same steps from a plain terminal.

## Build

Ubuntu/Debian:

    sudo apt install g++ cmake make libsqlite3-dev libcurl4-openssl-dev libssl-dev libasio-dev

macOS (Homebrew):

    brew install cmake sqlite curl openssl asio

Then, from the project folder:

    mkdir build && cd build
    cmake ..
    make -j4
    ctest --output-on-failure      # optional: run the tests

(The first compile of `main.cpp` takes a minute because Crow is a big header.)

## Run

Run everything **from the project root** (the server looks for `web/` and `data/` there):

    ./build/seed_data              # once: creates demo data and the admin login
    ./build/library_server         # then open http://localhost:18080

Demo login: **admin / admin123**. To pick another password: `./build/seed_data MyPassword`.
Change it before showing the project to anyone.

Environment variables (all optional):

| Variable | Default | Purpose |
|---|---|---|
| `PORT` | `18080` | Server port |
| `LIBRARY_DB` | `data/library.db` | Database file |
| `SMTP_URL`, `SMTP_USER`, `SMTP_PASS`, `SMTP_FROM` | not set | Real email. Without them, mails are written to `outbox/` as text files |
| `LIBRARY_TODAY` | today | Pretend it's another date (`2026-11-20`), handy for demos and tests |

### Sending real email

Gmail works for testing. Create an *app password* (needs 2-step verification), then:

    SMTP_URL=smtps://smtp.gmail.com:465 \
    SMTP_USER=you@gmail.com \
    SMTP_PASS=your-app-password \
    ./build/library_server

For anything beyond a demo, use a transactional provider (SES, SendGrid, Resend) so mail doesn't get throttled.

### Daily reminders

The check is a separate program so it can run on a schedule, whether or not the server is up:

    ./build/reminder_job --dry-run     # show who would get an email
    ./build/reminder_job               # send

Cron example (every day at 9 AM):

    0 9 * * *  cd /path/to/library-system && ./build/reminder_job

The dashboard's **Reminders** page has the same two buttons for demos.

### Demo tip

The seeded loans are dated relative to the day you seed. To see overdue books and reminders
on a fresh clone, seed and run normally. To replay a specific day, set `LIBRARY_TODAY` for both
`seed_data` and `library_server`.

## Project layout

    include/        class declarations (models, repositories, services, interfaces)
    src/            implementations, plus main.cpp (web server), reminder_job.cpp, seed_data.cpp
    web/            dashboard (index.html, assets/app.css, assets/app.js)
    tests/          unit tests
    docs/DESIGN.md  class diagram, database schema, API list, design notes
    third_party/    Crow single header

## Design in short

See `docs/DESIGN.md` for the class diagram, schema and API. The key ideas:

- **Inheritance:** abstract `Person` → `Student`, `Librarian`
- **Strategy + polymorphism:** `IFinePolicy` → `PerDayFinePolicy`, `CappedFinePolicy`; the rule is picked from settings at runtime
- **Dependency inversion:** `IEmailService` → `SmtpEmailService`, `FileEmailService`
- **Templates:** `Repository<T>` is the common interface for student, book and issue storage
- **Encapsulation:** SQL lives only in the repository classes; business rules only in `LibraryService`
- **RAII:** `Database` and `Statement` open and release SQLite resources automatically
- **Exceptions:** a `LibraryException` hierarchy, translated to HTTP errors at the edge

## Known limits

- Single-librarian-desk scale: the server runs one worker thread against SQLite
- Passwords use salted SHA-256, which is fine for a college project; use bcrypt/argon2 for production
- No HTTPS; put it behind a reverse proxy (nginx/Caddy) if it ever leaves a local network
- Sessions are kept in memory, so restarting the server signs everyone out
