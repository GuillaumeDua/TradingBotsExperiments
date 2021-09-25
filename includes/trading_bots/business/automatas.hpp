#pragma once

#include <trading_bots/business/data_types.hpp>
#include <trading_bots/business/indices.hpp>

#include <gcl/cx/type_name.hpp> // debug only

#define fwd(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)

namespace trading_bots {

    // todo : enforce values range -> data_type::rate
    struct threshold_type {
        std::size_t buy;
        std::size_t sell;
    };
    struct investment_strategy {
        threshold_type thresholds;
        float investment;
    };
}
namespace trading_bots::automata {
    using amount_type = double;

    struct base {

        base(amount_type initial_amount)
        : current_amount_USD{ initial_amount }
        {}

        using record_type = trading_bots::business::data_types::record;
        void update(const record_type & last_record) {
            investement.update(last_record);
            if (is_bankrupt()) {
                std::runtime_error{"business error : is_bankrupt"};
            }
        }

        auto total_capital() const {
            return investement.to_USDT() + current_amount_USD;
        }
        bool is_bankrupt() const {
            return total_capital() <= 0.f;
        }
        operator bool() const {
            return is_bankrupt();
        }

        void buy_up_to(amount_type value) {
            if (value == 0)
                return;
            std::cout << "\tBuy  : " << value << " / " << investement << '\n';

            const auto amount = std::min(value, current_amount_USD);
            if (amount < 0.f)
                throw std::runtime_error{"business error : cannot BUY less than 0"};
            current_amount_USD -= amount;
            investement.add_USDT_amount(amount);
        }
        void sell_up_to(amount_type value) {
            if (value == 0)
                return;
            std::cout << "\tSell : " << value << " / " << investement << '\n';

            const auto amount = std::min(value, investement.to_USDT());
            if (amount < 0.f)
                throw std::runtime_error{"business error : cannot SELL less than 0"};
            
            current_amount_USD += amount;
            investement.remove_USDT_amount(amount);
        }

    protected:
        amount_type current_amount_USD;
        trading_bots::business::data_types::wallet investement;
    };

    template <typename T>
    concept automata_type =
        std::derived_from<T, base> and
        std::constructible_from<T, amount_type> and
        requires { typename T::components_type; } and
        requires (T& value, T::components_type && c) { value.process(fwd(c)); }
    ;
    struct long_term : base {
        using components_type = std::tuple<>;
        void process(components_type && components) {
            static auto once_dummy = [this](){
                buy_up_to(current_amount_USD);
                return true;
            }();
        }
    };

    template <std::size_t duration>
    requires (duration not_eq 0)
    struct RSI_of { // struct-as-namespace

        struct caca : public base {

            using components_type = std::tuple<
                business::indices::rsi<>
            >;

            caca(amount_type initial_amount)
            : base{ initial_amount }
            {}

            void process(components_type && components) {

                auto & [rsi] = components;

                // strategy
                const auto rsi_value = rsi.value_for_duration(duration);
                if (not rsi_value)
                    return; // not enough records to process
                
                std::cout
                    << gcl::cx::type_name_v<std::remove_cvref_t<decltype(*this)>> << '\n'
                    << "\trsi = " << *rsi_value << '\n'
                ;

                if (*rsi_value < 50)
                    buy_up_to(current_amount_USD * (1 - (*rsi_value / 50)));
                if (*rsi_value > 50)
                    sell_up_to(investement.to_USDT() * ((*rsi_value / 50) - 1));
            }
        };

        template <float trend_fluctuation_rate>
        struct proportional_with_trends : public base {

            using components_type = std::tuple<
                business::indices::rsi<>,
                business::indices::trend<>
            >;

            proportional_with_trends(amount_type initial_amount)
            : base{ initial_amount }
            {}

            void process(components_type && components) {

                auto & [rsi, trend] = components;

                // strategy
                const auto rsi_value = rsi.value_for_duration(duration);
                const auto trend_value = trend.value_for_duration(duration, trend_fluctuation_rate);
                if (not rsi_value or    // rsi   : not enough records to process
                    not trend_value or  // trend : not enough records to process
                    *(trend_value) == business::indices::trend_value_type::stable // no observal trend
                ) return;

                std::cout
                    << gcl::cx::type_name_v<std::remove_cvref_t<decltype(*this)>> << '\n'
                    << "\trsi = " << *rsi_value << '\n'
                ;

                if (*trend_value == business::indices::trend_value_type::up and
                    *rsi_value < 50)
                    buy_up_to(current_amount_USD * (1 - (*rsi_value / 50)));
                if (*trend_value == business::indices::trend_value_type::down and
                    *rsi_value > 50)
                    sell_up_to(investement.to_USDT() * ((*rsi_value / 50) - 1));
            }
        };

        using proportional = proportional_with_trends<0.f>;

        template <investment_strategy strategy>
        struct thresholds : public base {

            using components_type = std::tuple<
                business::indices::rsi<>
            >;

            thresholds(amount_type initial_amount)
            : base{ initial_amount }
            {}

            void process(components_type && components) {

                auto & [rsi] = components;

                // strategy
                const auto rsi_value = rsi.value_for_duration(duration);
                if (not rsi_value)
                    return; // not enough records to process
                
                std::cout
                    << gcl::cx::type_name_v<std::remove_cvref_t<decltype(*this)>> << '\n'
                    << "\trsi = " << *rsi_value << '\n'
                ;
                if (*rsi_value < strategy.thresholds.buy)
                    buy_up_to(current_amount_USD * strategy.investment);
                if (*rsi_value > strategy.thresholds.sell)
                    sell_up_to(investement.to_USDT() * strategy.investment);
            }
        };

        template <investment_strategy strategy, float trend_fluctuation>
        requires (duration not_eq 0)
        struct thresholds_and_trends : public base {

            using components_type = std::tuple<
                business::indices::rsi<>,
                business::indices::trend<>
            >;

            thresholds_and_trends(amount_type initial_amount)
            : base{ initial_amount }
            {}

            void process(components_type && components) {

                auto & [rsi, trend] = components;

                // strategy
                const auto rsi_value = rsi.value_for_duration(duration);
                const auto trend_value = trend.value_for_duration(duration, trend_fluctuation);
                if (not rsi_value or    // rsi   : not enough records to process
                    not trend_value or  // trend : not enough records to process
                    *(trend_value) == business::indices::trend_value_type::stable // no observal trend
                ) return;
                
                std::cout
                    << gcl::cx::type_name_v<std::remove_cvref_t<decltype(*this)>> << '\n'
                    << "\trsi = " << *rsi_value << '\n'
                ;
                if (trend_value == business::indices::trend_value_type::up and
                    *rsi_value < strategy.thresholds.buy)
                    buy_up_to(current_amount_USD * strategy.investment);
                if (trend_value == business::indices::trend_value_type::down and
                    *rsi_value > strategy.thresholds.sell)
                    sell_up_to(investement.to_USDT() * strategy.investment);
            }
        };
    };

    // --- contract checks
    static_assert(automata_type<long_term>);
    static_assert(automata_type<RSI_of<1>::proportional>);
    static_assert(automata_type<RSI_of<1>::proportional_with_trends<0.f>>);
    static_assert(automata_type<
        RSI_of<1>::thresholds<investment_strategy{ threshold_type{ 30, 70 }, 0.5f}>
    >);
    static_assert(automata_type<
        RSI_of<1>::thresholds_and_trends<investment_strategy{ threshold_type{ 30, 70 }, 0.5f}, 0.f>
    >);
}

#undef fwd
