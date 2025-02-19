// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "api/public/presentation/presentation_connection.h"

#include <memory>

#include "api/impl/quic/testing/fake_quic_connection.h"
#include "api/impl/quic/testing/fake_quic_connection_factory.h"
#include "api/impl/quic/testing/quic_test_support.h"
#include "api/public/network_service_manager.h"
#include "api/public/presentation/presentation_controller.h"
#include "third_party/abseil/src/absl/strings/string_view.h"
#include "third_party/googletest/src/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace openscreen {
namespace presentation {

using ::testing::_;
using ::testing::Invoke;

class MockConnectionDelegate final : public Connection::Delegate {
 public:
  MockConnectionDelegate() = default;
  ~MockConnectionDelegate() override = default;

  MOCK_METHOD0(OnConnected, void());
  MOCK_METHOD0(OnClosedByRemote, void());
  MOCK_METHOD0(OnDiscarded, void());
  MOCK_METHOD1(OnError, void(const absl::string_view message));
  MOCK_METHOD0(OnTerminatedByRemote, void());
  MOCK_METHOD1(OnStringMessage, void(const absl::string_view message));
  MOCK_METHOD1(OnBinaryMessage, void(const std::vector<uint8_t>& data));
};

class MockConnectRequest final
    : public ProtocolConnectionClient::ConnectionRequestCallback {
 public:
  ~MockConnectRequest() override = default;

  void OnConnectionOpened(
      uint64_t request_id,
      std::unique_ptr<ProtocolConnection> connection) override {
    OnConnectionOpenedMock(request_id, connection.release());
  }
  MOCK_METHOD2(OnConnectionOpenedMock,
               void(uint64_t request_id, ProtocolConnection* connection));
  MOCK_METHOD1(OnConnectionFailed, void(uint64_t request_id));
};

class ConnectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    NetworkServiceManager::Create(nullptr, nullptr,
                                  std::move(quic_bridge_.quic_client),
                                  std::move(quic_bridge_.quic_server));
  }

  void TearDown() override { NetworkServiceManager::Dispose(); }

  std::string MakeEchoResponse(const std::string& message) {
    return std::string("echo: ") + message;
  }

  std::vector<uint8_t> MakeEchoResponse(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> response{13, 14, 15};
    response.insert(response.end(), data.begin(), data.end());
    return response;
  }

  FakeQuicBridge quic_bridge_;
  ConnectionManager controller_connection_manager_{
      quic_bridge_.controller_demuxer.get()};
  ConnectionManager receiver_connection_manager_{
      quic_bridge_.receiver_demuxer.get()};
};

TEST_F(ConnectionTest, ConnectAndSend) {
  const std::string id{"deadbeef01234"};
  const std::string url{"https://example.com/receiver.html"};
  const uint64_t connection_id = 13;
  MockConnectionDelegate mock_controller_delegate;
  MockConnectionDelegate mock_receiver_delegate;
  Connection controller(Connection::PresentationInfo{id, url},
                        Connection::Role::kController,
                        &mock_controller_delegate);
  Connection receiver(Connection::PresentationInfo{id, url},
                      Connection::Role::kReceiver, &mock_receiver_delegate);

  EXPECT_EQ(id, controller.get_presentation_info().id);
  EXPECT_EQ(url, controller.get_presentation_info().url);
  EXPECT_EQ(id, receiver.get_presentation_info().id);
  EXPECT_EQ(url, receiver.get_presentation_info().url);

  EXPECT_EQ(Connection::State::kConnecting, controller.state());
  EXPECT_EQ(Connection::State::kConnecting, receiver.state());

  MockConnectRequest mock_connect_request;
  std::unique_ptr<ProtocolConnection> controller_stream;
  std::unique_ptr<ProtocolConnection> receiver_stream;
  NetworkServiceManager::Get()->GetProtocolConnectionClient()->Connect(
      quic_bridge_.kReceiverEndpoint, &mock_connect_request);
  EXPECT_CALL(mock_connect_request, OnConnectionOpenedMock(_, _))
      .WillOnce(Invoke([&controller_stream](uint64_t request_id,
                                            ProtocolConnection* stream) {
        controller_stream.reset(stream);
      }));

  EXPECT_CALL(quic_bridge_.mock_server_observer, OnIncomingConnectionMock(_))
      .WillOnce(testing::WithArgs<0>(testing::Invoke(
          [&receiver_stream](std::unique_ptr<ProtocolConnection>& connection) {
            receiver_stream = std::move(connection);
          })));

  quic_bridge_.RunTasksUntilIdle();
  ASSERT_TRUE(controller_stream);
  ASSERT_TRUE(receiver_stream);

  EXPECT_CALL(mock_controller_delegate, OnConnected());
  EXPECT_CALL(mock_receiver_delegate, OnConnected());
  uint64_t controller_endpoint_id = receiver_stream->endpoint_id();
  uint64_t receiver_endpoint_id = controller_stream->endpoint_id();
  controller.OnConnected(connection_id, receiver_endpoint_id,
                         std::move(controller_stream));
  receiver.OnConnected(connection_id, controller_endpoint_id,
                       std::move(receiver_stream));
  controller_connection_manager_.AddConnection(&controller);
  receiver_connection_manager_.AddConnection(&receiver);

  EXPECT_EQ(Connection::State::kConnected, controller.state());
  EXPECT_EQ(Connection::State::kConnected, receiver.state());

  std::string message = "some connection message";
  const std::string expected_message = message;
  const std::string expected_response = MakeEchoResponse(expected_message);

  controller.SendString(message);

  std::string received;
  EXPECT_CALL(mock_receiver_delegate,
              OnStringMessage(static_cast<absl::string_view>(expected_message)))
      .WillOnce(Invoke(
          [&received](absl::string_view s) { received = std::string(s); }));
  quic_bridge_.RunTasksUntilIdle();

  std::string string_response = MakeEchoResponse(received);
  receiver.SendString(string_response);

  EXPECT_CALL(
      mock_controller_delegate,
      OnStringMessage(static_cast<absl::string_view>(expected_response)));
  quic_bridge_.RunTasksUntilIdle();

  std::vector<uint8_t> data{0, 3, 2, 4, 4, 6, 1};
  const std::vector<uint8_t> expected_data = data;
  const std::vector<uint8_t> expected_response_data =
      MakeEchoResponse(expected_data);

  controller.SendBinary(std::move(data));

  std::vector<uint8_t> received_data;
  EXPECT_CALL(mock_receiver_delegate, OnBinaryMessage(expected_data))
      .WillOnce(Invoke([&received_data](std::vector<uint8_t> d) {
        received_data = std::move(d);
      }));
  quic_bridge_.RunTasksUntilIdle();

  receiver.SendBinary(MakeEchoResponse(received_data));
  EXPECT_CALL(mock_controller_delegate,
              OnBinaryMessage(expected_response_data));
  quic_bridge_.RunTasksUntilIdle();

  receiver.Close(Connection::CloseReason::kClosed);
  EXPECT_CALL(mock_controller_delegate, OnClosedByRemote());
  quic_bridge_.RunTasksUntilIdle();
  controller_connection_manager_.RemoveConnection(&controller);
  receiver_connection_manager_.RemoveConnection(&receiver);
}

}  // namespace presentation
}  // namespace openscreen
