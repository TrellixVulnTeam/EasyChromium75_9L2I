// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/frame_host/navigation_handle_impl.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "content/browser/appcache/appcache_navigation_handle.h"
#include "content/browser/appcache/appcache_service_impl.h"
#include "content/browser/child_process_security_policy_impl.h"
#include "content/browser/devtools/devtools_instrumentation.h"
#include "content/browser/frame_host/debug_urls.h"
#include "content/browser/frame_host/navigation_controller_impl.h"
#include "content/browser/frame_host/navigation_entry_impl.h"
#include "content/browser/frame_host/navigator.h"
#include "content/browser/frame_host/navigator_delegate.h"
#include "content/browser/loader/resource_dispatcher_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/service_worker/service_worker_context_wrapper.h"
#include "content/browser/service_worker/service_worker_navigation_handle.h"
#include "content/common/frame_messages.h"
#include "content/public/browser/navigation_ui_data.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/content_client.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/url_utils.h"
#include "net/base/net_errors.h"
#include "services/network/public/cpp/resource_request_body.h"

namespace content {

namespace {

// Default timeout for the READY_TO_COMMIT -> COMMIT transition.  Chosen
// initially based on the Navigation.ReadyToCommitUntilCommit UMA, and then
// refined based on feedback based on CrashExitCodes.Renderer/RESULT_CODE_HUNG.
constexpr base::TimeDelta kDefaultCommitTimeout =
    base::TimeDelta::FromSeconds(30);

// Timeout for the READY_TO_COMMIT -> COMMIT transition.
// Overrideable via SetCommitTimeoutForTesting.
base::TimeDelta g_commit_timeout = kDefaultCommitTimeout;

// Use this to get a new unique ID for a NavigationHandle during construction.
// The returned ID is guaranteed to be nonzero (zero is the "no ID" indicator).
int64_t CreateUniqueHandleID() {
  static int64_t unique_id_counter = 0;
  return ++unique_id_counter;
}

// LOG_NAVIGATION_TIMING_HISTOGRAM logs |value| for "Navigation.<histogram>" UMA
// as well as supplementary UMAs (depending on |transition| and |is_background|)
// for BackForward/Reload/NewNavigation variants.
//
// kMaxTime and kBuckets constants are consistent with
// UMA_HISTOGRAM_MEDIUM_TIMES, but a custom kMinTime is used for high fidelity
// near the low end of measured values.
//
// TODO(csharrison,nasko): This macro is incorrect for subframe navigations,
// which will only have subframe-specific transition types. This means that all
// subframes currently are tagged as NewNavigations.
#define LOG_NAVIGATION_TIMING_HISTOGRAM(histogram, transition, is_background, \
                                        duration)                             \
  do {                                                                        \
    const base::TimeDelta kMinTime = base::TimeDelta::FromMilliseconds(1);    \
    const base::TimeDelta kMaxTime = base::TimeDelta::FromMinutes(3);         \
    const int kBuckets = 50;                                                  \
    UMA_HISTOGRAM_CUSTOM_TIMES("Navigation." histogram, duration, kMinTime,   \
                               kMaxTime, kBuckets);                           \
    if (transition & ui::PAGE_TRANSITION_FORWARD_BACK) {                      \
      UMA_HISTOGRAM_CUSTOM_TIMES("Navigation." histogram ".BackForward",      \
                                 duration, kMinTime, kMaxTime, kBuckets);     \
    } else if (ui::PageTransitionCoreTypeIs(transition,                       \
                                            ui::PAGE_TRANSITION_RELOAD)) {    \
      UMA_HISTOGRAM_CUSTOM_TIMES("Navigation." histogram ".Reload", duration, \
                                 kMinTime, kMaxTime, kBuckets);               \
    } else if (ui::PageTransitionIsNewNavigation(transition)) {               \
      UMA_HISTOGRAM_CUSTOM_TIMES("Navigation." histogram ".NewNavigation",    \
                                 duration, kMinTime, kMaxTime, kBuckets);     \
    } else {                                                                  \
      NOTREACHED() << "Invalid page transition: " << transition;              \
    }                                                                         \
    if (is_background.has_value()) {                                          \
      if (is_background.value()) {                                            \
        UMA_HISTOGRAM_CUSTOM_TIMES("Navigation." histogram                    \
                                   ".BackgroundProcessPriority",              \
                                   duration, kMinTime, kMaxTime, kBuckets);   \
      } else {                                                                \
        UMA_HISTOGRAM_CUSTOM_TIMES("Navigation." histogram                    \
                                   ".ForegroundProcessPriority",              \
                                   duration, kMinTime, kMaxTime, kBuckets);   \
      }                                                                       \
    }                                                                         \
  } while (0)

void LogIsSameProcess(ui::PageTransition transition, bool is_same_process) {
  // Log overall value, then log specific value per type of navigation.
  UMA_HISTOGRAM_BOOLEAN("Navigation.IsSameProcess", is_same_process);

  if (transition & ui::PAGE_TRANSITION_FORWARD_BACK) {
    UMA_HISTOGRAM_BOOLEAN("Navigation.IsSameProcess.BackForward",
                          is_same_process);
    return;
  }
  if (ui::PageTransitionCoreTypeIs(transition, ui::PAGE_TRANSITION_RELOAD)) {
    UMA_HISTOGRAM_BOOLEAN("Navigation.IsSameProcess.Reload", is_same_process);
    return;
  }
  if (ui::PageTransitionIsNewNavigation(transition)) {
    UMA_HISTOGRAM_BOOLEAN("Navigation.IsSameProcess.NewNavigation",
                          is_same_process);
    return;
  }
  NOTREACHED() << "Invalid page transition: " << transition;
}

}  // namespace

