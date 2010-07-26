// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the Windows-specific declarations for trace_event.h.
#ifndef BASE_TRACE_EVENT_WIN_H_
#define BASE_TRACE_EVENT_WIN_H_
#pragma once

#include <string>
#include "base/event_trace_provider_win.h"

#define TRACE_EVENT_BEGIN(name, id, extra) \
  base::TraceLog::Trace(name, \
                        base::TraceLog::EVENT_BEGIN, \
                        reinterpret_cast<const void*>(id), \
                        extra);

#define TRACE_EVENT_END(name, id, extra) \
  base::TraceLog::Trace(name, \
                        base::TraceLog::EVENT_END, \
                        reinterpret_cast<const void*>(id), \
                        extra);

#define TRACE_EVENT_INSTANT(name, id, extra) \
  base::TraceLog::Trace(name, \
                        base::TraceLog::EVENT_INSTANT, \
                        reinterpret_cast<const void*>(id), \
                        extra);

// Fwd.
template <typename Type>
struct StaticMemorySingletonTraits;

namespace base {

// This EtwTraceProvider subclass implements ETW logging
// for the macros above on Windows.
class TraceLog : public EtwTraceProvider {
 public:
  enum EventType {
    EVENT_BEGIN,
    EVENT_END,
    EVENT_INSTANT
  };

  // Start logging trace events.
  // This is a noop in this implementation.
  static bool StartTracing();

  // Trace begin/end/instant events, this is the bottleneck implementation
  // all the others defer to.
  // Allowing the use of std::string for name or extra is a convenience,
  // whereas passing name or extra as a const char* avoids the construction
  // of temporary std::string instances.
  // If -1 is passed for name_len or extra_len, the strlen of the string will
  // be used for length.
  static void Trace(const char* name,
                    size_t name_len,
                    EventType type,
                    const void* id,
                    const char* extra,
                    size_t extra_len);

  // Allows passing extra as a std::string for convenience.
  static void Trace(const char* name,
                    EventType type,
                    const void* id,
                    const std::string& extra) {
    return Trace(name, -1, type, id, extra.c_str(), extra.length());
  }

  // Allows passing extra as a const char* to avoid constructing temporary
  // std::string instances where not needed.
  static void Trace(const char* name,
                    EventType type,
                    const void* id,
                    const char* extra) {
    return Trace(name, -1, type, id, extra, -1);
  }

  // Retrieves the singleton.
  // Note that this may return NULL post-AtExit processing.
  static TraceLog* Get();

  // Returns true iff tracing is turned on.
  bool IsTracing() {
    return enable_level() >= TRACE_LEVEL_INFORMATION;
  }

  // Emit a trace of type |type| containing |name|, |id|, and |extra|.
  // Note: |name| and |extra| must be NULL, or a zero-terminated string of
  //    length |name_len| or |extra_len| respectively.
  // Note: if name_len or extra_len are -1, the length of the corresponding
  //    string will be used.
  void TraceEvent(const char* name,
                  size_t name_len,
                  base::TraceLog::EventType type,
                  const void* id,
                  const char* extra,
                  size_t extra_len);

  // Exposed for unittesting only, allows resurrecting our
  // singleton instance post-AtExit processing.
  static void Resurrect();

 private:
  // Ensure only the provider can construct us.
  friend struct StaticMemorySingletonTraits<TraceLog>;
  TraceLog();

  DISALLOW_COPY_AND_ASSIGN(TraceLog);
};

// The ETW trace provider GUID.
extern const GUID kChromeTraceProviderName;

// The ETW event class GUID for 32 bit events.
extern const GUID kTraceEventClass32;

// The ETW event class GUID for 64 bit events.
extern const GUID kTraceEventClass64;

// The ETW event types, IDs 0x00-0x09 are reserved, so start at 0x10.
const EtwEventType kTraceEventTypeBegin = 0x10;
const EtwEventType kTraceEventTypeEnd = 0x11;
const EtwEventType kTraceEventTypeInstant = 0x12;

// If this flag is set in enable flags
enum TraceEventFlags {
  CAPTURE_STACK_TRACE = 0x0001,
};

// The event format consists of:
// The "name" string as a zero-terminated ASCII string.
// The id pointer in the machine bitness.
// The "extra" string as a zero-terminated ASCII string.
// Optionally the stack trace, consisting of a DWORD "depth", followed
//    by an array of void* (machine bitness) of length "depth".

// Forward decl.
struct TraceLogSingletonTraits;

}  // namespace base

#endif  // BASE_TRACE_EVENT_WIN_H_
