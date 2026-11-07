// Daily reminder job. Meant to be run once a day by cron / Task Scheduler:
//   0 9 * * *  cd /path/to/library-system && ./build/reminder_job
// Add --dry-run to see who would get an email without sending anything.
#include <cstdlib>
#include <iostream>
#include <string>

#include "email_service.h"
#include "reminder_service.h"

int main(int argc, char** argv) {
    bool dryRun = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--dry-run") dryRun = true;

    const char* dbEnv = std::getenv("LIBRARY_DB");
    try {
        Database db(dbEnv ? dbEnv : "data/library.db");
        LibraryService library(db);
        auto mailer = makeEmailServiceFromEnv("outbox");
        ReminderService reminders(library, *mailer);

        auto results = reminders.run(dryRun);
        int sent = 0;
        for (const auto& r : results) {
            std::cout << (dryRun ? "[dry run] " : (r.sent ? "[sent]    " : "[failed]  ")) << r.kind << "  "
                      << r.studentName << " <" << r.email << ">  " << r.subject;
            if (!r.sent && !dryRun) std::cout << "  (" << r.note << ")";
            std::cout << "\n";
            if (r.sent) ++sent;
        }
        std::cout << results.size() << " reminder(s) " << (dryRun ? "would be sent" : "processed")
                  << ", " << sent << " sent via " << mailer->name() << ".\n";
    } catch (const std::exception& e) {
        std::cerr << "reminder_job failed: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
