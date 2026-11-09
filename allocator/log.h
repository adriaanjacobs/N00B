/**
 * @file log.h
 *
 * Logging functionality used in the N00B allocator.
 */

#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <thread>
#include <type_traits>

#include "NOOB/config.h"

/**
 * An incrementing counter for each thread so identical invocations for a thread can be
 * distinguished easily.
 */
thread_local extern int64_t traced_invocation;

namespace nooballoc::log {
extern bool logging_enabled;

template <typename Formattable>
void log(Formattable&& message) {
    if (logging_enabled) {
        std::cerr << std::format("[thread: {}] {}\n",
            std::hash<std::thread::id> {}(std::this_thread::get_id()), message);
    }
}
}

/**
 * Trace the invocation of the given function by printing information to stderr.
 *
 * @param function_name The name of the function.
 * @param function The function to invoke.
 * @param arguments The arguments of the function.
 * @return The return value of the function;
 */
template <typename Func, typename... Arg>
auto traced_call(const char* function_name, Func&& function,
    Arg&&... arguments) -> std::invoke_result_t<Func, Arg...> {
    // Write to string first and write the entire string at once to stderr to ensure no interleaved
    // output.
    std::string test = std::format("[thread: {}, invocation, {}]  {}(",
        std::hash<std::thread::id> {}(std::this_thread::get_id()), traced_invocation,
        function_name);
    ((test += std::format("{}, ", std::forward<Arg>(arguments))), ...);
    if (sizeof...(Arg) != 0) {
        test.pop_back();
        test.pop_back();
    }
    test += ")\n";
    std::cerr << test;

    if constexpr (std::is_same_v<std::invoke_result_t<Func, Arg...>, void>) {
        // Special case for void-returning functions as the result type would be void&&, which isn't
        // possible.
        std::invoke(std::forward<Func>(function), std::forward<Arg>(arguments)...);

        std::string test = std::format("[thread: {}, invocation, {}] ~{}(",
            std::hash<std::thread::id> {}(std::this_thread::get_id()), traced_invocation++,
            function_name);
        ((test += std::format("{}, ", std::forward<Arg>(arguments))), ...);
        if (sizeof...(Arg) != 0) {
            test.pop_back();
            test.pop_back();
        }
        test += ")\n";
        std::cerr << test;

        return;
    } else {
        // Non-void-returning functions.
        std::invoke_result_t<Func, Arg...>&& result =
            std::invoke(std::forward<Func>(function), std::forward<Arg>(arguments)...);

        std::string test = std::format("[thread: {}, invocation, {}] ~{}(",
            std::hash<std::thread::id> {}(std::this_thread::get_id()), traced_invocation++,
            function_name);
        ((test += std::format("{}, ", std::forward<Arg>(arguments))), ...);
        if (sizeof...(Arg) != 0) {
            test.pop_back();
            test.pop_back();
        }
        test += std::format(") -> {}\n", result);
        std::cerr << test;

        return result;
    }
}

/**
 * Invoke `function` with the given arguments, but print the function name and arguments before the
 * invocation and print the function name and return value after the invocation. The print output
 * also includes the thread id.
 */
#if DO_TRACING
#define TRACED_CALL(function, ...) traced_call(#function, function __VA_OPT__(, ) __VA_ARGS__)
#else
#define TRACED_CALL(function, ...) function(__VA_ARGS__)
#endif

/**
 * Trace the function this is placed in.
 */
#if DO_TRACING
#include <format>
#define TRACE_FUNCTION()                                                                           \
    do {                                                                                           \
        std::cerr << std::format("{}\n", __PRETTY_FUNCTION__);                                     \
    } while (false)
#else
#define TRACE_FUNCTION()
#endif

/**
 * Log the given `message` to stderr.
 *
 * @param message The message to log.
 */
#if DO_LOGGING
#include <format>
#define LOG(formatter, ...) nooballoc::log::log(std::format(formatter __VA_OPT__(, ) __VA_ARGS__))
#else
#define LOG(formatter, ...)
#endif

/**
 * Print all the N00B-related info about `ptr` in a human-friendly manner.
 *
 * @param prefix A prefix used when printing the pointer.
 * @param ptr Any pointer to print N00B-related info for.
 */
void noob_print_ptr(const char* prefix, const void* ptr);
