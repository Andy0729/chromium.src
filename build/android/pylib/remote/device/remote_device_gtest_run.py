# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Run specific test on specific environment."""

import logging
import os
import sys

from pylib import constants
from pylib.base import base_test_result
from pylib.remote.device import appurify_sanitized
from pylib.remote.device import remote_device_test_run
from pylib.remote.device import remote_device_helper


class RemoteDeviceGtestRun(remote_device_test_run.RemoteDeviceTestRun):
  """Run gtests and uirobot tests on a remote device."""

  DEFAULT_RUNNER_PACKAGE = (
      'org.chromium.native_test.ChromeNativeTestInstrumentationTestRunner')

  #override
  def TestPackage(self):
    return self._test_instance.suite

  #override
  def _TriggerSetUp(self):
    """Set up the triggering of a test run."""
    logging.info('Triggering test run.')

    if self._env.runner_type:
      logging.warning('Ignoring configured runner_type "%s"',
                      self._env.runner_type)

    if not self._env.runner_package:
      runner_package = self.DEFAULT_RUNNER_PACKAGE
      logging.info('Using default runner package: %s',
                   self.DEFAULT_RUNNER_PACKAGE)
    else:
      runner_package = self._env.runner_package

    dummy_app_path = os.path.join(
        constants.GetOutDirectory(), 'apks', 'remote_device_dummy.apk')
    self._AmInstrumentTestSetup(dummy_app_path, self._test_instance.apk,
                                runner_package)

  _INSTRUMENTATION_STREAM_LEADER = 'INSTRUMENTATION_STATUS: stream='

  #override
  def _ParseTestResults(self):
    logging.info('Parsing results from stdout.')
    results = base_test_result.TestRunResults()
    if self._results['results']['exception']:
      results.AddResult(base_test_result.BaseTestResult(
          self._results['results']['exception'],
          base_test_result.ResultType.FAIL))
    else:
      output = self._results['results']['output'].splitlines()
      output = (l[len(self._INSTRUMENTATION_STREAM_LEADER):] for l in output
                if l.startswith(self._INSTRUMENTATION_STREAM_LEADER))
      results_list = self._test_instance.ParseGTestOutput(output)
      results.AddResults(results_list)
      if not self._results['results']['pass']:
        results.AddResult(base_test_result.BaseTestResult(
            'Remote Service detected error.',
            base_test_result.ResultType.FAIL))
    return results
