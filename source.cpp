
#include <trading_bots/details/io.hpp>
#include <trading_bots/details/tuple_view.hpp>

#include <memory>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <stack>
#include <queue>
#include <optional>
#include <algorithm>
#include <numeric>
#include <variant>
#include <stdexcept>
#include <cassert>
#include <cmath>

// todo :
//
//  check if market trend is up or down before buying/selling
//  
//  automata that does nothing (no sell, buy, just update)
//
//  auto-update amounts using market datas (stocks * value) instead of dispatching updates
//
//  better datas (smaller timegaps)
//
//  Warning : open/close not contiguous -> small but noticable margin

#define fwd(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)

using namespace std::literals;

namespace gcl::cx {
    template <typename T>
    static constexpr /*consteval*/ std::string_view type_name(/*no parameters allowed*/)
    {
    #if defined(__GNUC__) or defined(__clang__)
        std::string_view str_view = __PRETTY_FUNCTION__;
        str_view.remove_prefix(str_view.find(__FUNCTION__) + sizeof(__FUNCTION__));
        const char prefix[] = "T = ";
        str_view.remove_prefix(str_view.find(prefix) + sizeof(prefix) - 1);
        str_view.remove_suffix(str_view.length() - str_view.find_first_of(";]"));
    #elif defined(_MSC_VER)
        std::string_view str_view = __FUNCSIG__;
        str_view.remove_prefix(str_view.find(__func__) + sizeof(__func__));
        if (auto enum_token_pos = str_view.find("enum "); enum_token_pos == 0)
            str_view.remove_prefix(enum_token_pos + sizeof("enum ") - 1);
        str_view.remove_suffix(str_view.length() - str_view.rfind(">(void)"));
    #else
        static_assert(false, "gcl::cx::typeinfo : unhandled plateform");
    #endif
        return str_view;
    }
    template <typename T>
    constexpr inline auto type_name_v = type_name<T>();
    template <auto value>
    static constexpr std::string_view type_name(/*no parameters allowed*/)
    {
        return type_name<decltype(value)>();
    }

    template <auto value>
    static constexpr std::string_view value_name(/*no parameters allowed*/)
    {
    #if defined(__GNUC__) or defined(__clang__)
        std::string_view str_view = __PRETTY_FUNCTION__;
        str_view.remove_prefix(str_view.find(__FUNCTION__) + sizeof(__FUNCTION__));
        const char prefix[] = "value = ";
        str_view.remove_prefix(str_view.find(prefix) + sizeof(prefix) - 1);
        str_view.remove_suffix(str_view.length() - str_view.find_first_of(";]"));
    #elif defined(_MSC_VER)
        std::string_view str_view = __FUNCSIG__;
        str_view.remove_prefix(str_view.find(__func__) + sizeof(__func__));
        if (auto enum_token_pos = str_view.find("enum "); enum_token_pos == 0)
            str_view.remove_prefix(enum_token_pos + sizeof("enum ") - 1);
        str_view.remove_suffix(str_view.length() - str_view.rfind(">(void)"));
    #else
        static_assert(false, "gcl::cx::typeinfo : unhandled plateform");
    #endif
        return str_view;
    }
    template <auto value>
    constexpr inline auto value_name_v = value_name<value>();
}

namespace trading_bots::input {

    using record_type = trading_bots::data_types::record;
    using amount_type = double;

    struct trend {
        void update(const record_type & input) {
            // todo
        }
    };
    struct last_record {
        void update(const record_type & input) {
            value = input;
        }
        record_type value;
    };

    template <std::size_t max_duration = 14>
    requires (max_duration not_eq 0)
    struct rsi {

        // todo : refactor this, based on CloseLast, not (Open / CloseLast) rate

        void update(const record_type & input) {
            cache.push_front(input);
            if (cache.size() > max_duration)
                cache.pop_back();
            assert(cache.size() <= max_duration);
        }
        
