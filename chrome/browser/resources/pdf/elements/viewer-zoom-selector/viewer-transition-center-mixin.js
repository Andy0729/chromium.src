// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This object can be used by Polymer.mixin in order to expose show() and
// hide() methods through core-transition-center.
var transitionCenterMixin = {
  ready: function() {
    this.super();
    this.state_ = { opened: false };
  },

  show: function(delay) {
    if (!this.state_.opened)
      this.toggle_(delay);
  },

  hide: function(delay) {
    if (this.state_.opened)
      this.toggle_(delay);
  },

  toggle_: function(delay) {
    delay = delay || 0;
    this.state_.opened = !this.state_.opened;
  }
};
