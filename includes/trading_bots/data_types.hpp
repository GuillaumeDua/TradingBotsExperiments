#pragma once

#include <string>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <cassert>

namespace trading_bots::data_types {
    struct rate {
        using value_type = float;
        constexpr rate(value_type arg)
        : storage { arg }
        {
            ensure_datas_integrity();
        }
        constexpr rate & operator=(rate arg) {
            arg.ensure_datas_integrity();
            storage = static_cast<value_type>(arg);
            return *this;
        }
        constexpr operator value_type() const {
            return storage;
        }
        constexpr auto value() const {
            return storage;
        }

        constexpr void ensure_datas_integrity() const {
            if (storage < 0.f or storage > 100.f)
                throw std::logic_error{"rate must be [0.0, 100.0]"};
        }

    private:
        value_type storage = 0;
    };
    struct record {
        // record() = default;
        // record(record&&) = default;
        // record(const record &) = default;
        // record& operator=(record&&) = default;

        bool operator==(const record & other) const {
            return Date == other.Date;
        }

        float price_fluctuation_rate() const {
            return CloseLast / Open;
        }
        float variation() const {
            return Open - CloseLast;
        }
        float variation_rate() const {
            return price_fluctuation_rate() - 1;
        }
        float amplitude() const {
            return High - Low;
        }
        float amplitude_rate() const {
            return (High / Low) - 1;
        }

        float       Low,  High, Open;
        std::string Volume;
        float       CloseLast;
        std::string Date;

        friend std::ostream & operator<<(std::ostream &, const record&);

        void ensure_datas_integrity() const {
            if (Date.empty())
                throw std::invalid_argument{"trades::record : corrupted datas Date"};
            if (High < Low)
                throw std::invalid_argument{"trades::record : corrupted datas High/Low"};
        }
    };
    static_assert(std::is_aggregate_v<record>);
    std::ostream & operator<<(std::ostream & os, const record& value) {
        return os
            << "record={ "
            << std::setw(10) << value.Date << ','
            << std::setw(10) << value.CloseLast << ','
            << std::setw(10) << value.Open << ','
            << std::setw(10) << value.High << ','
            << std::setw(10) << value.Low
            << " }"
        ;
    }

    struct wallet {

        using amount_type = double;

        void update(const record & value) {
            currency_price = value.CloseLast;
        }
        auto to_USDT() const {
            return currency_amount * currency_price;
        }
        void remove_USDT_amount(amount_type amount) {
            if (amount <= 0)
                throw std::invalid_argument{"data_types::wallet::remove_USDT_amount"};

            assert(to_USDT() >= amount);

            const auto currency_qty = amount / currency_price;
            assert(currency_qty >= 0);
            // assert(currency_amount >= currency_qty);
            currency_amount -= std::min(currency_amount, currency_qty); // double precision
        }
        void add_USDT_amount(amount_type amount) {
            if (amount <= 0)
                throw std::invalid_argument{"data_types::wallet::add_USDT_amount"};

            const auto currency_qty = amount / currency_price;
            assert(currency_qty >= 0);
            currency_amount += currency_qty;
        }

        friend std::ostream & operator<<(std::ostream &, const wallet&);
    private:
        amount_type currency_amount = 0;
        amount_type currency_price = 0;
    };
    std::ostream & operator<<(std::ostream & os, const wallet& value) {
        return os
            << "wallet={ "
            << value.currency_amount << " at "
            << value.currency_price
            << " == " << value.to_USDT()
            << " }"
        ;
    }

}
