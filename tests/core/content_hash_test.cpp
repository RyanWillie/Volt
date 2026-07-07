#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

#include <volt/core/content_hash.hpp>
#include <volt/core/errors.hpp>

namespace {

void check_invalid_content_hash(std::string_view value, std::string_view message) {
    try {
        (void)volt::ContentHash{std::string{value}};
        FAIL("ContentHash accepted a non-canonical label");
    } catch (const volt::KernelError &error) {
        CHECK(error.code() == volt::ErrorCode::InvalidArgument);
        CHECK(std::string{error.what()} == std::string{message});
    }
}

} // namespace

TEST_CASE("Content hash computes canonical SHA-256 labels") {
    CHECK(volt::sha256_content_hash("abc") ==
          volt::ContentHash{
              "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"});
}

TEST_CASE("ContentHash rejects non-canonical SHA-256 labels") {
    CHECK_THROWS_AS(volt::ContentHash{""}, std::invalid_argument);
    CHECK_THROWS_AS(
        volt::ContentHash{"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        std::invalid_argument);
    CHECK_THROWS_AS(
        volt::ContentHash{
            "sha256:BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"},
        std::invalid_argument);
}

TEST_CASE("ContentHash reports typed kernel errors for non-canonical labels") {
    check_invalid_content_hash("", "Content hash must use sha256:<64 lowercase hex digits>");
    check_invalid_content_hash(
        "sha256:BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
        "Content hash must use lowercase hexadecimal digits");
}
