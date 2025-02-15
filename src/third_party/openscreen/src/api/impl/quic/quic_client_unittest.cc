// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "api/impl/quic/quic_client.h"

#include <memory>

#include "api/impl/quic/quic_service_common.h"
#include "api/impl/quic/testing/fake_quic_connection_factory.h"
#include "api/impl/quic/testing/quic_test_support.h"
#include "api/public/network_metrics.h"
#include "api/public/network_service_manager.h"
#include "base/error.h"
#include "platform/api/logging.h"
#include "third_party/googletest/src/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace openscreen {
namespace {

using ::testing::_;
using ::testing::Invoke;

class MockMessageCallback final : public MessageDemuxer::MessageCallback {
 public:
  ~MockMessageCallback() override = default;

  MOCK_METHOD6(OnStreamMessage,
               ErrorOr<size_t>(uint64_t endpoint_id,
                               uint64_t connection_id,
                               msgs::Type message_type,
                               const uint8_t* buffer,
                               size_t buffer_size,
                               platform::TimeDelta now));
};

class MockConnectionObserver final : public ProtocolConnection::Observer {
 public:
  ~MockConnectionObserver() override = default;

  MOCK_METHOD1(OnConnectionClosed, void(const ProtocolConnection& connection));
};

class ConnectionCallback final
    : public ProtocolConnectionClient::ConnectionRequestCallback {
 public:
  explicit ConnectionCallback(std::unique_ptr<ProtocolConnection>* connection)
      : connection_(connection) {}
  ~ConnectionCallback() override = default;

  void OnConnectionOpened(
      uint64_t request_id,
      std::unique_ptr<ProtocolConnection> connection) override {
    OSP_DCHECK(!failed_ && !*connection_);
    *connection_ = std::move(connection);
  }

  void OnConnectionFailed(uint64_t request_id) override {
    OSP_DCHECK(!failed_ && !*connection_);
    failed_ = true;
  }

 private:
  bool failed_ = false;
  std::unique_ptr<ProtocolConnection>* const connection_;
};

class QuicClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_ = quic_bridge_.quic_client.get();
    NetworkServiceManager::Create(nullptr, nullptr,
                                  std::move(quic_bridge_.quic_client),
                                  std::move(quic_bridge_.quic_server));
  }

  void TearDown() override { NetworkServiceManager::Dispose(); }

  void SendTestMessage(ProtocolConnection* connection) {
    MockMessageCallback mock_message_callback;
    MessageDemuxer::MessageWatch message_watch =
        quic_bridge_.receiver_demuxer->WatchMessageType(
            0, msgs::Type::kPresentationConnectionMessage,
            &mock_message_callback);

    msgs::CborEncodeBuffer buffer;
    msgs::PresentationConnectionMessage message;
    message.presentation_id = "KMvyNqTCvvSv7v5X";
    message.connection_id = 7;
    message.message.which = decltype(message.message.which)::kString;
    new (&message.message.str) std::string("message from client");
    ASSERT_TRUE(msgs::EncodePresentationConnectionMessage(message, &buffer));
    connection->Write(buffer.data(), buffer.size());
    connection->CloseWriteEnd();

    ssize_t decode_result = 0;
    msgs::PresentationConnectionMessage received_message;
    EXPECT_CALL(
        mock_message_callback,
        OnStreamMessage(0, connection->id(),
                        msgs::Type::kPresentationConnectionMessage, _, _, _))
        .WillOnce(Invoke([&decode_result, &received_message](
                             uint64_t endpoint_id, uint64_t connection_id,
                             msgs::Type message_type, const uint8_t* buffer,
                             size_t buffer_size, platform::TimeDelta now) {
          decode_result = msgs::DecodePresentationConnectionMessage(
              buffer, buffer_size, &received_message);
          if (decode_result < 0)
            return ErrorOr<size_t>(Error::Code::kCborParsing);
          return ErrorOr<size_t>(decode_result);
        }));
    quic_bridge_.RunTasksUntilIdle();

    ASSERT_GT(decode_result, 0);
    EXPECT_EQ(decode_result, static_cast<ssize_t>(buffer.size() - 1));
    EXPECT_EQ(received_message.presentation_id, message.presentation_id);
    EXPECT_EQ(received_message.connection_id, message.connection_id);
    ASSERT_EQ(received_message.message.which,
              decltype(received_message.message.which)::kString);
    EXPECT_EQ(received_message.message.str, message.message.str);
  }

  FakeQuicBridge quic_bridge_;
  QuicClient* client_;
};

}  // namespace

