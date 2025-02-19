// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/device_sync/proto/cryptauth_v2_test_util.h"

#include "base/no_destructor.h"
#include "chromeos/services/device_sync/public/cpp/gcm_constants.h"

namespace cryptauthv2 {

const char kTestDeviceSyncGroupName[] = "device_sync_group_name";
const char kTestGcmRegistrationId[] = "gcm_registraion_id";
const char kTestInstanceId[] = "instance_id";
const char kTestInstanceIdToken[] = "instance_id_token";
const char kTestLongDeviceId[] = "long_device_id";

// Attributes of test ClientDirective.
const int32_t kTestClientDirectiveRetryAttempts = 3;
const int64_t kTestClientDirectiveCheckinDelayMillis = 2592000000;  // 30 days
const int64_t kTestClientDirectivePolicyReferenceVersion = 2;
const int64_t kTestClientDirectiveRetryPeriodMillis = 43200000;  // 12 hours
const int64_t kTestClientDirectiveCreateTimeMillis = 1566073800000;
const char kTestClientDirectiveInvokeNextKeyName[] =
    "client_directive_invoke_next_key_name";
const char kTestClientDirectivePolicyReferenceName[] =
    "client_directive_policy_reference_name";
const TargetService kTestClientDirectiveInvokeNextService =
    TargetService::DEVICE_SYNC;

ClientMetadata BuildClientMetadata(
    int32_t retry_count,
    const ClientMetadata::InvocationReason& invocation_reason) {
  ClientMetadata client_metadata;
  client_metadata.set_retry_count(retry_count);
  client_metadata.set_invocation_reason(invocation_reason);

  return client_metadata;
}

PolicyReference BuildPolicyReference(const std::string& name, int64_t version) {
  PolicyReference policy_reference;
  policy_reference.set_name(name);
  policy_reference.set_version(version);

  return policy_reference;
}

KeyDirective BuildKeyDirective(const PolicyReference& policy_reference,
                               int64_t enroll_time_millis) {
  KeyDirective key_directive;
  key_directive.mutable_policy_reference()->CopyFrom(policy_reference);
  key_directive.set_enroll_time_millis(enroll_time_millis);

  return key_directive;
}

RequestContext BuildRequestContext(const std::string& group,
                                   const ClientMetadata& client_metadata,
                                   const std::string& device_id,
                                   const std::string& device_id_token) {
  RequestContext request_context;
  request_context.set_group(group);
  request_context.mutable_client_metadata()->CopyFrom(client_metadata);
  request_context.set_device_id(device_id);
  request_context.set_device_id_token(device_id_token);

  return request_context;
}

DeviceFeatureStatus BuildDeviceFeatureStatus(
    const std::string& device_id,
    const std::vector<std::pair<std::string /* feature_type */,
                                bool /* enabled */>>& feature_statuses) {
  DeviceFeatureStatus device_feature_status;
  device_feature_status.set_device_id(device_id);

  for (const auto& feature_status : feature_statuses) {
    DeviceFeatureStatus::FeatureStatus* fs =
        device_feature_status.add_feature_statuses();
    fs->set_feature_type(feature_status.first);
    fs->set_enabled(feature_status.second);
  }

  return device_feature_status;
}

const ClientAppMetadata& GetClientAppMetadataForTest() {
  static const base::NoDestructor<ClientAppMetadata> metadata([] {
    ApplicationSpecificMetadata app_specific_metadata;
    app_specific_metadata.set_gcm_registration_id(kTestGcmRegistrationId);
    app_specific_metadata.set_device_software_package(
        chromeos::device_sync::kCryptAuthGcmAppId);

    BetterTogetherFeatureMetadata beto_metadata;
    beto_metadata.add_supported_features(
        BetterTogetherFeatureMetadata::BETTER_TOGETHER_CLIENT);
    beto_metadata.add_supported_features(
        BetterTogetherFeatureMetadata::SMS_CONNECT_CLIENT);

    FeatureMetadata feature_metadata;
    feature_metadata.set_feature_type(FeatureMetadata::BETTER_TOGETHER);
    feature_metadata.set_metadata(beto_metadata.SerializeAsString());

    ClientAppMetadata metadata;
    metadata.add_application_specific_metadata()->CopyFrom(
        app_specific_metadata);
    metadata.set_instance_id(kTestInstanceId);
    metadata.set_instance_id_token(kTestInstanceIdToken);
    metadata.set_long_device_id(kTestLongDeviceId);
    metadata.add_feature_metadata()->CopyFrom(feature_metadata);

    return metadata;
  }());

  return *metadata;
}

const ClientDirective& GetClientDirectiveForTest() {
  static const base::NoDestructor<ClientDirective> client_directive([] {
    InvokeNext invoke_next;
    invoke_next.set_service(kTestClientDirectiveInvokeNextService);
    invoke_next.set_key_name(kTestClientDirectiveInvokeNextKeyName);

    ClientDirective client_directive;
    client_directive.mutable_policy_reference()->CopyFrom(
        BuildPolicyReference(kTestClientDirectivePolicyReferenceName,
                             kTestClientDirectivePolicyReferenceVersion));
    client_directive.set_checkin_delay_millis(
        kTestClientDirectiveCheckinDelayMillis);
    client_directive.set_retry_attempts(kTestClientDirectiveRetryAttempts);
    client_directive.set_retry_period_millis(
        kTestClientDirectiveRetryPeriodMillis);
    client_directive.set_create_time_millis(
        kTestClientDirectiveCreateTimeMillis);
    client_directive.add_invoke_next()->CopyFrom(invoke_next);

    return client_directive;
  }());
  return *client_directive;
}

const RequestContext& GetRequestContextForTest() {
  static const base::NoDestructor<RequestContext> request_context([] {
    return BuildRequestContext(
        kTestDeviceSyncGroupName,
        BuildClientMetadata(0 /* retry_count */, ClientMetadata::MANUAL),
        kTestInstanceId, kTestInstanceIdToken);
  }());
  return *request_context;
}

}  // namespace cryptauthv2
