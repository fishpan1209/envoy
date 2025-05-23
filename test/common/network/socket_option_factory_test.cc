#include "envoy/config/core/v3/base.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/socket_option_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Network {
namespace {

class SocketOptionFactoryTest : public testing::Test {
public:
  SocketOptionFactoryTest() = default;

  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_{[this]() {
    // Before injecting OsSysCallsImpl, make sure validateIpv{4,6}Supported is called so the static
    // bool is initialized without requiring to mock ::socket and ::close. :( :(
    (void)std::make_unique<Address::Ipv4Instance>("1.2.3.4", 5678);
    (void)std::make_unique<Address::Ipv6Instance>("::1:2:3:4", 5678);
    return &os_sys_calls_mock_;
  }()};

protected:
  testing::NiceMock<MockListenSocket> socket_mock_;
  Api::MockOsSysCalls os_sys_calls_mock_;

  void SetUp() override { socket_mock_.connection_info_provider_->setLocalAddress(nullptr); }
  void makeSocketV4() {
    socket_mock_.connection_info_provider_->setLocalAddress(
        std::make_unique<Address::Ipv4Instance>("1.2.3.4", 5678));
  }
  void makeSocketV6() {
    socket_mock_.connection_info_provider_->setLocalAddress(
        std::make_unique<Address::Ipv6Instance>("::1:2:3:4", 5678));
  }
};

#define CHECK_OPTION_SUPPORTED(option)                                                             \
  if (!option.hasValue()) {                                                                        \
    return;                                                                                        \
  }

// TODO(klarose): Simplify these tests once https://github.com/envoyproxy/envoy/pull/5351 is merged.

TEST_F(SocketOptionFactoryTest, TestBuildSocketMarkOptions) {

  // use a shared_ptr due to applyOptions requiring one
  std::shared_ptr<Socket::Options> options = SocketOptionFactory::buildSocketMarkOptions(100);

  const auto expected_option = ENVOY_SOCKET_SO_MARK;
  CHECK_OPTION_SUPPORTED(expected_option);

  const int type = expected_option.level();
  const int option = expected_option.option();
  EXPECT_CALL(socket_mock_, setSocketOption(_, _, _, sizeof(int)))
      .WillOnce(Invoke([type, option](int input_type, int input_option, const void* optval,
                                      socklen_t) -> Api::SysCallIntResult {
        EXPECT_EQ(100, *static_cast<const int*>(optval));
        EXPECT_EQ(type, input_type);
        EXPECT_EQ(option, input_option);
        return {0, 0};
      }));

  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::config::core::v3::SocketOption::STATE_PREBIND));
}

TEST_F(SocketOptionFactoryTest, TestBuildIpv4TransparentOptions) {
  makeSocketV4();

  // use a shared_ptr due to applyOptions requiring one
  std::shared_ptr<Socket::Options> options = SocketOptionFactory::buildIpTransparentOptions();

  const auto expected_option = ENVOY_SOCKET_IP_TRANSPARENT;
  CHECK_OPTION_SUPPORTED(expected_option);

  const int type = expected_option.level();
  const int option = expected_option.option();
  EXPECT_CALL(socket_mock_, setSocketOption(_, _, _, sizeof(int)))
      .Times(2)
      .WillRepeatedly(Invoke([type, option](int input_type, int input_option, const void* optval,
                                            socklen_t) -> Api::SysCallIntResult {
        EXPECT_EQ(type, input_type);
        EXPECT_EQ(option, input_option);
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return {0, 0};
      }));
  EXPECT_CALL(socket_mock_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v4));
  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::config::core::v3::SocketOption::STATE_PREBIND));
  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::config::core::v3::SocketOption::STATE_BOUND));
}

TEST_F(SocketOptionFactoryTest, TestBuildIpv6TransparentOptions) {
  makeSocketV6();

  // use a shared_ptr due to applyOptions requiring one
  std::shared_ptr<Socket::Options> options = SocketOptionFactory::buildIpTransparentOptions();

  const auto expected_option = ENVOY_SOCKET_IPV6_TRANSPARENT;
  CHECK_OPTION_SUPPORTED(expected_option);

  const int type = expected_option.level();
  const int option = expected_option.option();
  EXPECT_CALL(socket_mock_, setSocketOption(_, _, _, sizeof(int)))
      .Times(2)
      .WillRepeatedly(Invoke([type, option](int input_type, int input_option, const void* optval,
                                            socklen_t) -> Api::SysCallIntResult {
        EXPECT_EQ(type, input_type);
        EXPECT_EQ(option, input_option);
        EXPECT_EQ(1, *static_cast<const int*>(optval));
        return {0, 0};
      }));

  EXPECT_CALL(socket_mock_, ipVersion()).WillRepeatedly(testing::Return(Address::IpVersion::v6));
  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::config::core::v3::SocketOption::STATE_PREBIND));
  EXPECT_TRUE(Network::Socket::applyOptions(options, socket_mock_,
                                            envoy::config::core::v3::SocketOption::STATE_BOUND));
}

