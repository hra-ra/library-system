#include "email_service.h"

#include <curl/curl.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace {

struct Payload {
    std::string data;
    size_t offset = 0;
};

size_t readCallback(char* dest, size_t size, size_t nmemb, void* userp) {
    auto* p = static_cast<Payload*>(userp);
    size_t room = size * nmemb;
    size_t left = p->data.size() - p->offset;
    size_t n = left < room ? left : room;
    if (n == 0) return 0;
    p->data.copy(dest, n, p->offset);
    p->offset += n;
    return n;
}

std::string rfcDate() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[64];
    std::strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S +0000", &tm);
    return buf;
}

std::string safeHeader(std::string s) {
    for (char& c : s)
        if (c == '\r' || c == '\n') c = ' ';  // stops header injection
    return s;
}

}  // namespace

bool SmtpEmailService::send(const EmailMessage& msg, std::string& error) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "could not start curl";
        return false;
    }

    Payload payload;
    payload.data = "Date: " + rfcDate() + "\r\n" + "To: " + safeHeader(msg.to) + "\r\n" +
                   "From: " + safeHeader(cfg_.from) + "\r\n" + "Subject: " + safeHeader(msg.subject) + "\r\n" +
                   "Content-Type: text/plain; charset=UTF-8\r\n\r\n" + msg.body + "\r\n";

    curl_slist* rcpt = curl_slist_append(nullptr, msg.to.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, cfg_.url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, cfg_.user.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, cfg_.password.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, cfg_.from.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, rcpt);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &payload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) error = curl_easy_strerror(rc);
    curl_slist_free_all(rcpt);
    curl_easy_cleanup(curl);
    return rc == CURLE_OK;
}

bool FileEmailService::send(const EmailMessage& msg, std::string& error) {
    try {
        std::filesystem::create_directories(dir_);
        std::time_t now = std::time(nullptr);
        std::string file = dir_ + "/mail_" + std::to_string(now) + "_" + std::to_string(++counter_) + ".txt";
        std::ofstream out(file);
        if (!out) {
            error = "cannot write " + file;
            return false;
        }
        out << "To: " << msg.to << "\nSubject: " << msg.subject << "\n\n" << msg.body << "\n";
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::unique_ptr<IEmailService> makeEmailServiceFromEnv(const std::string& outboxDir) {
    const char* url = std::getenv("SMTP_URL");
    const char* user = std::getenv("SMTP_USER");
    const char* pass = std::getenv("SMTP_PASS");
    const char* from = std::getenv("SMTP_FROM");
    if (url && user && pass) {
        SmtpConfig cfg{url, user, pass, from ? from : user};
        return std::make_unique<SmtpEmailService>(cfg);
    }
    return std::make_unique<FileEmailService>(outboxDir);
}