NavigationHandleImpl::NavigationHandleImpl(
    NavigationRequest* navigation_request,
    const std::vector<GURL>& redirect_chain,
    int pending_nav_entry_id,
    net::HttpRequestHeaders request_headers,
    const Referrer& sanitized_referrer)
    : navigation_request_(navigation_request),
      net_error_code_(net::OK),
      was_redirected_(false),
      did_replace_entry_(false),
      should_update_history_(false),
      subframe_entry_committed_(false),
      request_headers_(std::move(request_headers)),
      pending_nav_entry_id_(pending_nav_entry_id),
      navigation_id_(CreateUniqueHandleID()),
      redirect_chain_(redirect_chain),
      reload_type_(ReloadType::NONE),
      restore_type_(RestoreType::NONE),
      navigation_type_(NAVIGATION_TYPE_UNKNOWN),
      is_same_process_(true),
      weak_factory_(this) {
  const GURL& url = navigation_request_->common_params().url;
  TRACE_EVENT_ASYNC_BEGIN2("navigation", "NavigationHandle", this,
                           "frame_tree_node",
                           frame_tree_node()->frame_tree_node_id(), "url",
                           url.possibly_invalid_spec());
  DCHECK(!navigation_request_->common_params().navigation_start.is_null());
  DCHECK(!IsRendererDebugURL(url));

  if (redirect_chain_.empty())
    redirect_chain_.push_back(url);

  // Mirrors the logic in RenderFrameImpl::SendDidCommitProvisionalLoad.
  if (navigation_request_->common_params().transition &
      ui::PAGE_TRANSITION_CLIENT_REDIRECT) {
    // If the page contained a client redirect (meta refresh,
    // document.location), set the referrer appropriately.
    sanitized_referrer_ =
        Referrer(redirect_chain_[0], sanitized_referrer.policy);
  } else {
    sanitized_referrer_ = sanitized_referrer;
  }

  // Try to match this with a pending NavigationEntry if possible.  Note that
  // the NavigationController itself may be gone if this is a navigation inside
  // an interstitial and the interstitial is asynchronously deleting itself due
  // to its tab closing.
  NavigationControllerImpl* nav_controller =
      static_cast<NavigationControllerImpl*>(
          frame_tree_node()->navigator()->GetController());
  if (pending_nav_entry_id_ && nav_controller) {
    NavigationEntryImpl* nav_entry =
        nav_controller->GetEntryWithUniqueID(pending_nav_entry_id_);
    if (!nav_entry &&
        nav_controller->GetPendingEntry() &&
        nav_controller->GetPendingEntry()->GetUniqueID() ==
            pending_nav_entry_id_) {
      nav_entry = nav_controller->GetPendingEntry();
    }

    if (nav_entry) {
      reload_type_ = nav_entry->reload_type();
      restore_type_ = nav_entry->restore_type();
    }
  }

#if defined(OS_ANDROID)
  navigation_handle_proxy_ = std::make_unique<NavigationHandleProxy>(this);
#endif

  if (IsInMainFrame()) {
    TRACE_EVENT_ASYNC_BEGIN_WITH_TIMESTAMP1(
        "navigation", "Navigation StartToCommit", this,
        navigation_request_->common_params().navigation_start, "Initial URL",
        url.spec());
  }

  if (IsSameDocument()) {
    TRACE_EVENT_ASYNC_STEP_INTO0("navigation", "NavigationHandle", this,
                                 "Same document");
  }
}

