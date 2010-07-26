// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_IDLE_H_
#define CHROME_BROWSER_IDLE_H_
#pragma once

enum IdleState {
  IDLE_STATE_ACTIVE = 0,
  IDLE_STATE_IDLE = 1,   // No activity within threshold.
  IDLE_STATE_LOCKED = 2  // Only available on supported systems.
};

IdleState CalculateIdleState(unsigned int idle_threshold);

#endif  // CHROME_BROWSER_IDLE_H_
