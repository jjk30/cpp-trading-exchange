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
cc_library(
    name = "order",
    hdrs = ["order.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "order_test",
    srcs = ["order_test.cpp"],
    deps = [
        ":order",
        "@googletest//:gtest_main",
    ],
)
cc_library(
    name = "order_book",
    hdrs = ["order_book.h"],
    deps = [
        ":mem_pool",
        ":order",
    ],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "order_book_test",
    srcs = ["order_book_test.cpp"],
    deps = [
        ":order_book",
        "@googletest//:gtest_main",
    ],
)