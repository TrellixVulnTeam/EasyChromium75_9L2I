// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "chromeos/components/power/dark_resume_controller.h"

#include <utility>

#include "services/device/public/mojom/constants.mojom.h"
#include "services/device/public/mojom/wake_lock_provider.mojom.h"

namespace chromeos {
namespace system {

// static.
constexpr base::TimeDelta DarkResumeController::kDarkResumeWakeLockCheckTimeout;
constexpr base::TimeDelta DarkResumeController::kDarkResumeHardTimeout;

DarkResumeController::DarkResumeController(
    service_manager::Connector* connector)
    : connector_(connector),
      wake_lock_observer_binding_(this),
      weak_ptr_factory_(this) {
  connector_->BindInterface(device::mojom::kServiceName,
                            mojo::MakeRequest(&wake_lock_provider_));
  PowerManagerClient::Get()->AddObserver(this);
}

DarkResumeController::~DarkResumeController() {
  PowerManagerClient::Get()->RemoveObserver(this);
}

void DarkResumeController::DarkSuspendImminent() {
  DVLOG(1) << __func__;
  suspend_readiness_cb_ =
      PowerManagerClient::Get()->GetSuspendReadinessCallback(FROM_HERE);
  // Schedule task that will check for any wake locks acquired in dark resume.
  DCHECK(!wake_lock_check_timer_.IsRunning());
  wake_lock_check_timer_.Start(
      FROM_HERE, kDarkResumeWakeLockCheckTimeout,
      base::BindOnce(
          &DarkResumeController::HandleDarkResumeWakeLockCheckTimeout,
          weak_ptr_factory_.GetWeakPtr()));
}

void DarkResumeController::SuspendDone(const base::TimeDelta& sleep_duration) {
  DVLOG(1) << __func__;
  // Clear any dark resume state when the device resumes.
  ClearDarkResumeState();
}

void DarkResumeController::OnWakeLockDeactivated(
    device::mojom::WakeLockType type) {
  // If this callback fires then one of two scenarios happened -
  // 1. No wake lock was held after |kDarkResumeWakeLockCheckTimeout|.
  // 2. Wake lock was held but was released before the hard timeout.
  //
  // The device is now ready to re-suspend after this dark resume event. Tell
  // the power daemon to re-suspend and invalidate any other state associated
  // with dark resume.
  DVLOG(1) << __func__;
  // The observer is only registered once dark resume starts.
  DCHECK(suspend_readiness_cb_);
  std::move(suspend_readiness_cb_).Run();
  ClearDarkResumeState();
}

bool DarkResumeController::IsDarkResumeStateSetForTesting() const {
  return !suspend_readiness_cb_.is_null() &&
         wake_lock_observer_binding_.is_bound();
}

bool DarkResumeController::IsDarkResumeStateClearedForTesting() const {
  return !weak_ptr_factory_.HasWeakPtrs() &&
         !wake_lock_check_timer_.IsRunning() &&
         !hard_timeout_timer_.IsRunning() && suspend_readiness_cb_.is_null() &&
         !wake_lock_observer_binding_.is_bound();
}

void DarkResumeController::HandleDarkResumeWakeLockCheckTimeout() {
  DVLOG(1) << __func__;
  DCHECK_CALLED_ON_VALID_SEQUENCE(dark_resume_tasks_sequence_checker_);
  wake_lock_check_timer_.Stop();
  // Setup observer for wake lock deactivation. If a wake lock is not activated
  // this calls back immediately, else whenever the wake lock is deactivated.
  // The device will be suspended on a deactivation notification i.e. in
  // OnDeactivation.
  device::mojom::WakeLockObserverPtr observer;
  wake_lock_observer_binding_.Bind(mojo::MakeRequest(&observer));
  wake_lock_provider_->NotifyOnWakeLockDeactivation(
      device::mojom::WakeLockType::kPreventAppSuspension, std::move(observer));

  // Schedule task that will tell the power daemon to re-suspend after a dark
  // resume irrespective of any state. This is a last resort timeout to ensure
  // the device doesn't stay up indefinitely in dark resume.
  DCHECK(!hard_timeout_timer_.IsRunning());
  hard_timeout_timer_.Start(
      FROM_HERE, kDarkResumeHardTimeout,
      base::BindOnce(&DarkResumeController::HandleDarkResumeHardTimeout,
                     weak_ptr_factory_.GetWeakPtr()));
}

void DarkResumeController::HandleDarkResumeHardTimeout() {
  DVLOG(1) << __func__;
  DCHECK_CALLED_ON_VALID_SEQUENCE(dark_resume_tasks_sequence_checker_);
  hard_timeout_timer_.Stop();
  // Enough is enough. Tell power daemon it's okay to suspend.
  DCHECK(suspend_readiness_cb_);
  std::move(suspend_readiness_cb_).Run();
  ClearDarkResumeState();
}

void DarkResumeController::ClearDarkResumeState() {
  DVLOG(1) << __func__;
  // Reset the callback that is used to trigger a re-suspend. Won't be needed
  // if the dark resume state machine is ending.
  suspend_readiness_cb_.Reset();

  // This automatically invalidates any WakeLockObserver and associated callback
  // in this case OnDeactivation.
  wake_lock_observer_binding_.Close();

  // Stops timer and invalidates HandleDarkResumeWakeLockCheckTimeout.
  wake_lock_check_timer_.Stop();

  // Stops timer and invalidates HandleDarkResumeHardTimeout.
  hard_timeout_timer_.Stop();

  // At this point all pending callbacks should be invalidated. This is a last
  // fail safe to not have any lingering tasks associated with the dark resume
  // state machine.
  weak_ptr_factory_.InvalidateWeakPtrs();
}

}  // namespace system
}  // namespace chromeos
