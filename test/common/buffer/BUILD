load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
    ],
)

envoy_proto_library(
    name = "buffer_fuzz_proto",
    srcs = ["buffer_fuzz.proto"],
)

envoy_cc_test_library(
    name = "buffer_fuzz_lib",
    srcs = ["buffer_fuzz.cc"],
    hdrs = ["buffer_fuzz.h"],
    rbe_pool = "6gig",
    deps = [
        ":buffer_fuzz_proto_cc_proto",
        "//source/common/buffer:buffer_lib",
        "//source/common/memory:stats_lib",
        "//source/common/network:address_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "buffer_fuzz_test",
    srcs = ["buffer_fuzz_test.cc"],
    corpus = "buffer_corpus",
    rbe_pool = "6gig",
    deps = [":buffer_fuzz_lib"],
)

envoy_cc_test(
    name = "buffer_test",
    srcs = ["buffer_test.cc"],
    rbe_pool = "6gig",
    deps = [
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "owned_impl_test",
    srcs = ["owned_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:address_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "buffer_util_test",
    srcs = ["buffer_util_test.cc"],
    rbe_pool = "6gig",
    deps = [
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:buffer_util_lib",
    ],
)

envoy_cc_test(
    name = "watermark_buffer_test",
    srcs = ["watermark_buffer_test.cc"],
    rbe_pool = "6gig",
    deps = [
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/network:address_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "buffer_memory_account_test",
    srcs = ["buffer_memory_account_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//test/integration:tracked_watermark_buffer_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:stream_reset_handler_mock",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "zero_copy_input_stream_test",
    srcs = ["zero_copy_input_stream_test.cc"],
    rbe_pool = "6gig",
    deps = [
        ":utility_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "buffer_speed_test",
    srcs = ["buffer_speed_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "@com_github_google_benchmark//:benchmark",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "buffer_speed_test_benchmark_test",
    benchmark_binary = "buffer_speed_test",
)