        using value_type = trading_bots::data_types::rate;
        std::optional<value_type> value_for_duration(std::size_t duration) const {
            if (duration == 1)
                throw std::invalid_argument{"rsi::value_for_duration : duration <= 1"};

            if (std::size(cache) < duration)
                return std::nullopt;

            auto range_begin = std::next(std::crbegin(cache));
            auto range_end = std::next(std::crbegin(cache), duration);
            
            const auto effective_duration = (duration - 1.0);
            const amount_type gain_average = std::accumulate(
                range_begin,
                range_end,
                0.0,
                [prev_element_it = std::crbegin(cache)](amount_type result, const record_type & element) mutable {
                    const amount_type variation = ((element.CloseLast / prev_element_it->CloseLast) * 100.0) - 100;
                    ++prev_element_it;
                    if (variation > .0) // is gain
                        return result += variation;
                    return result;
                }
            ) / effective_duration;
            const amount_type loss_average = std::accumulate(
                range_begin,
                range_end,
                0.0,
                [prev_element_it = std::crbegin(cache)](amount_type result, const record_type & element) mutable {
                    const amount_type variation = ((element.CloseLast / prev_element_it->CloseLast) * 100.0) - 100;
                    ++prev_element_it;
                    if (variation < .0) // is loss
                        return result += std::fabs(variation);
                    return result;
                }
            ) / effective_duration;

            const auto result = 100.0 - (100.0 / (
                1.0 +
                gain_average / loss_average
            ));
            return result;
        }

    private:
        std::deque<record_type> cache;
    };
}
namespace trading_bots::automata {
    using amount_type = double;

    struct base {

        base(amount_type initial_amount)
        : current_amount_USD{ initial_amount }
        {}

        void update(const data_types::record & last_record) {
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
        data_types::wallet investement;
    };

    template <typename T>
    concept automata_type =
        std::derived_from<T, base> and
        std::constructible_from<T, amount_type> and
        requires { typename T::components_type; } and
        requires (T& value, T::components_type && c) { value.process(fwd(c)); }
    ;

    template <std::size_t duration>
    requires (duration not_eq 0)
    struct RSI_proportional : public base {

        using components_type = std::tuple<
            trading_bots::input::rsi<>
        >;

        RSI_proportional(amount_type initial_amount)
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
    static_assert(automata_type<RSI_proportional<1>>);

    struct RSI_investment_strategy {
        std::size_t buy_threshold;
        std::size_t sell_threshold;
        float investment_coef;

        // static_assert(investment_coef >= 0.f and investment_coef <= 1.f);
    };

    template <std::size_t duration, RSI_investment_strategy strategy>
    requires (duration not_eq 0)
    struct RSI_thresholds : public base {

        using components_type = std::tuple<
            trading_bots::input::rsi<>
        >;

        RSI_thresholds(amount_type initial_amount)
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
            if (*rsi_value < strategy.buy_threshold)
                buy_up_to(current_amount_USD * strategy.investment_coef);
            if (*rsi_value > strategy.sell_threshold)
                sell_up_to(investement.to_USDT() * strategy.investment_coef);
        }
    };
    static_assert(automata_type<
        RSI_thresholds<1, RSI_investment_strategy{30, 70, 0.5f}>
    >);
}

template <typename ... Ts>
auto make_array_of_variants(auto && ... args) {
    using element_type = std::variant<Ts...>;
    return std::array<element_type, sizeof...(Ts)>{
        Ts{ args... }...
    };
}

template <typename ... automatas_types>
void run_for_datas(const std::string & path, const float initial_amount) {
    using namespace trading_bots;

    auto process_dispatcher = [](auto & features_container, auto & value) constexpr {
        auto features_requested = [&features_container]<template <typename ...> typename T, typename ... Ts>(T<Ts...>){
            return details::tuple_view::make_tuple_view<Ts...>(features_container);
        }(typename std::remove_cvref_t<decltype(value)>::components_type{});
        value.process(std::move(features_requested));
    };

    auto records = details::io::csv::file::generate_datas(path);
    auto features = std::tuple {
        input::last_record{},
        input::rsi{}
    };
    auto automatas = make_array_of_variants<automatas_types...>(initial_amount);

    // process strategies ...
    std::size_t records_quantity = records.size();
    while (not records.empty()) {
        // record
        auto latest_record = [&records](){
            auto value = std::move(records.top());
            records.pop();
            return value;
        }();
        // features
        [&features, &latest_record]<std::size_t ... indexes>(std::index_sequence<indexes...>){
            ((std::get<indexes>(features).update(latest_record)), ...);
        }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<decltype(features)>>>());
        // automatas
        for (auto & element : automatas)
            std::visit([&](auto & value){
                value.update(latest_record);
                process_dispatcher(features, value);
            }, element);
    }

