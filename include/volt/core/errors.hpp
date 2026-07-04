#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <volt/core/diagnostics.hpp>

namespace volt {

/**
 * Stable machine-readable family for a structural kernel error.
 *
 * Codes classify why a mutation-boundary or lookup operation was rejected so callers can
 * branch on failure kind without parsing message strings. Codes are families, not
 * per-message identifiers; add a new code only when callers need to distinguish it.
 */
enum class ErrorCode {
    /** An entity ID or name does not identify an entity in the target model. */
    UnknownEntity,
    /** A name, reference designator, or other unique key already exists. */
    DuplicateName,
    /** An entity was used with a model, definition, or scope it does not belong to. */
    CrossReferenceViolation,
    /** A supplied value is malformed, such as an empty name or a non-finite coordinate. */
    InvalidArgument,
    /** The operation is structurally legal but not in the model's current state. */
    InvalidState,
};

/** Return the stable identifier name for an error code, such as "DuplicateName". */
[[nodiscard]] constexpr std::string_view error_code_name(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::UnknownEntity:
        return "UnknownEntity";
    case ErrorCode::DuplicateName:
        return "DuplicateName";
    case ErrorCode::CrossReferenceViolation:
        return "CrossReferenceViolation";
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::InvalidState:
        return "InvalidState";
    }
    return "UnknownErrorCode";
}

/**
 * Typed base for every structural error thrown by the kernel.
 *
 * Responsibility: carries the machine-readable ErrorCode and, when one is naturally at
 *   hand, an EntityRef identifying the entity the operation was rejected for.
 * Invariants: thrown objects always also derive from the std exception type historical
 *   callers expect (logic_error, invalid_argument, or out_of_range), so
 *   `catch (const volt::KernelError &)` and std-type catch sites both keep working.
 * Collaborators: thrown via KernelLogicError, KernelArgumentError, and KernelRangeError;
 *   consumed by tests, callers, and the Python exception translator.
 */
class KernelError {
  public:
    /** Return the machine-readable failure family. */
    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

    /** Return the entity the operation was rejected for, when one was recorded. */
    [[nodiscard]] const std::optional<EntityRef> &entity() const noexcept { return entity_; }

    /** Return the human-readable failure message. */
    [[nodiscard]] virtual const char *what() const noexcept = 0;

    /** Destroy the error. */
    virtual ~KernelError() = default;

  protected:
    /** Record the failure family and optional entity for a concrete typed error. */
    KernelError(ErrorCode code, std::optional<EntityRef> entity) noexcept
        : code_{code}, entity_{entity} {}

    /** Copy typed error metadata. */
    KernelError(const KernelError &other) = default;
    /** Copy typed error metadata. */
    KernelError &operator=(const KernelError &other) = default;

  private:
    ErrorCode code_;
    std::optional<EntityRef> entity_;
};

namespace detail {

/**
 * Concrete kernel error deriving from both KernelError and one std exception type.
 *
 * The std base preserves the exception type each throw site historically used, so
 * existing catch sites and test assertions keep passing while new callers branch on
 * ErrorCode through the KernelError base.
 */
template <typename StdError> class TypedKernelError final : public StdError, public KernelError {
  public:
    /** Construct a typed kernel error with a code, message, and optional entity. */
    TypedKernelError(ErrorCode code, const std::string &message,
                     std::optional<EntityRef> entity = std::nullopt)
        : StdError{message}, KernelError{code, entity} {}

    /** Return the human-readable failure message. */
    [[nodiscard]] const char *what() const noexcept override { return StdError::what(); }
};

} // namespace detail

/** Structural kernel error historically thrown as std::logic_error. */
using KernelLogicError = detail::TypedKernelError<std::logic_error>;

/** Structural kernel error historically thrown as std::invalid_argument. */
using KernelArgumentError = detail::TypedKernelError<std::invalid_argument>;

/** Structural kernel error historically thrown as std::out_of_range. */
using KernelRangeError = detail::TypedKernelError<std::out_of_range>;

} // namespace volt
