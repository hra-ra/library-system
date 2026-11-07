#pragma once
#include <string>
#include <vector>

#include "email_service.h"
#include "library_service.h"

struct ReminderOutcome {
    int issueId;
    std::string studentName, email, kind, subject;
    bool sent;
    std::string note;
};

// Works out who needs a nudge today, sends the mail, and logs it so nobody
// gets the same reminder twice. Used by both the daily job and the dashboard button.
class ReminderService {
public:
    ReminderService(LibraryService& library, IEmailService& mailer) : library_(library), mailer_(mailer) {}
    std::vector<ReminderOutcome> run(bool dryRun);

private:
    EmailMessage compose(const IssueView& v, int daysLeft, double fine);
    LibraryService& library_;
    IEmailService& mailer_;
};
