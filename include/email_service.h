#pragma once
#include <memory>
#include <string>

struct EmailMessage {
    std::string to;
    std::string subject;
    std::string body;
};

// The rest of the program only depends on this interface, so the real SMTP
// sender can be swapped for a file-based one while developing.
class IEmailService {
public:
    virtual ~IEmailService() = default;
    virtual bool send(const EmailMessage& msg, std::string& error) = 0;
    virtual std::string name() const = 0;
};

struct SmtpConfig {
    std::string url;       // e.g. smtps://smtp.gmail.com:465
    std::string user;
    std::string password;  // app password, never the real account password
    std::string from;
};

class SmtpEmailService : public IEmailService {
public:
    explicit SmtpEmailService(SmtpConfig cfg) : cfg_(std::move(cfg)) {}
    bool send(const EmailMessage& msg, std::string& error) override;
    std::string name() const override { return "smtp"; }

private:
    SmtpConfig cfg_;
};

// Development sender: writes each mail into a folder instead of sending it.
class FileEmailService : public IEmailService {
public:
    explicit FileEmailService(std::string dir) : dir_(std::move(dir)) {}
    bool send(const EmailMessage& msg, std::string& error) override;
    std::string name() const override { return "outbox folder"; }

private:
    std::string dir_;
    int counter_ = 0;
};

// Uses SMTP_URL / SMTP_USER / SMTP_PASS / SMTP_FROM if they are set, else the outbox folder.
std::unique_ptr<IEmailService> makeEmailServiceFromEnv(const std::string& outboxDir);
