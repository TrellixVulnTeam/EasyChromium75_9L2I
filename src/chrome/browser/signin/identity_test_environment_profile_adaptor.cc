// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/signin/identity_test_environment_profile_adaptor.h"

#include "base/bind.h"
#include "chrome/browser/signin/account_tracker_service_factory.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "components/signin/core/browser/fake_profile_oauth2_token_service.h"
#include "components/signin/core/browser/identity_manager_wrapper.h"

// static
std::unique_ptr<TestingProfile> IdentityTestEnvironmentProfileAdaptor::
    CreateProfileForIdentityTestEnvironment() {
  return CreateProfileForIdentityTestEnvironment(
      TestingProfile::TestingFactories());
}

// static
std::unique_ptr<TestingProfile>
IdentityTestEnvironmentProfileAdaptor::CreateProfileForIdentityTestEnvironment(
    const TestingProfile::TestingFactories& input_factories) {
  TestingProfile::Builder builder;

  for (auto& input_factory : input_factories) {
    builder.AddTestingFactory(input_factory.first, input_factory.second);
  }

  return CreateProfileForIdentityTestEnvironment(builder);
}

// static
std::unique_ptr<TestingProfile>
IdentityTestEnvironmentProfileAdaptor::CreateProfileForIdentityTestEnvironment(
    TestingProfile::Builder& builder) {
  for (auto& identity_factory : GetIdentityTestEnvironmentFactories()) {
    builder.AddTestingFactory(identity_factory.first, identity_factory.second);
  }

  return builder.Build();
}

// static
void IdentityTestEnvironmentProfileAdaptor::
    SetIdentityTestEnvironmentFactoriesOnBrowserContext(
        content::BrowserContext* context) {
  for (const auto& factory_pair : GetIdentityTestEnvironmentFactories()) {
    factory_pair.first->SetTestingFactory(context, factory_pair.second);
  }
}

// static
void IdentityTestEnvironmentProfileAdaptor::
    AppendIdentityTestEnvironmentFactories(
        TestingProfile::TestingFactories* factories_to_append_to) {
  TestingProfile::TestingFactories identity_factories =
      GetIdentityTestEnvironmentFactories();
  factories_to_append_to->insert(factories_to_append_to->end(),
                                 identity_factories.begin(),
                                 identity_factories.end());
}

// static
TestingProfile::TestingFactories
IdentityTestEnvironmentProfileAdaptor::GetIdentityTestEnvironmentFactories() {
  return {{IdentityManagerFactory::GetInstance(),
           base::BindRepeating(&BuildIdentityManagerForTests)}};
}

// static
std::unique_ptr<KeyedService>
IdentityTestEnvironmentProfileAdaptor::BuildIdentityManagerForTests(
    content::BrowserContext* context) {
  Profile* profile = Profile::FromBrowserContext(context);
  auto fake_token_service =
      std::make_unique<FakeProfileOAuth2TokenService>(profile->GetPrefs());

  return identity::IdentityTestEnvironment::BuildIdentityManagerForTests(
      ChromeSigninClientFactory::GetForProfile(profile), profile->GetPrefs(),
      std::move(fake_token_service),
      AccountTrackerServiceFactory::GetForProfile(profile));
}

IdentityTestEnvironmentProfileAdaptor::IdentityTestEnvironmentProfileAdaptor(
    Profile* profile)
    : identity_test_env_(IdentityManagerFactory::GetForProfile(profile)) {}