NavigationHandleImpl::~NavigationHandleImpl() {
#if defined(OS_ANDROID)
  navigation_handle_proxy_->DidFinish();
#endif

  GetDelegate()->DidFinishNavigation(this);

  if (IsInMainFrame()) {
    TRACE_EVENT_ASYNC_END2("navigation", "Navigation StartToCommit", this,
                           "URL",
                           navigation_request_->common_params().url.spec(),
                           "Net Error Code", net_error_code_);
  }
  TRACE_EVENT_ASYNC_END0("navigation", "NavigationHandle", this);
}

NavigatorDelegate* NavigationHandleImpl::GetDelegate() const {
  return frame_tree_node()->navigator()->GetDelegate();
}

int64_t NavigationHandleImpl::GetNavigationId() {
  return navigation_id_;
}

const GURL& NavigationHandleImpl::GetURL() {
  return navigation_request_->common_params().url;
}

SiteInstanceImpl* NavigationHandleImpl::GetStartingSiteInstance() {
  return navigation_request_->starting_site_instance();
}

bool NavigationHandleImpl::IsInMainFrame() {
  return frame_tree_node()->IsMainFrame();
}

bool NavigationHandleImpl::IsParentMainFrame() {
  if (frame_tree_node()->parent())
    return frame_tree_node()->parent()->IsMainFrame();

  return false;
}

bool NavigationHandleImpl::IsRendererInitiated() {
  return !navigation_request_->browser_initiated();
}

bool NavigationHandleImpl::WasServerRedirect() {
  return was_redirected_;
}

const std::vector<GURL>& NavigationHandleImpl::GetRedirectChain() {
  return redirect_chain_;
}

int NavigationHandleImpl::GetFrameTreeNodeId() {
  return frame_tree_node()->frame_tree_node_id();
}

RenderFrameHostImpl* NavigationHandleImpl::GetParentFrame() {
  if (IsInMainFrame())
    return nullptr;

  return frame_tree_node()->parent()->current_frame_host();
}

base::TimeTicks NavigationHandleImpl::NavigationStart() {
  return navigation_request_->common_params().navigation_start;
}

base::TimeTicks NavigationHandleImpl::NavigationInputStart() {
  return navigation_request_->common_params().input_start;
}

bool NavigationHandleImpl::IsPost() {
  return navigation_request_->common_params().method == "POST";
}

const scoped_refptr<network::ResourceRequestBody>&
NavigationHandleImpl::GetResourceRequestBody() {
  return navigation_request_->common_params().post_data;
}

