// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/guid.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/views/payments/payment_request_browsertest_base.h"
#include "chrome/browser/ui/views/payments/payment_request_dialog_view_ids.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "ui/views/controls/label.h"

namespace payments {

autofill::AutofillProfile CreateProfileWithPartialAddress() {
  autofill::AutofillProfile profile = autofill::test::GetFullProfile2();
  profile.SetRawInfo(autofill::ADDRESS_HOME_LINE1, base::ASCIIToUTF16(""));
  profile.SetRawInfo(autofill::ADDRESS_HOME_LINE2, base::ASCIIToUTF16(""));
  profile.SetRawInfo(autofill::ADDRESS_HOME_CITY, base::ASCIIToUTF16(""));
  profile.SetRawInfo(autofill::ADDRESS_HOME_STATE, base::ASCIIToUTF16(""));
  return profile;
}

class PaymentRequestProfileListTest : public PaymentRequestBrowserTestBase {
 protected:
  PaymentRequestProfileListTest() {}
};

IN_PROC_BROWSER_TEST_F(PaymentRequestProfileListTest, PrioritizeCompleteness) {
  NavigateTo("/payment_request_free_shipping_test.html");
  autofill::AutofillProfile complete = autofill::test::GetFullProfile();
  autofill::AutofillProfile partial = CreateProfileWithPartialAddress();
  partial.set_use_count(1000);

  AddAutofillProfile(complete);
  AddAutofillProfile(partial);

  // In the Personal Data Manager, the partial address is more frecent.
  autofill::PersonalDataManager* personal_data_manager = GetDataManager();
  std::vector<autofill::AutofillProfile*> profiles =
      personal_data_manager->GetProfilesToSuggest();
  ASSERT_EQ(2UL, profiles.size());
  EXPECT_EQ(partial, *profiles[0]);
  EXPECT_EQ(complete, *profiles[1]);

  InvokePaymentRequestUI();

  PaymentRequest* request = GetPaymentRequests(GetActiveWebContents()).front();

  // The complete profile should be selected.
  ASSERT_TRUE(request->state()->selected_shipping_profile());
  EXPECT_EQ(complete, *request->state()->selected_shipping_profile());

  // It should appear first in the shipping profiles.
  ASSERT_EQ(2UL, request->state()->shipping_profiles().size());
  EXPECT_EQ(complete, *request->state()->shipping_profiles()[0]);
  EXPECT_EQ(partial, *request->state()->shipping_profiles()[1]);

  // And both should appear in the UI.
  OpenShippingAddressSectionScreen();
  views::View* sheet = dialog_view()->GetViewByID(
      static_cast<int>(DialogViewID::SHIPPING_ADDRESS_SHEET_LIST_VIEW));
  ASSERT_EQ(2u, sheet->children().size());
  views::View* first_label = sheet->child_at(0)->GetViewByID(
      static_cast<int>(DialogViewID::PROFILE_LABEL_LINE_1));
  views::View* second_label = sheet->child_at(1)->GetViewByID(
      static_cast<int>(DialogViewID::PROFILE_LABEL_LINE_1));

  EXPECT_EQ(base::ASCIIToUTF16("John H. Doe"),
            static_cast<views::Label*>(first_label)->text());
  EXPECT_EQ(base::ASCIIToUTF16("Jane A. Smith"),
            static_cast<views::Label*>(second_label)->text());
}

}  // namespace payments
