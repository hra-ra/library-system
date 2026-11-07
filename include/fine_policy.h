#pragma once
#include <algorithm>
#include <memory>
#include <string>

// Strategy pattern: the service only knows about IFinePolicy, so a new
// rule can be added without touching the issue/return code.
class IFinePolicy {
public:
    virtual ~IFinePolicy() = default;
    virtual double calculate(int daysLate, double bookPrice) const = 0;
    virtual std::string name() const = 0;
};

// Plain rate per day, no upper limit.
class PerDayFinePolicy : public IFinePolicy {
public:
    explicit PerDayFinePolicy(double ratePerDay) : rate_(ratePerDay) {}
    double calculate(int daysLate, double) const override {
        return daysLate > 0 ? daysLate * rate_ : 0.0;
    }
    std::string name() const override { return "per_day"; }

private:
    double rate_;
};

// Same daily rate, but a fine can never be more than the price of the book.
class CappedFinePolicy : public IFinePolicy {
public:
    explicit CappedFinePolicy(double ratePerDay) : rate_(ratePerDay) {}
    double calculate(int daysLate, double bookPrice) const override {
        if (daysLate <= 0) return 0.0;
        double fine = daysLate * rate_;
        return bookPrice > 0 ? std::min(fine, bookPrice) : fine;
    }
    std::string name() const override { return "capped"; }

private:
    double rate_;
};

inline std::unique_ptr<IFinePolicy> makeFinePolicy(const std::string& name, double ratePerDay) {
    if (name == "capped") return std::make_unique<CappedFinePolicy>(ratePerDay);
    return std::make_unique<PerDayFinePolicy>(ratePerDay);
}
