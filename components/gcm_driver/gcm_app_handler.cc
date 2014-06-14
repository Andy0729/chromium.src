// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/gcm_driver/gcm_app_handler.h"

namespace gcm {

GCMAppHandler::GCMAppHandler() {}
GCMAppHandler::~GCMAppHandler() {}

void GCMAppHandler::OnConnected(const net::IPEndPoint& ip_endpoint) {
}

void GCMAppHandler::OnDisconnected() {
}

}  // namespace gcm