const Referrer& NavigationHandleImpl::GetReferrer() {
  return sanitized_referrer_;
}

bool NavigationHandleImpl::HasUserGesture() {
  return navigation_request_->common_params().has_user_gesture;
}

ui::PageTransition NavigationHandleImpl::GetPageTransition() {
  return navigation_request_->common_params().transition;
}

const NavigationUIData* NavigationHandleImpl::GetNavigationUIData() {
  return navigation_request_->navigation_ui_data();
}

bool NavigationHandleImpl::IsExternalProtocol() {
  return !GetContentClient()->browser()->IsHandledURL(GetURL());
}

net::Error NavigationHandleImpl::GetNetErrorCode() {
  return net_error_code_;
}

RenderFrameHostImpl* NavigationHandleImpl::GetRenderFrameHost() {
  return navigation_request_->render_frame_host();
}

bool NavigationHandleImpl::IsSameDocument() {
  return navigation_request_->IsSameDocument();
}

const net::HttpRequestHeaders& NavigationHandleImpl::GetRequestHeaders() {
  return request_headers_;
}

void NavigationHandleImpl::RemoveRequestHeader(const std::string& header_name) {
  DCHECK(state() == NavigationRequest::PROCESSING_WILL_REDIRECT_REQUEST ||
         state() == NavigationRequest::WILL_REDIRECT_REQUEST);
  removed_request_headers_.push_back(header_name);
}

void NavigationHandleImpl::SetRequestHeader(const std::string& header_name,
                                            const std::string& header_value) {
  DCHECK(state() == NavigationRequest::INITIAL ||
         state() == NavigationRequest::PROCESSING_WILL_START_REQUEST ||
         state() == NavigationRequest::PROCESSING_WILL_REDIRECT_REQUEST ||
         state() == NavigationRequest::WILL_START_REQUEST ||
         state() == NavigationRequest::WILL_REDIRECT_REQUEST);
  modified_request_headers_.SetHeader(header_name, header_value);
}

const net::HttpResponseHeaders* NavigationHandleImpl::GetResponseHeaders() {
  if (response_headers_for_testing_)
    return response_headers_for_testing_.get();
  return navigation_request_->response()
             ? navigation_request_->response()->head.headers.get()
             : nullptr;
}

net::HttpResponseInfo::ConnectionInfo
NavigationHandleImpl::GetConnectionInfo() {
  return navigation_request_->response()
             ? navigation_request_->response()->head.connection_info
             : net::HttpResponseInfo::ConnectionInfo();
}

const base::Optional<net::SSLInfo> NavigationHandleImpl::GetSSLInfo() {
  return navigation_request_->ssl_info();
}

bool NavigationHandleImpl::IsWaitingToCommit() {
  return state() == NavigationRequest::READY_TO_COMMIT;
}

bool NavigationHandleImpl::HasCommitted() {
  return state() == NavigationRequest::DID_COMMIT ||
         state() == NavigationRequest::DID_COMMIT_ERROR_PAGE;
}

bool NavigationHandleImpl::IsErrorPage() {
  return state() == NavigationRequest::DID_COMMIT_ERROR_PAGE;
}

bool NavigationHandleImpl::HasSubframeNavigationEntryCommitted() {
  DCHECK(!IsInMainFrame());
  DCHECK(state() == NavigationRequest::DID_COMMIT ||
         state() == NavigationRequest::DID_COMMIT_ERROR_PAGE);
  return subframe_entry_committed_;
}

bool NavigationHandleImpl::DidReplaceEntry() {
  DCHECK(state() == NavigationRequest::DID_COMMIT ||
         state() == NavigationRequest::DID_COMMIT_ERROR_PAGE);
  return did_replace_entry_;
}

