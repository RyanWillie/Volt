#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include <volt/core/errors.hpp>
#include <volt/core/ids.hpp>

TEST_CASE("Typed kernel errors carry a code, message, and optional entity") {
    const auto error = volt::KernelLogicError{volt::ErrorCode::DuplicateName, "Name already exists",
                                              volt::EntityRef::net(volt::NetId{3})};

    CHECK(error.code() == volt::ErrorCode::DuplicateName);
    CHECK(std::string{error.what()} == "Name already exists");
    REQUIRE(error.entity().has_value());
    CHECK(error.entity()->kind() == volt::EntityKind::Net);
    CHECK(error.entity()->index() == 3);

    const auto without_entity =
        volt::KernelArgumentError{volt::ErrorCode::InvalidArgument, "Value must not be empty"};
    CHECK_FALSE(without_entity.entity().has_value());
}

TEST_CASE("Typed kernel errors preserve their historical std exception types") {
    CHECK_THROWS_AS(throw volt::KernelLogicError(volt::ErrorCode::InvalidState, "state"),
                    std::logic_error);
    CHECK_THROWS_AS(throw volt::KernelArgumentError(volt::ErrorCode::InvalidArgument, "argument"),
                    std::invalid_argument);
    CHECK_THROWS_AS(throw volt::KernelRangeError(volt::ErrorCode::UnknownEntity, "range"),
                    std::out_of_range);

    CHECK_THROWS_AS(throw volt::KernelLogicError(volt::ErrorCode::InvalidState, "state"),
                    volt::KernelError);
    CHECK_THROWS_AS(throw volt::KernelArgumentError(volt::ErrorCode::InvalidArgument, "argument"),
                    volt::KernelError);
    CHECK_THROWS_AS(throw volt::KernelRangeError(volt::ErrorCode::UnknownEntity, "range"),
                    volt::KernelError);
}

TEST_CASE("Kernel errors are reachable from std exception catch sites") {
    try {
        throw volt::KernelRangeError{volt::ErrorCode::UnknownEntity, "Missing entity",
                                     volt::EntityRef::component(volt::ComponentId{7})};
    } catch (const std::exception &error) {
        const auto *kernel_error = dynamic_cast<const volt::KernelError *>(&error);
        REQUIRE(kernel_error != nullptr);
        CHECK(kernel_error->code() == volt::ErrorCode::UnknownEntity);
        CHECK(std::string{kernel_error->what()} == "Missing entity");
        REQUIRE(kernel_error->entity().has_value());
        CHECK(kernel_error->entity()->kind() == volt::EntityKind::Component);
    }
}

TEST_CASE("Error code names are stable identifiers") {
    CHECK(volt::error_code_name(volt::ErrorCode::UnknownEntity) == "UnknownEntity");
    CHECK(volt::error_code_name(volt::ErrorCode::DuplicateName) == "DuplicateName");
    CHECK(volt::error_code_name(volt::ErrorCode::CrossReferenceViolation) ==
          "CrossReferenceViolation");
    CHECK(volt::error_code_name(volt::ErrorCode::InvalidArgument) == "InvalidArgument");
    CHECK(volt::error_code_name(volt::ErrorCode::InvalidState) == "InvalidState");
}
