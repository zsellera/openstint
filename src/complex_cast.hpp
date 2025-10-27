#pragma once

#include <complex>

namespace {
    template <typename To, typename From>
    constexpr std::complex<To> complex_cast(const std::complex<From>& c) noexcept {
        return {static_cast<To>(c.real()), static_cast<To>(c.imag())};
    }
}