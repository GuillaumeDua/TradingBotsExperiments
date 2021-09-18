#pragma once

#include <coroutine>
#include <memory>
#include <utility>

#ifndef fwd
# define fwd(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#endif

namespace trading_bots::details::coro {

    template<typename T>
    struct generator {
    
        struct promise_type;
        using handle_type = std::coroutine_handle<promise_type>;
        
        generator(handle_type h) : coro(h) {}
        handle_type coro;
        std::shared_ptr<T> value;
        
        ~generator() {
            if (coro) coro.destroy();
        }
        generator(const generator&) = delete;
        generator& operator = (const generator&) = delete;
        generator(generator&& oth): coro(oth.coro) {
            oth.coro = nullptr;
        }
        generator& operator = (generator&& oth) {
            coro = oth.coro;
            oth.coro = nullptr;
            return *this;
        }
        T && getValue() {
            return std::move(coro.promise().current_value);
        }
        bool next() {
            coro.resume();
            return not coro.done();
        }
        struct promise_type {
            promise_type()  = default;
            ~promise_type() = default;
            
            auto initial_suspend() {
                return std::suspend_always{};
            }
            auto final_suspend() noexcept {
                return std::suspend_always{};
            }
            auto get_return_object() {
                return generator{handle_type::from_promise(*this)};
            }
            auto return_void() {
                return std::suspend_never{};
            }

            auto yield_value(T && value) {
                current_value = fwd(value);
                return std::suspend_always{};
            }
            void unhandled_exception() {
                throw std::runtime_error{"generator<T> : unhandled exception"};
            }
            T current_value;
        };
    };
}

#undef fwd