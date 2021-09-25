#pragma once

#include <trading_bots/business/data_types.hpp>

#include <stack>
#include <optional>
#include <numeric>
#include <cmath>

namespace trading_bots::business::indices {

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
