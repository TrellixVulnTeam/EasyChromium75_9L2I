// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_SIGNALING_FTL_SIGNAL_STRATEGY_H_
#define REMOTING_SIGNALING_FTL_SIGNAL_STRATEGY_H_

#include <memory>

#include "base/macros.h"
#include "remoting/signaling/signal_strategy.h"

namespace remoting {

class FtlDeviceIdProvider;
class OAuthTokenGetter;

// FtlSignalStrategy implements SignalStrategy using the FTL messaging service.
// This class can be created on a different sequence from the one it is used
// (when Connect() is called).
class FtlSignalStrategy : public SignalStrategy {
 public:
  // |oauth_token_getter| must outlive |core_|. Ideally it should be a
  // singleton.
  // TODO(yuweih): Consider taking weak pointer to OAuthTokenGetter or wrapping
  // it with OAuthTokenGetterProxy.
  FtlSignalStrategy(OAuthTokenGetter* oauth_token_getter,
                    std::unique_ptr<FtlDeviceIdProvider> device_id_provider);
  ~FtlSignalStrategy() override;

  // SignalStrategy interface.
  void Connect() override;
  void Disconnect() override;
  State GetState() const override;
  Error GetError() const override;
  const SignalingAddress& GetLocalAddress() const override;
  void AddListener(Listener* listener) override;
  void RemoveListener(Listener* listener) override;
  bool SendStanza(std::unique_ptr<jingle_xmpp::XmlElement> stanza) override;
  std::string GetNextId() override;

  // Returns true if the signal strategy gets into an error state when it tries
  // to sign in. You can get back the actual error by calling GetError().
  bool IsSignInError() const;

 private:
  // This ensures that even if a Listener deletes the current instance during
  // OnSignalStrategyIncomingStanza(), we can delete |core_| asynchronously.
  class Core;

  std::unique_ptr<Core> core_;

  DISALLOW_COPY_AND_ASSIGN(FtlSignalStrategy);
};

}  // namespace remoting

#endif  // REMOTING_SIGNALING_FTL_SIGNAL_STRATEGY_H_
