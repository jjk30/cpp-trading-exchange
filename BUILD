load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

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
    name = "trade",
    hdrs = ["trade.h"],
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
cc_library(
    name = "lf_queue",
    hdrs = ["lf_queue.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "lf_queue_test",
    srcs = ["lf_queue_test.cpp"],
    deps = [
        ":lf_queue",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "exchange_main",
    srcs = ["exchange_main.cpp"],
    deps = [
        ":lf_queue",
        ":order",
        ":order_book",
    ],
)
cc_library(
    name = "md_message",
    hdrs = ["md_message.h"],
    deps = [":order"],
    visibility = ["//visibility:public"],
)
cc_library(
    name = "order_book",
    srcs = ["order_book.cpp"],
    hdrs = ["order_book.h"],
    deps = [
        ":md_message",
        ":mem_pool",
        ":order",
        ":trade",
    ],
    visibility = ["//visibility:public"],
)
cc_library(
    name = "md_book",
    hdrs = ["md_book.h"],
    deps = [":md_message"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "md_book_test",
    srcs = ["md_book_test.cpp"],
    deps = [
        ":md_book",
        ":order_book",
        "@googletest//:gtest_main",
    ],
)