TEST_F(SocketOptionFactoryTest, TestBuildLiteralOptions) {
  Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketOption> socket_options_proto;
  Envoy::Protobuf::TextFormat::Parser parser;
  envoy::config::core::v3::SocketOption socket_option_proto;
  struct linger expected_linger;
  expected_linger.l_onoff = 1;
  expected_linger.l_linger = 3456;
  absl::string_view linger_bstr{reinterpret_cast<const char*>(&expected_linger),
                                sizeof(struct linger)};
  std::string linger_bstr_formatted = testing::PrintToString(linger_bstr);
  static const char linger_option_format[] = R"proto(
    state: STATE_PREBIND
    level: %d
    name: %d
    buf_value: %s
  )proto";
  auto linger_option =
      absl::StrFormat(linger_option_format, SOL_SOCKET, SO_LINGER, linger_bstr_formatted);
  ASSERT_TRUE(parser.ParseFromString(linger_option, &socket_option_proto));

  *socket_options_proto.Add() = socket_option_proto;
  static const char keepalive_option_format[] = R"proto(
    state: STATE_PREBIND
    level: %d
    name: %d
    int_value: 1
  )proto";
  auto keepalive_option = absl::StrFormat(keepalive_option_format, SOL_SOCKET, SO_KEEPALIVE);
  ASSERT_TRUE(parser.ParseFromString(keepalive_option, &socket_option_proto));
  *socket_options_proto.Add() = socket_option_proto;

  static constexpr char socket_type_not_set_option_format[] = R"proto(
    state: STATE_PREBIND
    level: %d
    name: %d
    int_value: 1
  )proto";
  auto socket_type_not_set_textproto =
      absl::StrFormat(socket_type_not_set_option_format, SOL_SOCKET, SO_KEEPALIVE);
  ASSERT_TRUE(parser.ParseFromString(socket_type_not_set_textproto, &socket_option_proto));
  *socket_options_proto.Add() = socket_option_proto;

  static constexpr char empty_socket_type_option_format[] = R"proto(
    state: STATE_PREBIND
    level: %d
    name: %d
    int_value: 1
    type: {}
  )proto";
  auto empty_socket_type_textproto =
      absl::StrFormat(empty_socket_type_option_format, SOL_SOCKET, SO_KEEPALIVE);
  ASSERT_TRUE(parser.ParseFromString(empty_socket_type_textproto, &socket_option_proto));
  *socket_options_proto.Add() = socket_option_proto;

  static constexpr char all_socket_types_set_option_format[] = R"proto(
    state: STATE_PREBIND
    level: %d
    name: %d
    int_value: 1
    type: {
      stream: {}
      datagram: {}
    }
  )proto";
  auto all_socket_types_set_textproto =
      absl::StrFormat(all_socket_types_set_option_format, SOL_SOCKET, SO_KEEPALIVE);
  ASSERT_TRUE(parser.ParseFromString(all_socket_types_set_textproto, &socket_option_proto));
  *socket_options_proto.Add() = socket_option_proto;

  static constexpr char stream_socket_type_option_format[] = R"proto(
    state: STATE_PREBIND
    level: %d
    name: %d
    int_value: 1
    type: {
      stream: {}
    }
  )proto";
  auto stream_socket_type_textproto =
      absl::StrFormat(stream_socket_type_option_format, SOL_SOCKET, SO_KEEPALIVE);
  ASSERT_TRUE(parser.ParseFromString(stream_socket_type_textproto, &socket_option_proto));
  *socket_options_proto.Add() = socket_option_proto;

  static constexpr char datagram_socket_type_option_format[] = R"proto(
    state: STATE_PREBIND
    level: %d
    name: %d
    int_value: 1
    type: {
      datagram: {}
    }
  )proto";
  auto datagram_socket_type_textproto =
      absl::StrFormat(datagram_socket_type_option_format, SOL_SOCKET, SO_KEEPALIVE);
  ASSERT_TRUE(parser.ParseFromString(datagram_socket_type_textproto, &socket_option_proto));
  *socket_options_proto.Add() = socket_option_proto;

  auto socket_options = SocketOptionFactory::buildLiteralOptions(socket_options_proto);
  EXPECT_EQ(7, socket_options->size());
  auto option_details = socket_options->at(0)->getOptionDetails(
      socket_mock_, envoy::config::core::v3::SocketOption::STATE_PREBIND);
  EXPECT_TRUE(option_details.has_value());
  EXPECT_EQ(SOL_SOCKET, option_details->name_.level());
  EXPECT_EQ(SO_LINGER, option_details->name_.option());
  EXPECT_EQ(linger_bstr, option_details->value_);

  option_details = socket_options->at(1)->getOptionDetails(
      socket_mock_, envoy::config::core::v3::SocketOption::STATE_PREBIND);
  EXPECT_TRUE(option_details.has_value());
  EXPECT_EQ(SOL_SOCKET, option_details->name_.level());
  EXPECT_EQ(SO_KEEPALIVE, option_details->name_.option());
  int value = 1;
  absl::string_view value_bstr{reinterpret_cast<const char*>(&value), sizeof(int)};
  EXPECT_EQ(value_bstr, option_details->value_);

  auto socket_type_not_set_option =
      dynamic_pointer_cast<const SocketOptionImpl>(socket_options->at(2));
  EXPECT_FALSE(socket_type_not_set_option->socketType().has_value());

  auto empty_socket_type_option =
      dynamic_pointer_cast<const SocketOptionImpl>(socket_options->at(3));
  EXPECT_FALSE(empty_socket_type_option->socketType().has_value());

  auto all_socket_types_option =
      dynamic_pointer_cast<const SocketOptionImpl>(socket_options->at(4));
  EXPECT_TRUE(all_socket_types_option->socketType().has_value());
  EXPECT_EQ(Socket::Type::Stream, *all_socket_types_option->socketType());

  auto stream_socket_type_option =
      dynamic_pointer_cast<const SocketOptionImpl>(socket_options->at(5));
  EXPECT_TRUE(stream_socket_type_option->socketType().has_value());
  EXPECT_EQ(Socket::Type::Stream, *stream_socket_type_option->socketType());

  auto datagram_socket_type_option =
      dynamic_pointer_cast<const SocketOptionImpl>(socket_options->at(6));
  EXPECT_TRUE(datagram_socket_type_option->socketType().has_value());
  EXPECT_EQ(Socket::Type::Datagram, *datagram_socket_type_option->socketType());
}