bool NavigationHandleImpl::ShouldUpdateHistory() {
  DCHECK(state() == NavigationRequest::DID_COMMIT ||
         state() == NavigationRequest::DID_COMMIT_ERROR_PAGE);
  return should_update_history_;
}

const GURL& NavigationHandleImpl::GetPreviousURL() {
  DCHECK(state() == NavigationRequest::DID_COMMIT ||
         state() == NavigationRequest::DID_COMMIT_ERROR_PAGE);
  return previous_url_;
}

net::IPEndPoint NavigationHandleImpl::GetSocketAddress() {
  // This is CANCELING because although the data comes in after
  // WILL_PROCESS_RESPONSE, it's possible for the navigation to be cancelled
  // after and the caller might want this value.
  DCHECK_GE(state(), NavigationRequest::CANCELING);
  return navigation_request_->response()
             ? navigation_request_->response()->head.remote_endpoint
             : net::IPEndPoint();
}

void NavigationHandleImpl::RegisterThrottleForTesting(
    std::unique_ptr<NavigationThrottle> navigation_throttle) {
  navigation_request_->RegisterThrottleForTesting(
      std::move(navigation_throttle));
}

#if defined(OS_ANDROID)
base::android::ScopedJavaGlobalRef<jobject>
NavigationHandleImpl::java_navigation_handle() {
  return navigation_handle_proxy_->java_navigation_handle();
}
#endif

bool NavigationHandleImpl::IsDeferredForTesting() {
  return navigation_request_->IsDeferredForTesting();
}

bool NavigationHandleImpl::WasStartedFromContextMenu() {
  return navigation_request_->common_params().started_from_context_menu;
}

const GURL& NavigationHandleImpl::GetSearchableFormURL() {
  return navigation_request_->begin_params()->searchable_form_url;
}

const std::string& NavigationHandleImpl::GetSearchableFormEncoding() {
  return navigation_request_->begin_params()->searchable_form_encoding;
}

ReloadType NavigationHandleImpl::GetReloadType() {
  return reload_type_;
}

RestoreType NavigationHandleImpl::GetRestoreType() {
  return restore_type_;
}

const GURL& NavigationHandleImpl::GetBaseURLForDataURL() {
  return navigation_request_->common_params().base_url_for_data_url;
}

NavigationData* NavigationHandleImpl::GetNavigationData() {
  return navigation_data_.get();
}

void NavigationHandleImpl::RegisterSubresourceOverride(
    mojom::TransferrableURLLoaderPtr transferrable_loader) {
  if (!transferrable_loader)
    return;

  navigation_request_->RegisterSubresourceOverride(
      std::move(transferrable_loader));
}

const GlobalRequestID& NavigationHandleImpl::GetGlobalRequestID() {
  DCHECK_GE(state(), NavigationRequest::PROCESSING_WILL_PROCESS_RESPONSE);
  return navigation_request_->request_id();
}

bool NavigationHandleImpl::IsDownload() {
  return navigation_request_->is_download();
}

bool NavigationHandleImpl::IsFormSubmission() {
  return navigation_request_->begin_params()->is_form_submission;
}

const std::string& NavigationHandleImpl::GetHrefTranslate() {
  return navigation_request_->common_params().href_translate;
}

void NavigationHandleImpl::CallResumeForTesting() {
  navigation_request_->CallResumeForTesting();
}

const base::Optional<url::Origin>& NavigationHandleImpl::GetInitiatorOrigin() {
  return navigation_request_->common_params().initiator_origin;
}

bool NavigationHandleImpl::IsSameProcess() {
  DCHECK(state() == NavigationRequest::DID_COMMIT ||
         state() == NavigationRequest::DID_COMMIT_ERROR_PAGE);
  return is_same_process_;
}

int NavigationHandleImpl::GetNavigationEntryOffset() {
  return navigation_request_->navigation_entry_offset();
}

