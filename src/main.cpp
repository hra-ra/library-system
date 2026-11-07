// Web server: JSON API under /api, static dashboard files from web/.
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>

#include "auth.h"
#include "crow_all.h"
#include "email_service.h"
#include "library_service.h"
#include "reminder_service.h"
#include "util.h"

namespace {

// ---------- small helpers ----------
std::string trim(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string getStr(const crow::json::rvalue& b, const char* key) {
    if (!b.has(key)) return "";
    const auto& v = b[key];
    if (v.t() == crow::json::type::String) return trim(v.s());
    if (v.t() == crow::json::type::Number) {
        std::ostringstream os;
        os << v.d();
        return os.str();
    }
    return "";
}

double getNum(const crow::json::rvalue& b, const char* key, double fallback = 0) {
    if (!b.has(key)) return fallback;
    const auto& v = b[key];
    if (v.t() == crow::json::type::Number) return v.d();
    if (v.t() == crow::json::type::String) {
        try { return std::stod(v.s()); } catch (...) {}
    }
    return fallback;
}

crow::json::rvalue parseBody(const crow::request& req) {
    auto body = crow::json::load(req.body);
    if (!body) throw ValidationException("Request body is not valid JSON");
    return body;
}

crow::response json(int code, crow::json::wvalue body) {
    crow::response res(code, body.dump());
    res.set_header("Content-Type", "application/json");
    return res;
}

crow::response errorJson(int code, const std::string& msg) {
    crow::json::wvalue b;
    b["error"] = msg;
    return json(code, std::move(b));
}

std::string cookieValue(const crow::request& req, const std::string& name) {
    std::string header = req.get_header_value("Cookie");
    std::stringstream ss(header);
    std::string part;
    while (std::getline(ss, part, ';')) {
        part = trim(part);
        if (part.rfind(name + "=", 0) == 0) return part.substr(name.size() + 1);
    }
    return "";
}

// ---------- JSON mappers ----------
crow::json::wvalue studentJson(const Student& s, int booksOut) {
    crow::json::wvalue j;
    j["id"] = s.id();
    j["enrollment_no"] = s.enrollmentNo();
    j["name"] = s.name();
    j["email"] = s.email();
    j["phone"] = s.phone();
    j["department"] = s.department();
    j["year"] = s.year();
    j["status"] = s.status();
    j["books_out"] = booksOut;
    return j;
}

crow::json::wvalue bookJson(const BookStock& b) {
    crow::json::wvalue j;
    j["id"] = b.book.id();
    j["title"] = b.book.title();
    j["author"] = b.book.author();
    j["isbn"] = b.book.isbn();
    j["category"] = b.book.category();
    j["price"] = b.book.price();
    j["total_copies"] = b.totalCopies;
    j["available_copies"] = b.availableCopies;
    return j;
}

crow::json::wvalue issueJson(LibraryService& lib, const IssueView& v) {
    const std::string today = util::today();
    crow::json::wvalue j;
    j["id"] = v.record.id();
    j["student"] = v.studentName;
    j["enrollment_no"] = v.enrollmentNo;
    j["email"] = v.studentEmail;
    j["book"] = v.bookTitle;
    j["author"] = v.bookAuthor;
    j["book_id"] = v.bookId;
    j["copy_no"] = v.copyNo;
    j["issued_on"] = v.record.issuedOn();
    j["due_on"] = v.record.dueOn();
    j["returned_on"] = v.record.returnedOn();
    j["days_overdue"] = lib.daysOverdue(v);
    j["days_left"] = v.record.isReturned() ? 0 : util::daysBetween(today, v.record.dueOn());
    j["fine"] = lib.fineFor(v);
    j["fine_paid"] = v.record.finePaid();
    j["status"] = v.record.isReturned() ? "returned" : (lib.daysOverdue(v) > 0 ? "overdue" : "out");
    return j;
}

crow::json::wvalue issueList(LibraryService& lib, const std::vector<IssueView>& rows) {
    std::vector<crow::json::wvalue> items;
    for (const auto& v : rows) items.push_back(issueJson(lib, v));
    return crow::json::wvalue(items);
}

Student studentFromBody(const crow::json::rvalue& b, int id) {
    return Student(id, getStr(b, "name"), getStr(b, "email"), getStr(b, "phone"), getStr(b, "enrollment_no"),
                   getStr(b, "department"), static_cast<int>(getNum(b, "year", 0)),
                   getStr(b, "status").empty() ? "active" : getStr(b, "status"));
}

Book bookFromBody(const crow::json::rvalue& b, int id) {
    return Book(id, getStr(b, "title"), getStr(b, "author"), getStr(b, "isbn"), getStr(b, "category"),
                getNum(b, "price", 0));
}

int httpCodeFor(const LibraryException& e) {
    if (dynamic_cast<const AuthException*>(&e)) return 401;
    if (dynamic_cast<const NotFoundException*>(&e)) return 404;
    if (dynamic_cast<const ValidationException*>(&e)) return 400;
    if (dynamic_cast<const DuplicateException*>(&e)) return 409;
    return 409;  // a rule was broken (limit reached, unpaid fine, no copy left ...); Crow has no 422
}

}  // namespace

int main() {
    const char* dbEnv = std::getenv("LIBRARY_DB");
    const char* portEnv = std::getenv("PORT");
    const int port = portEnv ? std::atoi(portEnv) : 18080;

    Database db(dbEnv ? dbEnv : "data/library.db");
    LibraryService lib(db);
    AuthService auth(lib.librarians());
    auto mailer = makeEmailServiceFromEnv("outbox");
    ReminderService reminders(lib, *mailer);

    if (lib.librarians().count() == 0)
        std::cerr << "No librarian account yet. Run ./seed_data first.\n";

    crow::SimpleApp app;
    app.loglevel(crow::LogLevel::Warning);

    // Wraps an API handler: checks the session cookie, turns exceptions into JSON errors.
    using Handler = std::function<crow::response(const crow::request&)>;
    auto guarded = [&](const crow::request& req, const Handler& handler) -> crow::response {
        try {
            if (!auth.validate(cookieValue(req, "session"))) return errorJson(401, "Please sign in");
            return handler(req);
        } catch (const LibraryException& e) {
            return errorJson(httpCodeFor(e), e.what());
        } catch (const std::exception& e) {
            std::cerr << "Unexpected error: " << e.what() << "\n";
            return errorJson(500, "Something went wrong on the server");
        }
    };

    // ---------- pages & assets ----------
    CROW_ROUTE(app, "/")([](const crow::request&, crow::response& res) {
        res.set_static_file_info("web/index.html");
        res.end();
    });
    CROW_ROUTE(app, "/assets/<string>")([](const crow::request&, crow::response& res, const std::string& name) {
        if (name.find("..") != std::string::npos || name.find('/') != std::string::npos) {
            res.code = 400;
            res.end();
            return;
        }
        res.set_static_file_info("web/assets/" + name);
        res.end();
    });

    // ---------- auth ----------
    CROW_ROUTE(app, "/api/login").methods("POST"_method)([&](const crow::request& req) {
        try {
            auto b = parseBody(req);
            std::string token = auth.login(getStr(b, "username"), getStr(b, "password"));
            auto session = auth.validate(token);
            crow::json::wvalue out;
            out["name"] = session->name;
            out["username"] = session->username;
            crow::response res = json(200, std::move(out));
            res.add_header("Set-Cookie", "session=" + token + "; HttpOnly; SameSite=Strict; Path=/");
            return res;
        } catch (const LibraryException& e) {
            return errorJson(httpCodeFor(e), e.what());
        }
    });

    CROW_ROUTE(app, "/api/logout").methods("POST"_method)([&](const crow::request& req) {
        auth.logout(cookieValue(req, "session"));
        crow::response res = json(200, crow::json::wvalue());
        res.add_header("Set-Cookie", "session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
        return res;
    });

    CROW_ROUTE(app, "/api/me")([&](const crow::request& req) {
        auto session = auth.validate(cookieValue(req, "session"));
        if (!session) return errorJson(401, "Please sign in");
        crow::json::wvalue out;
        out["name"] = session->name;
        out["username"] = session->username;
        out["library_name"] = lib.settings().get("library_name", "Central Library");
        return json(200, std::move(out));
    });

    // ---------- dashboard ----------
    CROW_ROUTE(app, "/api/dashboard")([&](const crow::request& req) {
        return guarded(req, [&](const crow::request&) {
            const std::string today = util::today();
            DashboardStats s = lib.stats();
            crow::json::wvalue out;
            out["today"] = today;
            out["library_name"] = lib.settings().get("library_name", "Central Library");
            out["students"] = s.students;
            out["titles"] = s.titles;
            out["copies"] = s.copies;
            out["copies_out"] = s.copiesOut;
            out["overdue"] = s.overdue;
            out["due_soon"] = s.dueSoon;
            out["unpaid_fine_count"] = s.unpaidFineCount;
            out["unpaid_fine_total"] = s.unpaidFineTotal;
            out["accruing_fine_total"] = s.accruingFineTotal;
            out["due_soon_list"] = issueList(lib, lib.issues().openDueBetween(today, util::addDays(today, 3)));
            out["overdue_list"] = issueList(lib, lib.issues().openOverdue(today));
            return json(200, std::move(out));
        });
    });

    // ---------- students ----------
    CROW_ROUTE(app, "/api/students")([&](const crow::request& req) {
        return guarded(req, [&](const crow::request& r) {
            const char* q = r.url_params.get("q");
            std::vector<crow::json::wvalue> rows;
            for (const auto& s : lib.students().search(q ? q : ""))
                rows.push_back(studentJson(s, lib.issues().openCountForStudent(s.id())));
            return json(200, crow::json::wvalue(rows));
        });
    });

    CROW_ROUTE(app, "/api/students").methods("POST"_method)([&](const crow::request& req) {
        return guarded(req, [&](const crow::request& r) {
            int id = lib.addStudent(studentFromBody(parseBody(r), 0));
            return json(201, studentJson(*lib.students().findById(id), 0));
        });
    });

    CROW_ROUTE(app, "/api/students/<int>").methods("PUT"_method, "DELETE"_method)(
        [&](const crow::request& req, int id) {
            return guarded(req, [&](const crow::request& r) {
                if (r.method == crow::HTTPMethod::Delete) {
                    lib.deleteStudent(id);
                    return json(200, crow::json::wvalue());
                }
                lib.updateStudent(studentFromBody(parseBody(r), id));
                return json(200, studentJson(*lib.students().findById(id), lib.issues().openCountForStudent(id)));
            });
        });

    // ---------- books ----------
    CROW_ROUTE(app, "/api/books")([&](const crow::request& req) {
        return guarded(req, [&](const crow::request& r) {
            const char* q = r.url_params.get("q");
            std::vector<crow::json::wvalue> rows;
            for (const auto& b : lib.books().search(q ? q : "")) rows.push_back(bookJson(b));
            return json(200, crow::json::wvalue(rows));
        });
    });

    CROW_ROUTE(app, "/api/books").methods("POST"_method)([&](const crow::request& req) {
        return guarded(req, [&](const crow::request& r) {
            auto b = parseBody(r);
            int id = lib.addBook(bookFromBody(b, 0), static_cast<int>(getNum(b, "copies", 1)));
            return json(201, bookJson(*lib.books().stockFor(id)));
        });
    });

    CROW_ROUTE(app, "/api/books/<int>").methods("PUT"_method, "DELETE"_method)(
        [&](const crow::request& req, int id) {
            return guarded(req, [&](const crow::request& r) {
                if (r.method == crow::HTTPMethod::Delete) {
                    lib.deleteBook(id);
                    return json(200, crow::json::wvalue());
                }
                auto b = parseBody(r);
                lib.updateBook(bookFromBody(b, id));
                int extra = static_cast<int>(getNum(b, "add_copies", 0));
                if (extra > 0 && extra <= 100) lib.books().addCopies(id, extra);
                return json(200, bookJson(*lib.books().stockFor(id)));
            });
        });

    // ---------- issues ----------
    CROW_ROUTE(app, "/api/issues")([&](const crow::request& req) {
        return guarded(req, [&](const crow::request& r) {
            const char* status = r.url_params.get("status");
            const char* q = r.url_params.get("q");
            std::string s = status ? status : "open";
            if (s == "overdue") return json(200, issueList(lib, lib.issues().openOverdue(util::today())));
            if (s == "fines") {
                // unpaid fines on returned books, then fines still growing on overdue ones
                auto rows = lib.issues().unpaidFines();
                for (const auto& v : lib.issues().openOverdue(util::today()))
                    if (lib.fineFor(v) > 0) rows.push_back(v);
                return json(200, issueList(lib, rows));
            }
            return json(200, issueList(lib, lib.issues().views(s, q ? q : "")));
        });
    });

    CROW_ROUTE(app, "/api/issues").methods("POST"_method)([&](const crow::request& req) {
        return guarded(req, [&](const crow::request& r) {
            auto b = parseBody(r);
            IssueView v = lib.issueBook(getStr(b, "enrollment_no"), static_cast<int>(getNum(b, "book_id", 0)));
            return json(201, issueJson(lib, v));
        });
    });

    CROW_ROUTE(app, "/api/issues/<int>/return").methods("POST"_method)([&](const crow::request& req, int id) {
        return guarded(req, [&](const crow::request&) { return json(200, issueJson(lib, lib.returnBook(id))); });
    });

    CROW_ROUTE(app, "/api/issues/<int>/pay").methods("POST"_method)([&](const crow::request& req, int id) {
        return guarded(req, [&](const crow::request&) {
            lib.markFinePaid(id);
            return json(200, issueJson(lib, *lib.issues().findViewById(id)));
        });
    });

    // ---------- settings ----------
    auto settingsJson = [&]() {
        crow::json::wvalue j;
        j["library_name"] = lib.settings().get("library_name");
        j["loan_days"] = lib.settings().getInt("loan_days", 14);
        j["fine_per_day"] = lib.settings().getDouble("fine_per_day", 2);
        j["fine_policy"] = lib.settings().get("fine_policy", "capped");
        j["max_books"] = lib.settings().getInt("max_books", 3);
        j["reminder_days"] = lib.settings().get("reminder_days", "3,1,0");
        j["email_sender"] = mailer->name();
        return j;
    };

    CROW_ROUTE(app, "/api/settings")([&](const crow::request& req) {
        return guarded(req, [&](const crow::request&) { return json(200, settingsJson()); });
    });

    CROW_ROUTE(app, "/api/settings").methods("PUT"_method)([&](const crow::request& req) {
        return guarded(req, [&](const crow::request& r) {
            auto b = parseBody(r);
            auto inRange = [&](const char* key, double lo, double hi, const char* label) {
                if (!b.has(key)) return;
                double v = getNum(b, key, -1);
                if (v < lo || v > hi) throw ValidationException(std::string(label) + " must be between " +
                                                               std::to_string(static_cast<int>(lo)) + " and " +
                                                               std::to_string(static_cast<int>(hi)));
                lib.settings().set(key, getStr(b, key));
            };
            inRange("loan_days", 1, 90, "Loan period");
            inRange("fine_per_day", 0, 100, "Fine per day");
            inRange("max_books", 1, 20, "Books per student");
            if (b.has("fine_policy")) {
                std::string p = getStr(b, "fine_policy");
                if (p != "per_day" && p != "capped") throw ValidationException("Unknown fine rule");
                lib.settings().set("fine_policy", p);
            }
            if (b.has("library_name") && !getStr(b, "library_name").empty())
                lib.settings().set("library_name", getStr(b, "library_name"));
            if (b.has("reminder_days")) {
                std::string days = getStr(b, "reminder_days");
                for (char c : days)
                    if (!std::isdigit(static_cast<unsigned char>(c)) && c != ',')
                        throw ValidationException("Reminder days should look like 3,1,0");
                lib.settings().set("reminder_days", days);
            }
            return json(200, settingsJson());
        });
    });

    // ---------- reminders ----------
    CROW_ROUTE(app, "/api/emails")([&](const crow::request& req) {
        return guarded(req, [&](const crow::request&) {
            std::vector<crow::json::wvalue> rows;
            for (const auto& e : lib.emailLog().recent(100)) {
                crow::json::wvalue j;
                j["id"] = e.id;
                j["issue_id"] = e.issueId;
                j["kind"] = e.kind;
                j["sent_on"] = e.sentOn;
                j["recipient"] = e.recipient;
                j["status"] = e.status;
                j["student"] = e.studentName;
                j["book"] = e.bookTitle;
                rows.push_back(std::move(j));
            }
            return json(200, crow::json::wvalue(rows));
        });
    });

    CROW_ROUTE(app, "/api/reminders/run").methods("POST"_method)([&](const crow::request& req) {
        return guarded(req, [&](const crow::request& r) {
            bool dry = false;
            if (!r.body.empty()) {
                auto b = crow::json::load(r.body);
                if (b && b.has("dry_run")) dry = b["dry_run"].b();
            }
            std::vector<crow::json::wvalue> rows;
            int sent = 0;
            for (const auto& o : reminders.run(dry)) {
                crow::json::wvalue j;
                j["issue_id"] = o.issueId;
                j["student"] = o.studentName;
                j["email"] = o.email;
                j["kind"] = o.kind;
                j["subject"] = o.subject;
                j["sent"] = o.sent;
                j["note"] = o.note;
                if (o.sent) ++sent;
                rows.push_back(std::move(j));
            }
            crow::json::wvalue out;
            out["dry_run"] = dry;
            out["count"] = static_cast<int>(rows.size());
            out["sent"] = sent;
            out["items"] = crow::json::wvalue(rows);
            return json(200, std::move(out));
        });
    });

    std::cout << "Library dashboard running at http://localhost:" << port << "\n";
    app.port(port).concurrency(1).run();  // one worker thread: SQLite access stays simple and safe
    return 0;
}
