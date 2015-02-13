// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

var mobileNav = false;

/**
 * For small screen mobile the navigation buttons are moved
 * below the advanced text.
 */
function onResize() {
  var helpOuterBox = document.querySelector('#details');
  var mainContent = document.querySelector('#main-content');
  var mediaQuery = '(min-width: 300px) and (max-width: 420px) and ' +
      '(orientation: portrait), (max-width: 736px) and ' +
      '(max-height: 420px) and (orientation: landscape)';
  var detailsHidden = helpOuterBox.classList.contains('hidden');

  // Check for change in nav status.
  if (mobileNav != window.matchMedia(mediaQuery).matches) {
    mobileNav = !mobileNav;

    // Handle showing the top content / details sections according to state.
    if (mobileNav) {
      mainContent.classList.toggle('hidden', !detailsHidden);
      helpOuterBox.classList.toggle('hidden', detailsHidden);
    } else if (!detailsHidden) {
      // Non mobile nav with visible details.
      mainContent.classList.remove('hidden');
      helpOuterBox.classList.remove('hidden');
    }
  }
}

function setupMobileNav() {
  window.addEventListener('resize', onResize);
  onResize();
}

document.addEventListener('DOMContentLoaded', setupMobileNav);
