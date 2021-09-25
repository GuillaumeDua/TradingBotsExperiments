#include <trading_bots/business/data_types.hpp>
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
//  stats : how many buy/sell orders per automata ?
//
//  auto-update amounts using market datas (stocks * value) instead of dispatching updates
//
//  datas : better datas (smaller timegaps) : 1d => 4h, 1h, 15m, 1m

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

    using record_type = trading_bots::business::data_types::record;
    using amount_type = double;

    template <typename T>
    concept input_type = requires (T& value, const record_type & arg) {
        value.update(arg);
    };

    enum trend_value_type {
        up, stable, down
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

        void update(const record_type & input) {
            cache.push_front(input);
            if (cache.size() > max_duration)
                cache.pop_back();
            assert(cache.size() <= max_duration);
        }
        
        using value_type = trading_bots::business::data_types::rate;
        std::optional<value_type> value_for_duration(std::size_t duration) const {

            if (duration <= 1)
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

    template <std::size_t max_duration = 14>
    requires (max_duration > 1)
    struct trend { 
        using value_type = trend_value_type;  
        
        std::optional<value_type> value_for_duration(std::size_t duration, float fluctuation_threshold) const {

            if (duration <= 1)
                throw std::invalid_argument{"trend::value_for_duration : duration <= 1"};

            if (std::size(cache) < duration)
                return std::nullopt;

            const auto latest_input = std::begin(cache);
            const auto past_input = std::next(latest_input, duration);

            const auto fluctuation_rate = (latest_input->CloseLast / past_input->CloseLast) - 1;
            if (fluctuation_rate < fluctuation_threshold and fluctuation_rate > -fluctuation_threshold)
                return value_type::stable;
            else
                return fluctuation_rate < .0 ? value_type::down : value_type::up;
        }

        void update(const record_type & input) {

            cache.push_front(input);
            if (cache.size() > max_duration)
                cache.pop_back();
            assert(cache.size() <= max_duration);
        }

    private:
        std::deque<record_type> cache;
    };
    
    // todo : MA / EMA / BOLL

    // ROC => Rate of Change
    //  Positive values => buying  pressure/upward-momentum
    //  Negative values => selling pressure/downward-momentum
    template <std::size_t max_duration = 14>
    requires (max_duration > 1)
    struct roc { 
        using value_type = float;
        std::optional<value_type> value_for_duration(std::size_t duration) const {

            if (duration <= 1)
                throw std::invalid_argument{"roc::value_for_duration : duration <= 1"};

            if (std::size(cache) < duration)
                return std::nullopt;

            const auto latest_input = std::begin(cache);
            const auto past_input = std::next(latest_input, duration);

            return (
                (
                    (latest_input->CloseLast - past_input->CloseLast)
                    / past_input->CloseLast
                ) * 100
            );

            // wip
            // https://www.investopedia.com/terms/p/pricerateofchange.asp
        }

        void update(const record_type & input) {

            cache.push_front(input);
            if (cache.size() > max_duration)
                cache.pop_back();
            assert(cache.size() <= max_duration);
        }

    private:
        std::deque<record_type> cache;
    };
}
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
                trading_bots::input::rsi<>
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
                trading_bots::input::rsi<>,
                trading_bots::input::trend<>
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
                    *(trend_value) == input::trend_value_type::stable // no observal trend
                ) return;

                std::cout
                    << gcl::cx::type_name_v<std::remove_cvref_t<decltype(*this)>> << '\n'
                    << "\trsi = " << *rsi_value << '\n'
                ;

                if (*trend_value == input::trend_value_type::up and
                    *rsi_value < 50)
                    buy_up_to(current_amount_USD * (1 - (*rsi_value / 50)));
                if (*trend_value == input::trend_value_type::down and
                    *rsi_value > 50)
                    sell_up_to(investement.to_USDT() * ((*rsi_value / 50) - 1));
            }
        };

        using proportional = proportional_with_trends<0.f>;

        template <investment_strategy strategy>
        struct thresholds : public base {

            using components_type = std::tuple<
                trading_bots::input::rsi<>
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
                trading_bots::input::rsi<>,
                trading_bots::input::trend<>
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
                    *(trend_value) == input::trend_value_type::stable // no observal trend
                ) return;
                
                std::cout
                    << gcl::cx::type_name_v<std::remove_cvref_t<decltype(*this)>> << '\n'
                    << "\trsi = " << *rsi_value << '\n'
                ;
                if (trend_value == input::trend_value_type::up and
                    *rsi_value < strategy.thresholds.buy)
                    buy_up_to(current_amount_USD * strategy.investment);
                if (trend_value == input::trend_value_type::down and
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
            // todo : better errors when some features are missing (avoid error bloat in std::tuple impl details)
            return details::tuple_view::make_tuple_view<Ts...>(features_container);
        }(typename std::remove_cvref_t<decltype(value)>::components_type{});
        value.process(std::move(features_requested));
    };

    using record_type = trading_bots::business::data_types::record;
    auto records = details::io::csv::file<record_type>{ path }.extract_datas();
    auto features = std::tuple {
        input::last_record{},
        input::rsi{},
        input::trend{},
        input::roc{}
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
                    << '\t' << std::setw(130) << std::left
                    << gcl::cx::type_name_v<std::remove_cvref_t<decltype(value)>>
                    << " : " << std::setw(10) << value.total_capital()
                    << " = " << std::setw(10) << (value.total_capital() - initial_amount)
                    << " | " << (win_or_loss_rate < 0 ? '-' : '+') << ' ' << std::setw(8) << ((value.total_capital() / initial_amount) * 100) - 100 << " %"
                    << '\n'
                ;
            }, automata_value
        );
}