bool NavigationHandleImpl::IsSignedExchangeInnerResponse() {
  return navigation_request_->response()
             ? navigation_request_->response()
                   ->head.is_signed_exchange_inner_response
             : false;
}

bool NavigationHandleImpl::WasResponseCached() {
  return navigation_request_->response()
             ? navigation_request_->response()->head.was_fetched_via_cache
             : false;
}

const net::ProxyServer& NavigationHandleImpl::GetProxyServer() {
  return proxy_server_;
}

void NavigationHandleImpl::InitServiceWorkerHandle(
    ServiceWorkerContextWrapper* service_worker_context) {
  service_worker_handle_.reset(
      new ServiceWorkerNavigationHandle(service_worker_context));
}

void NavigationHandleImpl::InitAppCacheHandle(
    ChromeAppCacheService* appcache_service) {
  // The final process id won't be available until
  // NavigationHandleImpl::ReadyToCommitNavigation.
  appcache_handle_.reset(new AppCacheNavigationHandle(
      appcache_service, ChildProcessHost::kInvalidUniqueID));
}

std::unique_ptr<AppCacheNavigationHandle>
NavigationHandleImpl::TakeAppCacheHandle() {
  return std::move(appcache_handle_);
}

void NavigationHandleImpl::UpdateStateFollowingRedirect(
    const GURL& new_referrer_url,
    ThrottleChecksFinishedCallback callback) {
  // The navigation should not redirect to a "renderer debug" url. It should be
  // blocked in NavigationRequest::OnRequestRedirected or in
  // ResourceLoader::OnReceivedRedirect.
  // Note: the call to GetURL below returns the post-redirect URL.
  // See https://crbug.com/728398.
  CHECK(!IsRendererDebugURL(GetURL()));

  // Update the navigation parameters.
  if (!(GetPageTransition() & ui::PAGE_TRANSITION_CLIENT_REDIRECT)) {
    sanitized_referrer_.url = new_referrer_url;
    sanitized_referrer_ =
        Referrer::SanitizeForRequest(GetURL(), sanitized_referrer_);
  }

  was_redirected_ = true;
  redirect_chain_.push_back(GetURL());

  navigation_request_->set_handle_state(
      NavigationRequest::PROCESSING_WILL_REDIRECT_REQUEST);

#if defined(OS_ANDROID)
  navigation_handle_proxy_->DidRedirect();
#endif

  complete_callback_ = std::move(callback);
}

void NavigationHandleImpl::ReadyToCommitNavigation(bool is_error) {
  TRACE_EVENT_ASYNC_STEP_INTO0("navigation", "NavigationHandle", this,
                               "ReadyToCommitNavigation");

  navigation_request_->set_handle_state(NavigationRequest::READY_TO_COMMIT);
  ready_to_commit_time_ = base::TimeTicks::Now();
  RestartCommitTimeout();

  if (appcache_handle_)
    appcache_handle_->SetProcessId(GetRenderFrameHost()->GetProcess()->GetID());

  // Record metrics for the time it takes to get to this state from the
  // beginning of the navigation.
  if (!IsSameDocument() && !is_error) {
    is_same_process_ =
        GetRenderFrameHost()->GetProcess()->GetID() ==
        frame_tree_node()->current_frame_host()->GetProcess()->GetID();
    LogIsSameProcess(GetPageTransition(), is_same_process_);

    // Don't log process-priority-specific UMAs for TimeToReadyToCommit2 metric
    // (which shouldn't be influenced by renderer priority).
    constexpr base::Optional<bool> kIsBackground = base::nullopt;

    base::TimeDelta delta =
        ready_to_commit_time_ -
        navigation_request_->common_params().navigation_start;
    LOG_NAVIGATION_TIMING_HISTOGRAM("TimeToReadyToCommit2", GetPageTransition(),
                                    kIsBackground, delta);

    if (IsInMainFrame()) {
      LOG_NAVIGATION_TIMING_HISTOGRAM("TimeToReadyToCommit2.MainFrame",
                                      GetPageTransition(), kIsBackground,
                                      delta);
    } else {
      LOG_NAVIGATION_TIMING_HISTOGRAM("TimeToReadyToCommit2.Subframe",
                                      GetPageTransition(), kIsBackground,
                                      delta);
    }

    if (is_same_process_) {
      LOG_NAVIGATION_TIMING_HISTOGRAM("TimeToReadyToCommit2.SameProcess",
                                      GetPageTransition(), kIsBackground,
                                      delta);
    } else {
      LOG_NAVIGATION_TIMING_HISTOGRAM("TimeToReadyToCommit2.CrossProcess",
                                      GetPageTransition(), kIsBackground,
                                      delta);
    }
  }

  navigation_request_->SetExpectedProcess(GetRenderFrameHost()->GetProcess());

  if (!IsSameDocument())
    GetDelegate()->ReadyToCommitNavigation(this);
}

