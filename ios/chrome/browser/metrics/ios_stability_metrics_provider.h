// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_METRICS_IOS_STABILITY_METRICS_PROVIDER_H_
#define IOS_CHROME_BROWSER_METRICS_IOS_STABILITY_METRICS_PROVIDER_H_

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "components/metrics/metrics_provider.h"

namespace metrics {
class MetricsService;
}

// Exposed for testing purposes only.
// Values of the UMA Stability.MobileSessionShutdownType histogram.
enum MobileSessionShutdownType {
  SHUTDOWN_IN_BACKGROUND = 0,
  SHUTDOWN_IN_FOREGROUND_NO_CRASH_LOG_NO_MEMORY_WARNING,
  SHUTDOWN_IN_FOREGROUND_WITH_CRASH_LOG_NO_MEMORY_WARNING,
  SHUTDOWN_IN_FOREGROUND_NO_CRASH_LOG_WITH_MEMORY_WARNING,
  SHUTDOWN_IN_FOREGROUND_WITH_CRASH_LOG_WITH_MEMORY_WARNING,
  FIRST_LAUNCH_AFTER_UPGRADE,
  MOBILE_SESSION_SHUTDOWN_TYPE_COUNT,
};

class IOSStabilityMetricsProvider : public metrics::MetricsProvider {
 public:
  explicit IOSStabilityMetricsProvider(
      metrics::MetricsService* metrics_service);
  ~IOSStabilityMetricsProvider() override;

  // metrics::MetricsProvider
  bool HasInitialStabilityMetrics() override;
  void ProvideInitialStabilityMetrics(
      metrics::SystemProfileProto* system_profile_proto) override;

 protected:
  // Provides information on the last session environment, used to decide what
  // stability metrics to provide in ProvideInitialStabilityMetrics.
  // These methods are virtual to be overridden in the tests.
  // The default implementations return the real values.

  // Whether this is the first time the app is launched after an upgrade.
  virtual bool IsFirstLaunchAfterUpgrade();

  // Whether there are crash reports to upload.
  virtual bool HasCrashLogs();

  // Whether there were crash reports that have been uploaded in background
  // since the last full start.
  virtual bool HasUploadedCrashReportsInBackground();

  // Whether there was a memory warning shortly before last shutdown.
  virtual bool ReceivedMemoryWarningBeforeLastShutdown();

 private:
  metrics::MetricsService* metrics_service_;
  DISALLOW_COPY_AND_ASSIGN(IOSStabilityMetricsProvider);
};

#endif  // IOS_CHROME_BROWSER_METRICS_IOS_STABILITY_METRICS_PROVIDER_H_