// --- tests

namespace test {
    template <std::size_t rsi_value>
    using RSI_types = trading_bots::details::mp::pack_type<
        typename trading_bots::automata::RSI_of<rsi_value>::proportional,
        // thresholds
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds<trading_bots::investment_strategy{ .thresholds = { .buy = 30, .sell = 70 }, .investment = 0.5f  }>,
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds<trading_bots::investment_strategy{ .thresholds = { .buy = 30, .sell = 70 }, .investment = 0.25f }>,
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds<trading_bots::investment_strategy{ .thresholds = { .buy = 40, .sell = 60 }, .investment = 0.5f  }>,
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds<trading_bots::investment_strategy{ .thresholds = { .buy = 40, .sell = 60 }, .investment = 0.25f }>,
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds<trading_bots::investment_strategy{ .thresholds = { .buy = 45, .sell = 55 }, .investment = 0.5f  }>,
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds<trading_bots::investment_strategy{ .thresholds = { .buy = 45, .sell = 55 }, .investment = 0.25f }>,
        // thresholds_and_trends
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds_and_trends<trading_bots::investment_strategy{ .thresholds = { .buy = 30, .sell = 70 }, .investment = 0.5f  }, 2.f>,
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds_and_trends<trading_bots::investment_strategy{ .thresholds = { .buy = 30, .sell = 70 }, .investment = 0.25f }, 2.f>,
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds_and_trends<trading_bots::investment_strategy{ .thresholds = { .buy = 40, .sell = 60 }, .investment = 0.5f  }, 2.f>,
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds_and_trends<trading_bots::investment_strategy{ .thresholds = { .buy = 40, .sell = 60 }, .investment = 0.25f }, 2.f>,
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds_and_trends<trading_bots::investment_strategy{ .thresholds = { .buy = 45, .sell = 55 }, .investment = 0.5f  }, 2.f>,
        typename trading_bots::automata::RSI_of<rsi_value>::thresholds_and_trends<trading_bots::investment_strategy{ .thresholds = { .buy = 45, .sell = 55 }, .investment = 0.25f }, 2.f>
    >;
}

auto main() -> int {
    
    const std::string path_ETH_august_2021 = "./datas/ETH/HistoricalData_1631403583208.csv";
    const std::string path_ETH_2021 = "./datas/ETH/HistoricalData_1631403618344.csv";
    const std::string path_ETH_all = "./datas/ETH/HistoricalData_1631482231024.csv";

    using namespace trading_bots;

    using RSI_to_test_t = std::integer_sequence<std::size_t, 1, 4, 6, 7, 10, 14>;

    try {
        run_for_datas<
            // long-term (do nothing but wait)
            automata::long_term,
            // RSI_proportional
            automata::RSI_of<4>::proportional,
            automata::RSI_of<6>::proportional,
            automata::RSI_of<7>::proportional,
            automata::RSI_of<14>::proportional,
            // RSI_thresholds
            // 14
            automata::RSI_of<14>::thresholds<investment_strategy{ .thresholds = { .buy = 30, .sell = 70 }, .investment = 0.5f  }>,
            automata::RSI_of<14>::thresholds<investment_strategy{ .thresholds = { .buy = 30, .sell = 70 }, .investment = 0.25f }>,
            automata::RSI_of<14>::thresholds<investment_strategy{ .thresholds = { .buy = 40, .sell = 60 }, .investment = 0.5f  }>,
            automata::RSI_of<14>::thresholds<investment_strategy{ .thresholds = { .buy = 40, .sell = 60 }, .investment = 0.25f }>,
            automata::RSI_of<14>::thresholds<investment_strategy{ .thresholds = { .buy = 45, .sell = 55 }, .investment = 0.5f  }>,
            automata::RSI_of<14>::thresholds<investment_strategy{ .thresholds = { .buy = 45, .sell = 55 }, .investment = 0.25f }>
            // RSI
            
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