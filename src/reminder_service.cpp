#include "reminder_service.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <set>
#include <sstream>

#include "util.h"

namespace {

std::string prettyDate(const std::string& iso) {
    std::tm tm = util::parseDate(iso);
    char buf[32];
    std::strftime(buf, sizeof buf, "%d %b %Y", &tm);
    return buf;
}

std::string money(double v) {
    std::ostringstream os;
    if (std::fabs(v - std::round(v)) < 0.005) os << "Rs. " << static_cast<long>(std::round(v));
    else os.setf(std::ios::fixed), os.precision(2), os << "Rs. " << v;
    return os.str();
}

std::string firstName(const std::string& full) {
    auto sp = full.find(' ');
    return sp == std::string::npos ? full : full.substr(0, sp);
}

std::set<int> parseDays(const std::string& csv) {
    std::set<int> days;
    std::stringstream ss(csv);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        try { days.insert(std::stoi(tok)); } catch (...) {}
    }
    return days;
}

std::string kindFor(int daysLeft) {
    if (daysLeft < 0) return "overdue";
    if (daysLeft == 0) return "due_today";
    return "due_in_" + std::to_string(daysLeft);
}

}  // namespace

EmailMessage ReminderService::compose(const IssueView& v, int daysLeft, double fine) {
    const std::string lib = library_.settings().get("library_name", "Central Library");
    const double rate = library_.settings().getDouble("fine_per_day", 2);
    std::ostringstream body;
    EmailMessage m;
    m.to = v.studentEmail;
    body << "Hi " << firstName(v.studentName) << ",\n\n";

    if (daysLeft > 0) {
        m.subject = "Reminder: \"" + v.bookTitle + "\" is due " + (daysLeft == 1 ? "tomorrow" : "in " + std::to_string(daysLeft) + " days");
        body << "Just a reminder that \"" << v.bookTitle << "\" by " << v.bookAuthor << " is due back on "
             << prettyDate(v.record.dueOn()) << ".\n\n"
             << "If it comes back by then there's no charge. After that the fine is " << money(rate) << " per day.\n";
    } else if (daysLeft == 0) {
        m.subject = "Due today: \"" + v.bookTitle + "\"";
        body << "\"" << v.bookTitle << "\" by " << v.bookAuthor << " is due back today.\n\n"
             << "Please return it at the counter before closing to avoid a fine of " << money(rate) << " per day.\n";
    } else {
        const int late = -daysLeft;
        m.subject = "Overdue: \"" + v.bookTitle + "\" (" + std::to_string(late) + (late == 1 ? " day" : " days") + " late)";
        body << "\"" << v.bookTitle << "\" by " << v.bookAuthor << " was due on " << prettyDate(v.record.dueOn())
             << " and is now " << late << (late == 1 ? " day" : " days") << " overdue.\n\n"
             << "The fine so far is " << money(fine) << ". Please return the book as soon as you can; "
             << "you won't be able to borrow anything else until it's back and the fine is cleared.\n";
    }
    body << "\nThanks,\n" << lib << "\n";
    m.body = body.str();
    return m;
}

std::vector<ReminderOutcome> ReminderService::run(bool dryRun) {
    std::vector<ReminderOutcome> results;
    const std::string today = util::today();
    const auto leadDays = parseDays(library_.settings().get("reminder_days", "3,1,0"));
    const int repeat = std::max(1, library_.settings().getInt("overdue_repeat_days", 3));

    for (const auto& v : library_.issues().views("open")) {
        const int daysLeft = util::daysBetween(today, v.record.dueOn());
        if (daysLeft >= 0 && !leadDays.count(daysLeft)) continue;

        const std::string kind = kindFor(daysLeft);
        // overdue mails repeat every few days; the others go out once per loan
        const std::string since = daysLeft < 0 ? util::addDays(today, -(repeat - 1)) : "";
        if (library_.emailLog().alreadySent(v.record.id(), kind, since)) continue;

        EmailMessage msg = compose(v, daysLeft, daysLeft < 0 ? library_.fineFor(v) : 0.0);
        ReminderOutcome out{v.record.id(), v.studentName, v.studentEmail, kind, msg.subject, false, ""};
        if (dryRun) {
            out.note = "dry run";
        } else {
            std::string err;
            out.sent = mailer_.send(msg, err);
            out.note = out.sent ? mailer_.name() : err;
            library_.emailLog().record(v.record.id(), kind, today, v.studentEmail, out.sent ? "sent" : "failed");
        }
        results.push_back(out);
    }
    return results;
}