TEST_F(SocketOptionFactoryTest, TestBuildZeroSoLingerOptions) {
  struct linger expected_linger;
  expected_linger.l_onoff = 1;
  expected_linger.l_linger = 0;
  absl::string_view linger_bstr{reinterpret_cast<const char*>(&expected_linger),
                                sizeof(struct linger)};
  auto socket_options = SocketOptionFactory::buildZeroSoLingerOptions();
  auto option_details = socket_options->at(0)->getOptionDetails(
      socket_mock_, envoy::config::core::v3::SocketOption::STATE_LISTENING);
  EXPECT_TRUE(option_details.has_value());
  EXPECT_EQ(SOL_SOCKET, option_details->name_.level());
  EXPECT_EQ(SO_LINGER, option_details->name_.option());
  EXPECT_EQ(linger_bstr, option_details->value_);
}

TEST_F(SocketOptionFactoryTest, TestBuildBindAddressNoPortOptions) {
  const auto expected_option = ENVOY_SOCKET_IP_BIND_ADDRESS_NO_PORT;
  CHECK_OPTION_SUPPORTED(expected_option);

  int value = 1;
  absl::string_view expected_value{reinterpret_cast<char*>(&value), sizeof(value)};
  auto socket_options = SocketOptionFactory::buildBindAddressNoPort();
  auto option_details = socket_options->at(0)->getOptionDetails(
      socket_mock_, envoy::config::core::v3::SocketOption::STATE_PREBIND);
  EXPECT_TRUE(option_details.has_value());
  EXPECT_EQ(expected_option.level(), option_details->name_.level());
  EXPECT_EQ(expected_option.option(), option_details->name_.option());
  EXPECT_EQ(expected_value, option_details->value_);
}

} // namespace
} // namespace Network
} // namespace Envoy
