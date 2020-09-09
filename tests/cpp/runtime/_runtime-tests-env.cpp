#include <array>
#include <gtest/gtest.h>

#include "runtime/interface.h"
#include "server/php-engine-vars.h"

// Используется в некоторых тестах, что бы обмануть clang и не дать ему выкинуть вызов std::malloc из кода
void *alloc_no_inline(int x) {
  return std::malloc(x);
}

class RuntimeTestsEnvironment final : public testing::Environment {
public:
  ~RuntimeTestsEnvironment() final {}

  static void reset_global_vars() {
    pid = 0;
    logname_id = 0;
    workers_n = 1;
  }

  void SetUp() final {
    testing::Environment::SetUp();

    reset_global_vars();

    global_init_runtime_libs();
    global_init_script_allocator();

    init_runtime_environment(nullptr, memory_.data(), memory_.size());

    php_disable_warnings = true;
    php_warning_level = 0;
  }

  void TearDown() final {
    reset_global_vars();

    free_runtime_environment();

    testing::Environment::TearDown();
  }

private:
  std::array<uint8_t, 16*1024*1024> memory_;
};

const testing::Environment* runtime_tests_env = testing::AddGlobalTestEnvironment(new RuntimeTestsEnvironment);
