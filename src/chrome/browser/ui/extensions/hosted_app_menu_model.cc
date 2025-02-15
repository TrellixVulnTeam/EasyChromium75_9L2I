// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/extensions/hosted_app_menu_model.h"

#include "base/metrics/histogram_macros.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/web_app_browser_controller.h"
#include "chrome/grit/chromium_strings.h"
#include "chrome/grit/generated_resources.h"
#include "components/strings/grit/components_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"

constexpr int HostedAppMenuModel::kUninstallAppCommandId;

HostedAppMenuModel::HostedAppMenuModel(ui::AcceleratorProvider* provider,
                                       Browser* browser)
    : AppMenuModel(provider, browser) {}

HostedAppMenuModel::~HostedAppMenuModel() {}

void HostedAppMenuModel::Build() {
  if (CreateActionToolbarOverflowMenu())
    AddSeparator(ui::UPPER_SEPARATOR);
  AddItemWithStringId(IDC_HOSTED_APP_MENU_APP_INFO,
                      IDS_APP_CONTEXT_MENU_SHOW_INFO);
  int app_info_index = GetItemCount() - 1;
  SetMinorText(app_info_index, WebAppBrowserController::FormatUrlOrigin(
                                   browser()
                                       ->tab_strip_model()
                                       ->GetActiveWebContents()
                                       ->GetVisibleURL()));
  SetMinorIcon(app_info_index,
               browser()->location_bar_model()->GetVectorIcon());

  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItemWithStringId(IDC_COPY_URL, IDS_COPY_URL);
  AddItemWithStringId(IDC_OPEN_IN_CHROME, IDS_OPEN_IN_CHROME);

// Chrome OS's app list is prominent enough to not need a separate uninstall
// option in the app menu.
#if !defined(OS_CHROMEOS)
  DCHECK(browser()->web_app_controller());
  if (browser()->web_app_controller()->IsInstalled()) {
    AddSeparator(ui::NORMAL_SEPARATOR);
    AddItem(kUninstallAppCommandId,
            l10n_util::GetStringFUTF16(
                IDS_UNINSTALL_FROM_OS_LAUNCH_SURFACE,
                base::UTF8ToUTF16(
                    browser()->web_app_controller()->GetAppShortName())));
  }
#endif  // !defined(OS_CHROMEOS)
  AddSeparator(ui::LOWER_SEPARATOR);

  CreateZoomMenu();
  AddSeparator(ui::UPPER_SEPARATOR);
  AddItemWithStringId(IDC_PRINT, IDS_PRINT);
  AddItemWithStringId(IDC_FIND, IDS_FIND);
  if (media_router::MediaRouterEnabled(browser()->profile()))
    AddItemWithStringId(IDC_ROUTE_MEDIA, IDS_MEDIA_ROUTER_MENU_ITEM_TITLE);
  AddSeparator(ui::LOWER_SEPARATOR);
  CreateCutCopyPasteMenu();
}

bool HostedAppMenuModel::IsCommandIdEnabled(int command_id) const {
  return command_id == kUninstallAppCommandId
             ? browser()->web_app_controller()->CanUninstall()
             : AppMenuModel::IsCommandIdEnabled(command_id);
}

void HostedAppMenuModel::ExecuteCommand(int command_id, int event_flags) {
  if (command_id == kUninstallAppCommandId) {
    browser()->web_app_controller()->Uninstall(
        extensions::UNINSTALL_REASON_USER_INITIATED,
        extensions::UNINSTALL_SOURCE_HOSTED_APP_MENU);
  } else {
    AppMenuModel::ExecuteCommand(command_id, event_flags);
  }
}

void HostedAppMenuModel::LogMenuAction(AppMenuAction action_id) {
  UMA_HISTOGRAM_ENUMERATION("HostedAppFrame.WrenchMenu.MenuAction", action_id,
                            LIMIT_MENU_ACTION);
}
