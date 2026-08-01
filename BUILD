load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "mem_pool",
    hdrs = ["mem_pool.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "mem_pool_test",
    srcs = ["mem_pool_test.cpp"],
    deps = [
        ":mem_pool",
        "@googletest//:gtest_main",
    ],
)