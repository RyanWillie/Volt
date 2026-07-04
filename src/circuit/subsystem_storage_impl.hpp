#pragma once

#include <volt/circuit/detail/subsystem_storage.hpp>

#include <memory>
#include <utility>

namespace volt::detail {

template <typename Facade, typename State>
SubsystemStorage<Facade, State>::SubsystemStorage() : SubsystemStorage{std::make_shared<State>()} {}

template <typename Facade, typename State>
SubsystemStorage<Facade, State>::SubsystemStorage(std::shared_ptr<State> state)
    : Facade{state}, state_{std::move(state)} {}

template <typename Facade, typename State>
SubsystemStorage<Facade, State>::SubsystemStorage(const SubsystemStorage &other)
    : SubsystemStorage{std::make_shared<State>(other.state())} {}

template <typename Facade, typename State>
SubsystemStorage<Facade, State>::SubsystemStorage(SubsystemStorage &&other) noexcept
    : SubsystemStorage{} {
    swap_with(other);
}

template <typename Facade, typename State>
SubsystemStorage<Facade, State> &
SubsystemStorage<Facade, State>::operator=(const SubsystemStorage &other) {
    if (this != &other) {
        auto replacement = SubsystemStorage{std::make_shared<State>(other.state())};
        swap_with(replacement);
    }
    return *this;
}

template <typename Facade, typename State>
SubsystemStorage<Facade, State> &
SubsystemStorage<Facade, State>::operator=(SubsystemStorage &&other) noexcept {
    if (this != &other) {
        swap_with(other);
        auto fresh = SubsystemStorage{};
        other.swap_with(fresh);
    }
    return *this;
}

template <typename Facade, typename State>
SubsystemStorage<Facade, State>::~SubsystemStorage() = default;

template <typename Facade, typename State>
State &SubsystemStorage<Facade, State>::mutable_state() noexcept {
    return *state_;
}

template <typename Facade, typename State>
const State &SubsystemStorage<Facade, State>::state() const noexcept {
    return *state_;
}

template <typename Facade, typename State>
void SubsystemStorage<Facade, State>::swap_with(SubsystemStorage &other) noexcept {
    std::swap(static_cast<Facade &>(*this), static_cast<Facade &>(other));
    std::swap(state_, other.state_);
}

} // namespace volt::detail
