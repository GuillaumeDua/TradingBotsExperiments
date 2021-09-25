#include <trading_bots/business/automatas.hpp>
#include <trading_bots/business/indices.hpp>

#include <trading_bots/details/io.hpp>
#include <trading_bots/details/tuple_view.hpp>

#include <gcl/cx/type_name.hpp>

#include <memory>
#include <iostream>
#include <iomanip>
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

    using record_type = business::data_types::record;
    auto records = details::io::csv::file<record_type>{ path }.extract_datas();
    auto features = std::tuple {
        business::indices::last_record{},
        business::indices::rsi{},
        business::indices::trend{},
        business::indices::roc{}
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