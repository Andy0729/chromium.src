// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CHROME_BROWSER_CHROMEOS_SYSTEM_LOGS_LSB_RELEASE_LOG_SOURCE_H_
#define CHROME_BROWSER_CHROMEOS_SYSTEM_LOGS_LSB_RELEASE_LOG_SOURCE_H_

#include "chrome/browser/feedback/system_logs/system_logs_fetcher_base.h"

namespace system_logs {

// Fetches release information form /etc/lsb-release file.
class LsbReleaseLogSource : public SystemLogsSource {
 public:
  LsbReleaseLogSource() {}
  virtual ~LsbReleaseLogSource() {}

  // SystemLogsSource override.
  virtual void Fetch(const SysLogsSourceCallback& callback) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(LsbReleaseLogSource);
};

}  // namespace system_logs

#endif  // CHROME_BROWSER_CHROMEOS_SYSTEM_LOGS_LSB_RELEASE_LOG_SOURCE_H_
