// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/quartc/test/quartc_peer.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"

namespace quic {
namespace test {

QuartcPeer::QuartcPeer(const QuicClock* clock,
                       QuicAlarmFactory* alarm_factory,
                       QuicRandom* random,
                       const std::vector<QuartcDataSource::Config>& configs)
    : clock_(clock),
      alarm_factory_(alarm_factory),
      random_(random),
      enabled_(false),
      session_(nullptr),
      configs_(configs) {}

QuartcPeer::~QuartcPeer() {
  session_->CloseConnection("~QuartcPeer()");
}

void QuartcPeer::SetEnabled(bool value) {
  enabled_ = value;
  for (auto& source : data_sources_) {
    source->SetEnabled(enabled_);
  }
}

IdToSequenceNumberMap QuartcPeer::GetLastSequenceNumbers() const {
  DCHECK_GE(configs_.size(), data_sources_.size());
  IdToSequenceNumberMap out;
  for (int i = 0; i < data_sources_.size(); ++i) {
    out[configs_[i].id] = data_sources_[i]->sequence_number();
  }
  return out;
}

void QuartcPeer::OnSessionCreated(QuartcSession* session) {
  session_ = session;

  session_->SetDelegate(this);
  session_->StartCryptoHandshake();

  QuicByteCount largest_message_payload =
      session_->GetGuaranteedLargestMessagePayload();
  for (auto& config : configs_) {
    // Clamp maximum frame sizes to the largest supported by the session before
    // creating data sources.
    config.max_frame_size =
        config.max_frame_size > 0
            ? std::min(config.max_frame_size, largest_message_payload)
            : largest_message_payload;
    QUIC_LOG(INFO) << "Set max frame size for source " << config.id << " to "
                   << config.max_frame_size;
    data_sources_.push_back(QuicMakeUnique<QuartcDataSource>(
        clock_, alarm_factory_, random_, config, this));
  }
}

void QuartcPeer::OnConnectError(QuicErrorCode error,
                                const std::string& error_details) {
  QUIC_LOG(WARNING) << "Connect failed, error=" << error
                    << ", details=" << error_details;
  SetEnabled(false);
}

void QuartcPeer::OnCryptoHandshakeComplete() {
  SetEnabled(true);
}

void QuartcPeer::OnConnectionWritable() {
  SetEnabled(true);
}

void QuartcPeer::OnIncomingStream(QuartcStream* stream) {
  QUIC_LOG(DFATAL) << "Unexpected incoming stream, id=" << stream->id();
}

void QuartcPeer::OnCongestionControlChange(QuicBandwidth bandwidth_estimate,
                                           QuicBandwidth pacing_rate,
                                           QuicTime::Delta latest_rtt) {
  // Note: this is fairly crude rate adaptation and makes no effort to account
  // for overhead.  The congestion controller is assumed to account for this.
  // It may do so by detecting overuse and pushing back on its bandwidth
  // estimate, or it may explicitly subtract overhead before surfacing its
  // estimate.
  QuicBandwidth available = std::min(bandwidth_estimate, pacing_rate);
  for (auto& source : data_sources_) {
    available = source->AllocateBandwidth(available);
  }
}

void QuartcPeer::OnConnectionClosed(QuicErrorCode error_code,
                                    const std::string& error_details,
                                    ConnectionCloseSource source) {
  QUIC_LOG(INFO) << "Connection closed, error=" << error_code
                 << ", details=" << error_details;
  SetEnabled(false);
}

void QuartcPeer::OnMessageReceived(QuicStringPiece message) {
  ReceivedMessage received;
  received.receive_time = clock_->Now();

  if (!ParsedQuartcDataFrame::Parse(message, &received.frame)) {
    QUIC_LOG(DFATAL) << "Failed to parse incoming message as test data frame: ["
                     << message << "]";
  }
  received_messages_.push_back(received);
}

void QuartcPeer::OnDataProduced(const char* data, size_t length) {
  // Further packetization is not required, as sources are configured to produce
  // frames that fit within message payloads.
  DCHECK_LE(length, session_->GetCurrentLargestMessagePayload());
  session_->SendOrQueueMessage(std::string(data, length));
}

}  // namespace test
}  // namespace quic
