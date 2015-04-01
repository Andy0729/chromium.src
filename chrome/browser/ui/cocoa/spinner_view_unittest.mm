// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/spinner_view.h"

#import "ui/gfx/test/ui_cocoa_test_helper.h"

@interface SpinnerView(ExposedForTesting)

- (BOOL)is_animating;

@end

@implementation SpinnerView(ExposedForTesting)

- (BOOL)is_animating
{
  return is_animating_;
}

@end

namespace {

  class SpinnerViewTest : public ui::CocoaTest {
  public:
    SpinnerViewTest() {
      CGRect frame = NSMakeRect(0.0, 0.0, 16.0, 16.0);
      view_.reset([[SpinnerView alloc] initWithFrame:frame]);
      [[test_window() contentView] addSubview:view_];
    }

    base::scoped_nsobject<SpinnerView> view_;
  };

  TEST_VIEW(SpinnerViewTest, view_)

  TEST_F(SpinnerViewTest, StopAnimationOnMiniaturize) {
    EXPECT_TRUE([view_ is_animating]);

    [test_window() miniaturize:nil];
    EXPECT_FALSE([view_ is_animating]);

    [test_window() deminiaturize:nil];
    EXPECT_TRUE([view_ is_animating]);
  }

  TEST_F(SpinnerViewTest,
         StopAnimationOnRemoveFromSuperview) {
    EXPECT_TRUE([view_ is_animating]);

    [view_ removeFromSuperview];
    EXPECT_FALSE([view_ is_animating]);

    [[test_window() contentView] addSubview:view_];
    EXPECT_TRUE([view_ is_animating]);
  }

  TEST_F(SpinnerViewTest, StopAnimationOnHidden) {
    EXPECT_TRUE([view_ is_animating]);

    [view_ setHidden:YES];
    EXPECT_FALSE([view_ is_animating]);

    [view_ setHidden:NO];
    EXPECT_TRUE([view_ is_animating]);
  }
}