void NavigationHandleImpl::DidCommitNavigation(
    const FrameHostMsg_DidCommitProvisionalLoad_Params& params,
    bool navigation_entry_committed,
    bool did_replace_entry,
    const GURL& previous_url,
    NavigationType navigation_type) {
  CHECK_EQ(GetURL(), params.url);

  did_replace_entry_ = did_replace_entry;
  should_update_history_ = params.should_update_history;
  previous_url_ = previous_url;
  base_url_ = params.base_url;
  navigation_type_ = navigation_type;

  // If an error page reloads, net_error_code might be 200 but we still want to
  // count it as an error page.
  if (params.base_url.spec() == kUnreachableWebDataURL ||
      net_error_code_ != net::OK) {
    TRACE_EVENT_ASYNC_STEP_INTO0("navigation", "NavigationHandle", this,
                                 "DidCommitNavigation: error page");
    navigation_request_->set_handle_state(
        NavigationRequest::DID_COMMIT_ERROR_PAGE);
  } else {
    TRACE_EVENT_ASYNC_STEP_INTO0("navigation", "NavigationHandle", this,
                                 "DidCommitNavigation");
    navigation_request_->set_handle_state(NavigationRequest::DID_COMMIT);
  }

  StopCommitTimeout();

  // Record metrics for the time it took to commit the navigation if it was to
  // another document without error.
  if (!IsSameDocument() && !IsErrorPage()) {
    base::TimeTicks now = base::TimeTicks::Now();
    base::TimeDelta delta =
        now - navigation_request_->common_params().navigation_start;
    ui::PageTransition transition = GetPageTransition();
    base::Optional<bool> is_background =
        GetRenderFrameHost()->GetProcess()->IsProcessBackgrounded();
    LOG_NAVIGATION_TIMING_HISTOGRAM("StartToCommit", transition, is_background,
                                    delta);
    if (IsInMainFrame()) {
      LOG_NAVIGATION_TIMING_HISTOGRAM("StartToCommit.MainFrame", transition,
                                      is_background, delta);
    } else {
      LOG_NAVIGATION_TIMING_HISTOGRAM("StartToCommit.Subframe", transition,
                                      is_background, delta);
    }
    if (is_same_process_) {
      LOG_NAVIGATION_TIMING_HISTOGRAM("StartToCommit.SameProcess", transition,
                                      is_background, delta);
      if (IsInMainFrame()) {
        LOG_NAVIGATION_TIMING_HISTOGRAM("StartToCommit.SameProcess.MainFrame",
                                        transition, is_background, delta);
      } else {
        LOG_NAVIGATION_TIMING_HISTOGRAM("StartToCommit.SameProcess.Subframe",
                                        transition, is_background, delta);
      }
    } else {
      LOG_NAVIGATION_TIMING_HISTOGRAM("StartToCommit.CrossProcess", transition,
                                      is_background, delta);
      if (IsInMainFrame()) {
        LOG_NAVIGATION_TIMING_HISTOGRAM("StartToCommit.CrossProcess.MainFrame",
                                        transition, is_background, delta);
      } else {
        LOG_NAVIGATION_TIMING_HISTOGRAM("StartToCommit.CrossProcess.Subframe",
                                        transition, is_background, delta);
      }
    }

    if (!ready_to_commit_time_.is_null()) {
      LOG_NAVIGATION_TIMING_HISTOGRAM("ReadyToCommitUntilCommit2",
                                      GetPageTransition(), is_background,
                                      now - ready_to_commit_time_);
    }
  }

  DCHECK(!IsInMainFrame() || navigation_entry_committed)
      << "Only subframe navigations can get here without changing the "
      << "NavigationEntry";
  subframe_entry_committed_ = navigation_entry_committed;

  // For successful navigations, ensure the frame owner element is no longer
  // collapsed as a result of a prior navigation.
  if (!IsErrorPage() && !frame_tree_node()->IsMainFrame()) {
    // The last committed load in collapsed frames will be an error page with
    // |kUnreachableWebDataURL|. Same-document navigation should not be
    // possible.
    DCHECK(!IsSameDocument() || !frame_tree_node()->is_collapsed());
    frame_tree_node()->SetCollapsed(false);
  }
}

