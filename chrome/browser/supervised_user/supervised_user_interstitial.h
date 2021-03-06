// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SUPERVISED_USER_SUPERVISED_USER_INTERSTITIAL_H_
#define CHROME_BROWSER_SUPERVISED_USER_SUPERVISED_USER_INTERSTITIAL_H_

#include <string>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/supervised_user/supervised_user_service_observer.h"
#include "chrome/browser/supervised_user/supervised_user_url_filter.h"
#include "content/public/browser/interstitial_page_delegate.h"
#include "url/gurl.h"

namespace content {
class InterstitialPage;
class WebContents;
}

class Profile;

// Delegate for an interstitial page when a page is blocked for a supervised
// user because it is on a blacklist (in "allow everything" mode) or not on any
// whitelist (in "allow only specified sites" mode).
class SupervisedUserInterstitial : public content::InterstitialPageDelegate,
                                   public SupervisedUserServiceObserver {
 public:
  static void Show(content::WebContents* web_contents,
                   const GURL& url,
                   SupervisedUserURLFilter::FilteringBehaviorReason reason,
                   const base::Callback<void(bool)>& callback);

 private:
  SupervisedUserInterstitial(
      content::WebContents* web_contents,
      const GURL& url,
      SupervisedUserURLFilter::FilteringBehaviorReason reason,
      const base::Callback<void(bool)>& callback);
  ~SupervisedUserInterstitial() override;

  bool Init();

  // InterstitialPageDelegate implementation.
  std::string GetHTMLContents() override;
  void CommandReceived(const std::string& command) override;
  void OnProceed() override;
  void OnDontProceed() override;

  // SupervisedUserServiceObserver implementation.
  void OnURLFilterChanged() override;
  // TODO(treib): Also listen to OnCustodianInfoChanged and update as required.

  void OnAccessRequestAdded(bool success);

  // Returns whether there is a manual exception for the blocked URL.
  // Called initially before the interstitial is shown (to catch race
  // conditions), or when the URL filtering prefs change. Note that this does
  // not include the asynchronous online checks, so the return value indicates
  // either "allow" or "don't know".
  bool ShouldProceed();

  void DispatchContinueRequest(bool continue_request);

  // Owns the interstitial, which owns us.
  content::WebContents* web_contents_;

  Profile* profile_;

  content::InterstitialPage* interstitial_page_;  // Owns us.

  GURL url_;
  SupervisedUserURLFilter::FilteringBehaviorReason reason_;

  base::Callback<void(bool)> callback_;

  base::WeakPtrFactory<SupervisedUserInterstitial> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SupervisedUserInterstitial);
};

#endif  // CHROME_BROWSER_SUPERVISED_USER_SUPERVISED_USER_INTERSTITIAL_H_
