#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/access_loggers/common/file_access_log_impl.h"
#include "source/extensions/access_loggers/file/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {
namespace {

class TestCustomCommandParser : public Formatter::CommandParser {
public:
  Formatter::FormatterProviderPtr parse(absl::string_view command, absl::string_view,
                                        absl::optional<size_t>) const override {
    if (command == "TEST_CUSTOM") {
      return std::make_unique<Formatter::PlainStringFormatter>("custom");
    }
    return nullptr;
  }
};

TEST(FileAccessLogNegativeTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  EXPECT_THROW(FileAccessLogFactory().createAccessLogInstance(
                   envoy::extensions::access_loggers::file::v3::FileAccessLog(), nullptr, context),
               ProtoValidationException);
}

TEST(FileAccessLogNegativeTest, InvalidNameFail) {
  envoy::config::accesslog::v3::AccessLog config;

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context), EnvoyException,
                            "Didn't find a registered implementation for '' with type URL: ''");

  config.set_name("INVALID");

  EXPECT_THROW_WITH_MESSAGE(
      AccessLog::AccessLogFactory::fromProto(config, context), EnvoyException,
      "Didn't find a registered implementation for 'INVALID' with type URL: ''");
}

class FileAccessLogTest : public testing::Test {
public:
  FileAccessLogTest() = default;

  void runTest(const std::string& yaml, absl::string_view expected, bool is_json,
               std::vector<Formatter::CommandParserPtr>&& command_parsers = {}) {
    envoy::extensions::access_loggers::file::v3::FileAccessLog fal_config;
    TestUtility::loadFromYaml(yaml, fal_config);

    envoy::config::accesslog::v3::AccessLog config;
    config.mutable_typed_config()->PackFrom(fal_config);

    auto file = std::make_shared<AccessLog::MockAccessLogFile>();
    Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, fal_config.path()};
    EXPECT_CALL(context_.server_factory_context_.access_log_manager_, createAccessLog(file_info))
        .WillOnce(Return(file));

    AccessLog::InstanceSharedPtr logger =
        AccessLog::AccessLogFactory::fromProto(config, context_, std::move(command_parsers));

    absl::Time abslStartTime =
        TestUtility::parseTime("Dec 18 01:50:34 2018 GMT", "%b %e %H:%M:%S %Y GMT");
    stream_info_.start_time_ = absl::ToChronoTime(abslStartTime);
    stream_info_.upstreamInfo()->setUpstreamHost(nullptr);
    stream_info_.setResponseCode(200);

    EXPECT_CALL(*file, write(_)).WillOnce(Invoke([expected, is_json](absl::string_view got) {
      if (is_json) {
        EXPECT_TRUE(TestUtility::jsonStringEqual(std::string(got), std::string(expected)));
      } else {
        EXPECT_EQ(got, expected);
      }
    }));
    logger->log({&request_headers_, &response_headers_, &response_trailers_}, stream_info_);
  }

  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/bar/foo"}};
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;

  NiceMock<Server::Configuration::MockFactoryContext> context_;
};

TEST_F(FileAccessLogTest, DEPRECATED_FEATURE_TEST(LegacyFormatEmpty)) {
  runTest(
      R"(
  path: "/foo"
  format: ""
)",
      "[2018-12-18T01:50:34.000Z] \"GET /bar/foo -\" 200 - 0 0 - - \"-\" \"-\" \"-\" \"-\" \"-\"\n",
      false);
}

TEST_F(FileAccessLogTest, DEPRECATED_FEATURE_TEST(LegacyFormatPlainText)) {
  runTest(
      R"(
  path: "/foo"
  format: "plain_text"
)",
      "plain_text", false);
}

TEST_F(FileAccessLogTest, DEPRECATED_FEATURE_TEST(LegacyJsonFormat)) {
  runTest(
      R"(
  path: "/foo"
  json_format:
    text: "plain text"
    path: "%REQ(:path)%"
    code: "%RESPONSE_CODE%"
)",
      R"({
    "text": "plain text",
    "path": "/bar/foo",
    "code": 200
})",
      true);
}

TEST_F(FileAccessLogTest, DEPRECATED_FEATURE_TEST(LegacyTypedJsonFormat)) {
  runTest(
      R"(
  path: "/foo"
  typed_json_format:
    text: "plain text"
    path: "%REQ(:path)%"
    code: "%RESPONSE_CODE%"
)",
      R"({
    "text": "plain text",
    "path": "/bar/foo",
    "code": 200
})",
      true);
}

TEST_F(FileAccessLogTest, EmptyFormat) {
  runTest(
      R"(
  path: "/foo"
)",
      "[2018-12-18T01:50:34.000Z] \"GET /bar/foo -\" 200 - 0 0 - - \"-\" \"-\" \"-\" \"-\" \"-\"\n",
      false);
}

TEST_F(FileAccessLogTest, LogFormatText) {
  runTest(
      R"(
  path: "/foo"
  log_format:
    text_format_source:
      inline_string: "plain_text - %REQ(:path)% - %RESPONSE_CODE%"
)",
      "plain_text - /bar/foo - 200", false);
}

TEST_F(FileAccessLogTest, LogFormatJson) {
  runTest(
      R"(
  path: "/foo"
  log_format:
    json_format:
      text: "plain text"
      path: "%REQ(:path)%"
      code: "%RESPONSE_CODE%"
)",
      R"({
    "text": "plain text",
    "path": "/bar/foo",
    "code": 200
})",
      true);
}

TEST_F(FileAccessLogTest, LogFormatJsonWithCustomCommands) {
  const std::string format_yaml = R"(
    path: "/foo"
    log_format:
      json_format:
        custom: "%TEST_CUSTOM%"
        text: "plain text"
        path: "%REQ(:path)%"
  )";

  envoy::extensions::access_loggers::file::v3::FileAccessLog fal_config;
  TestUtility::loadFromYaml(format_yaml, fal_config);

  {
    // No custom command parsers and the formatter should fail.
    envoy::config::accesslog::v3::AccessLog config;
    config.mutable_typed_config()->PackFrom(fal_config);
    config.set_name("file");

    EXPECT_THROW_WITH_MESSAGE(AccessLog::AccessLogFactory::fromProto(config, context_),
                              EnvoyException, "Not supported field in StreamInfo: TEST_CUSTOM");
  }

  {
    std::vector<Formatter::CommandParserPtr> command_parsers;
    command_parsers.push_back(std::make_unique<TestCustomCommandParser>());

    runTest(format_yaml, R"({
      "custom": "custom",
      "text": "plain text",
      "path": "/bar/foo",
  })",
            true, std::move(command_parsers));
  }
}

} // namespace
} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