TEST_F(QuicClientTest, Connect) {
  client_->Start();

  std::unique_ptr<ProtocolConnection> connection;
  ConnectionCallback connection_callback(&connection);
  ProtocolConnectionClient::ConnectRequest request =
      client_->Connect(quic_bridge_.kReceiverEndpoint, &connection_callback);
  ASSERT_TRUE(request);

  quic_bridge_.RunTasksUntilIdle();
  ASSERT_TRUE(connection);

  SendTestMessage(connection.get());

  client_->Stop();
}

TEST_F(QuicClientTest, OpenImmediate) {
  client_->Start();

  std::unique_ptr<ProtocolConnection> connection1;
  std::unique_ptr<ProtocolConnection> connection2;

  connection2 = client_->CreateProtocolConnection(1);
  EXPECT_FALSE(connection2);

  ConnectionCallback connection_callback(&connection1);
  ProtocolConnectionClient::ConnectRequest request =
      client_->Connect(quic_bridge_.kReceiverEndpoint, &connection_callback);
  ASSERT_TRUE(request);

  connection2 = client_->CreateProtocolConnection(1);
  EXPECT_FALSE(connection2);

  quic_bridge_.RunTasksUntilIdle();
  ASSERT_TRUE(connection1);

  connection2 = client_->CreateProtocolConnection(connection1->endpoint_id());
  ASSERT_TRUE(connection2);

  SendTestMessage(connection2.get());

  client_->Stop();
}

TEST_F(QuicClientTest, States) {
  client_->Stop();
  std::unique_ptr<ProtocolConnection> connection1;
  ConnectionCallback connection_callback(&connection1);
  ProtocolConnectionClient::ConnectRequest request =
      client_->Connect(quic_bridge_.kReceiverEndpoint, &connection_callback);
  EXPECT_FALSE(request);
  std::unique_ptr<ProtocolConnection> connection2 =
      client_->CreateProtocolConnection(1);
  EXPECT_FALSE(connection2);

  EXPECT_CALL(quic_bridge_.mock_client_observer, OnRunning());
  EXPECT_TRUE(client_->Start());
  EXPECT_FALSE(client_->Start());

  request =
      client_->Connect(quic_bridge_.kReceiverEndpoint, &connection_callback);
  ASSERT_TRUE(request);
  quic_bridge_.RunTasksUntilIdle();
  ASSERT_TRUE(connection1);
  MockConnectionObserver mock_connection_observer1;
  connection1->SetObserver(&mock_connection_observer1);

  connection2 = client_->CreateProtocolConnection(connection1->endpoint_id());
  ASSERT_TRUE(connection2);
  MockConnectionObserver mock_connection_observer2;
  connection2->SetObserver(&mock_connection_observer2);

  EXPECT_CALL(mock_connection_observer1, OnConnectionClosed(_));
  EXPECT_CALL(mock_connection_observer2, OnConnectionClosed(_));
  EXPECT_CALL(quic_bridge_.mock_client_observer, OnStopped());
  EXPECT_TRUE(client_->Stop());
  EXPECT_FALSE(client_->Stop());

  request =
      client_->Connect(quic_bridge_.kReceiverEndpoint, &connection_callback);
  EXPECT_FALSE(request);
  connection2 = client_->CreateProtocolConnection(1);
  EXPECT_FALSE(connection2);
}

}  // namespace openscreen