void NavigationHandleImpl::RunCompleteCallback(
    NavigationThrottle::ThrottleCheckResult result) {
  DCHECK(result.action() != NavigationThrottle::DEFER);

  ThrottleChecksFinishedCallback callback = std::move(complete_callback_);
  complete_callback_.Reset();

  if (!complete_callback_for_testing_.is_null())
    std::move(complete_callback_for_testing_).Run(result);

  if (!callback.is_null())
    std::move(callback).Run(result);

  // No code after running the callback, as it might have resulted in our
  // destruction.
}

void NavigationHandleImpl::RenderProcessBlockedStateChanged(bool blocked) {
  if (blocked)
    StopCommitTimeout();
  else
    RestartCommitTimeout();
}

void NavigationHandleImpl::StopCommitTimeout() {
  commit_timeout_timer_.Stop();
  render_process_blocked_state_changed_subscription_.reset();
  GetRenderFrameHost()->GetRenderWidgetHost()->RendererIsResponsive();
}

void NavigationHandleImpl::RestartCommitTimeout() {
  commit_timeout_timer_.Stop();
  if (state() >= NavigationRequest::DID_COMMIT)
    return;

  RenderProcessHost* renderer_host =
      GetRenderFrameHost()->GetRenderWidgetHost()->GetProcess();
  if (!render_process_blocked_state_changed_subscription_) {
    render_process_blocked_state_changed_subscription_ =
        renderer_host->RegisterBlockStateChangedCallback(base::BindRepeating(
            &NavigationHandleImpl::RenderProcessBlockedStateChanged,
            base::Unretained(this)));
  }
  if (!renderer_host->IsBlocked())
    commit_timeout_timer_.Start(
        FROM_HERE, g_commit_timeout,
        base::BindRepeating(&NavigationHandleImpl::OnCommitTimeout,
                            weak_factory_.GetWeakPtr()));
}

void NavigationHandleImpl::OnCommitTimeout() {
  DCHECK_EQ(NavigationRequest::READY_TO_COMMIT, state());
  render_process_blocked_state_changed_subscription_.reset();
  GetRenderFrameHost()->GetRenderWidgetHost()->RendererIsUnresponsive(
      base::BindRepeating(&NavigationHandleImpl::RestartCommitTimeout,
                          weak_factory_.GetWeakPtr()));
}

// static
void NavigationHandleImpl::SetCommitTimeoutForTesting(
    const base::TimeDelta& timeout) {
  if (timeout.is_zero())
    g_commit_timeout = kDefaultCommitTimeout;
  else
    g_commit_timeout = timeout;
}

}  // namespace content