    // show results ...
    std::cout << "\n\nProcessed records : " << records_quantity << '\n';
    for (auto & automata_value : automatas)
        std::visit(
            [initial_amount](auto & value) {
                auto win_or_loss_rate = ((value.total_capital() / initial_amount) * 100) - 100;
                std::cout
                    << '\t' << std::setw(110) << std::left
                    << gcl::cx::type_name_v<std::remove_cvref_t<decltype(value)>>
                    << " : " << std::setw(10) << value.total_capital()
                    << " = " << std::setw(10) << (value.total_capital() - initial_amount)
                    << " | " << (win_or_loss_rate < 0 ? '-' : '+') << ' ' << std::setw(8) << ((value.total_capital() / initial_amount) * 100) - 100 << " %"
                    << '\n'
                ;
            }, automata_value
        );
}

auto main() -> int {
    
    const std::string path_ETH_august_2021 = "./datas/ETH/HistoricalData_1631403583208.csv";
    const std::string path_ETH_2021 = "./datas/ETH/HistoricalData_1631403618344.csv";
    const std::string path_ETH_all = "./datas/ETH/HistoricalData_1631482231024.csv";

    try {
        using namespace trading_bots;
        run_for_datas<
            // RSI_proportional
            automata::RSI_proportional<4>,
            automata::RSI_proportional<6>,
            automata::RSI_proportional<7>,
            automata::RSI_proportional<14>,
            // // RSI_thresholds
            // // 4
            // automata::RSI_thresholds<4, automata::RSI_investment_strategy{30, 70, 0.5f}>,
            // automata::RSI_thresholds<4, automata::RSI_investment_strategy{30, 70, 0.25f}>,
            // automata::RSI_thresholds<4, automata::RSI_investment_strategy{40, 60, 0.5f}>,
            // automata::RSI_thresholds<4, automata::RSI_investment_strategy{40, 60, 0.25f}>,
            // // 7
            // automata::RSI_thresholds<7, automata::RSI_investment_strategy{30, 70, 0.5f}>,
            // automata::RSI_thresholds<7, automata::RSI_investment_strategy{30, 70, 0.25f}>,
            // automata::RSI_thresholds<7, automata::RSI_investment_strategy{40, 60, 0.5f}>,
            // automata::RSI_thresholds<7, automata::RSI_investment_strategy{40, 60, 0.25f}>,
            // // 14
            automata::RSI_thresholds<14, automata::RSI_investment_strategy{30, 70, 0.5f}>,
            automata::RSI_thresholds<14, automata::RSI_investment_strategy{30, 70, 0.25f}>,
            automata::RSI_thresholds<14, automata::RSI_investment_strategy{40, 60, 0.5f}>,
            automata::RSI_thresholds<14, automata::RSI_investment_strategy{40, 60, 0.25f}>,
            automata::RSI_thresholds<14, automata::RSI_investment_strategy{45, 55, 0.5f}>,
            automata::RSI_thresholds<14, automata::RSI_investment_strategy{45, 55, 0.25f}>
        >(path_ETH_august_2021, 1000.f);
    }
    catch (const std::exception & error) {
        std::cerr << "exception : " << error.what() << '\n';
    }
    catch (...) {
        std::cerr << "unknown error\n";
    }

    std::cout << "done.\n";
}