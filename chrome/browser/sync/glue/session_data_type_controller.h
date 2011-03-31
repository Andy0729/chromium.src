// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_SESSION_DATA_TYPE_CONTROLLER_H_
#define CHROME_BROWSER_SYNC_GLUE_SESSION_DATA_TYPE_CONTROLLER_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/sync/glue/data_type_controller.h"
#include "chrome/browser/sync/glue/session_model_associator.h"

class ProfileSyncFactory;
class ProfileSyncService;

namespace browser_sync {

class AssociatorInterface;
class ChangeProcessor;

class SessionDataTypeController : public DataTypeController {
 public:
  SessionDataTypeController(
      ProfileSyncFactory* profile_sync_factory,
      ProfileSyncService* sync_service);
  virtual ~SessionDataTypeController();

  // DataTypeController implementation.
  virtual void Start(StartCallback* start_callback);

  virtual void Stop();

  virtual bool enabled();

  virtual syncable::ModelType type();

  virtual browser_sync::ModelSafeGroup model_safe_group();

  virtual const char* name() const;

  virtual State state();

  // UnrecoverableErrorHandler interface.
  virtual void OnUnrecoverableError(
      const tracked_objects::Location& from_here,
      const std::string& message);

  SessionModelAssociator* GetModelAssociator();

 private:
  // Helper method to run the stashed start callback with a given result.
  void FinishStart(StartResult result,
      const tracked_objects::Location& location);

  // Cleans up state and calls callback when start fails.
  void StartFailed(StartResult result,
      const tracked_objects::Location& location);

  ProfileSyncFactory* profile_sync_factory_;
  ProfileSyncService* sync_service_;

  State state_;

  scoped_ptr<StartCallback> start_callback_;
  scoped_ptr<AssociatorInterface> model_associator_;
  scoped_ptr<ChangeProcessor> change_processor_;

  DISALLOW_COPY_AND_ASSIGN(SessionDataTypeController);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_SESSION_DATA_TYPE_CONTROLLER_H_

