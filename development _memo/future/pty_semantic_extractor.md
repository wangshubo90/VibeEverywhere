# Terminal Semantic Extractor – Architecture Draft

## 1. Purpose

The Terminal Semantic Extractor is a host-side module that sits between:

- raw PTY byte stream
- terminal screen reconstruction
- session lifecycle / attention inference

Its purpose is to extract **weak but useful semantic hints** from terminal behavior without depending on any provider-specific API.

It should help answer questions like:

- is the terminal showing a spinner or progress loop?
- is the session likely waiting for confirmation?
- is there a visible prompt?
- is the screen actively redrawing one status line?
- is the terminal behaving like an interactive menu?

This module should **not** define lifecycle by itself.

It should provide **semantic hints** that can support or raise confidence in higher-level inference.

---

# 2. Design Principle

The extractor should follow this rule:

> PTY semantics are hints, not truth.

Truth should still come from:

- runtime lifecycle state
- process state
- filesystem activity
- structured session control events
- timing

The extractor is an enrichment layer.

---

# 3. Position In The Architecture

```text
PTY bytes
   ↓
ANSI / terminal parser
   ↓
Virtual screen buffer
   ↓
Terminal Semantic Extractor
   ↓
terminalHints
   ↓
Session inference engine
   ↓
lifecycle / attention / UI summaries
```

---

# 4. Inputs

The extractor consumes two categories of input.

## 4.1 Stream-level input

Directly from the PTY stream:

- raw byte chunks
- timestamps
- chunk sizes

Examples:

- `\r`
- `\n`
- `ESC[K`
- `ESC[A`
- `ESC[J`
- visible text bytes
- repeated short status updates

---

## 4.2 Screen-level input

Produced by terminal emulation / screen reconstruction:

- current visible screen buffer
- cursor row / column
- bottom visible lines
- line diff since last frame
- recent redraw history

Examples:

- current bottom line text
- whether line N changed repeatedly
- whether the prompt line is visible
- whether a confirmation string is present

---

# 5. Outputs

The extractor should not output a full lifecycle state.

It should output a structured hint object.

Recommended shape:

```json
{
  "spinnerDetected": false,
  "progressDetected": false,
  "confirmationPromptDetected": false,
  "promptVisible": false,
  "menuDetected": false,
  "dynamicStatusLineDetected": false,
  "screenStable": true,
  "screenChangedRecently": true,
  "bottomLineText": "",
  "hintConfidence": {
    "spinner": 0.0,
    "progress": 0.0,
    "confirmation": 0.0,
    "prompt": 0.0,
    "menu": 0.0
  }
}
```

This output should remain:

- lightweight
- explainable
- provider-agnostic

---

# 6. Core Responsibilities

The extractor should focus on six capabilities.

## 6.1 Dynamic line detection

Detect whether one or more visible lines are being repeatedly overwritten or refreshed.

Signals:

- frequent carriage returns (`\r`)
- frequent `ESC[K`
- repeated changes on same screen row
- high line-rewrite ratio relative to newline creation

This is useful for:

- spinner detection
- progress detection
- dynamic status line detection

---

## 6.2 Bottom status line extraction

Many coding CLIs place active prompt/status UI in the bottom visible lines.

The extractor should collect:

- last visible line
- second-last visible line
- last N visible lines

Recommended:

```text
bottomLines = visibleScreen[-5:]
```

These lines are useful for:

- confirmation prompts
- visible prompt detection
- menu detection
- status text extraction

Important:
This is a weak signal and must not become a hard dependency.

---

## 6.3 Prompt detection

Detect whether the visible terminal looks like it is waiting at a prompt.

Examples:

- `$ `
- `> `
- `>>> `
- provider prompt shell line
- custom coding CLI input line

Signals:

- cursor near bottom
- stable bottom line
- prompt-like suffix
- no dynamic rewrite in recent window

This can support:

- waiting input hints
- “safe to type” UI
- mobile input mode switching

---

## 6.4 Confirmation / menu detection

Detect text patterns that often imply user choice.

Examples:

- `[y/n]`
- `(y/n)`
- `Press enter to continue`
- `Approve changes?`
- numbered option list
- arrow-key menu indicators
- `Continue?`

Important:
This is the most useful early semantic hint.

The extractor should support a small generic regex set.

---

## 6.5 Spinner / progress detection

Detect terminal patterns that indicate active waiting or background work.

Possible signals:

- repeated carriage-return updates
- repeated line overwrite with small text differences
- rotating single-character patterns
- unicode braille spinner patterns
- increasing percentage text
- progress-bar style repeated line

This should not define lifecycle, but can help distinguish:

- active waiting
- silent stall
- visible progress

---

## 6.6 Screen stability measurement

Track whether the screen is:

- changing rapidly
- stable
- partially redrawing
- mostly frozen

Recommended outputs:

```text
screenChangedRecently
screenStable
dynamicStatusLineDetected
```

This can help:

- mobile preview
- session watch summaries
- stall heuristics

---

# 7. Internal Submodules

The extractor should be split into separate parts.

## 7.1 ANSI Stats Collector

Consumes PTY chunks and tracks low-level control sequence frequency.

Suggested counters:

```text
carriageReturnCount
newlineCount
clearLineCount
clearScreenCount
cursorUpCount
cursorDownCount
cursorMoveCount
escapeSequenceCount
```

Windowed rates are preferred over absolute totals.

Example:

```text
carriageReturnRatePer10s
clearLineRatePer10s
```

Purpose:
identify dynamic terminal UI patterns.

---

## 7.2 Virtual Screen Adapter

Wraps the terminal emulator / screen buffer and provides simplified accessors.

Suggested methods:

```text
getBottomLines(n)
getCursorPosition()
getVisibleScreenHash()
getChangedRows()
getRowText(row)
```

Purpose:
avoid coupling the extractor to raw emulator internals.

---

## 7.3 Line Rewrite Tracker

Tracks repeated changes on the same rows.

Suggested metrics:

```text
recentlyRewrittenRows
sameRowRewriteRate
bottomRowRewriteRate
lineMutationEntropy
```

Purpose:
detect spinner / status-line behavior.

---

## 7.4 Pattern Matcher

Matches generic text patterns against visible lines and recent output text.

Suggested pattern categories:

- confirmation
- prompt
- menu
- progress
- spinner-like textual forms

This should remain small and configurable.

---

## 7.5 Hint Synthesizer

Combines:

- ANSI stats
- line rewrite behavior
- visible bottom lines
- regex matches

into final `terminalHints`.

This is the only submodule that emits semantic outputs.

---

# 8. Hint Definitions

Recommended first-wave hints:

## 8.1 `spinnerDetected`

Meaning:
The terminal appears to be repeatedly redrawing a small status line in place.

Likely signals:

- high carriage return rate
- repeated rewrite of same row
- low net newline growth
- small text mutation per frame

Use:
support active waiting / running task inference

---

## 8.2 `progressDetected`

Meaning:
The terminal appears to be showing a progress bar, percentage, or step counter.

Likely signals:

- repeated same-row rewrite
- visible `%`
- bar-like `[==== ]`
- incremental numeric change

Use:
support running task detection

---

## 8.3 `confirmationPromptDetected`

Meaning:
The terminal appears to be asking for a user confirmation or approval.

Likely patterns:

- `[y/n]`
- `(y/n)`
- `continue?`
- `approve`
- `press enter`
- `select option`

Use:
support awaiting-input attention

This is the highest-value hint.

---

## 8.4 `promptVisible`

Meaning:
The screen appears to end in a stable input prompt.

Signals:

- prompt-like bottom line
- stable cursor position
- little active redraw
- visible command input area

Use:
support input-ready UI state

---

## 8.5 `menuDetected`

Meaning:
The visible terminal appears to present a choice/menu interface.

Possible patterns:

- numbered options
- selection arrows
- highlighted rows
- bracketed options
- “choose”, “select”, “option”

Use:
support future richer mobile controls

---

## 8.6 `dynamicStatusLineDetected`

Meaning:
The terminal repeatedly refreshes one or more visible status lines.

Use:
general-purpose support signal for:
- spinner
- progress
- active waiting
- UI-rich CLIs

---

# 9. Reliability Model

Not all hints are equally reliable.

Recommended reliability ranking:

```text
confirmationPromptDetected
promptVisible
progressDetected
spinnerDetected
menuDetected
dynamicStatusLineDetected
```

Even the strongest hint must still remain below lifecycle truth.

Suggested principle:

- hints may raise confidence
- hints may refine display
- hints may assist attention reasoning
- hints must not override hard runtime truth

---

# 10. How Hints Feed Session Inference

The semantic extractor should influence inference in limited, safe ways.

## 10.1 Good uses

### Awaiting input confirmation

If:

- lifecycle is Running
- confirmationPromptDetected = true
- low filesystem activity
- screen stable near bottom

Then:
raise confidence that session is effectively waiting for input

Possible output:
```text
attentionReasonHint = possible_confirmation_prompt
```

---

### Running task confidence

If:

- spinnerDetected or progressDetected
- subprocess exists or recent output exists

Then:
raise confidence for `RUNNING_TASK`

---

### Stall suppression

If:

- process alive
- low file activity
- low newline growth
- but dynamicStatusLineDetected = true

Then:
do not immediately classify as stalled

This is a very important use.

---

## 10.2 Bad uses

Do not do:

- “thinking...” text => definitely THINKING
- “done” text => definitely COMPLETED
- bottom line contains a word => override lifecycle
- one spinner-like frame => classify running task

These would be too fragile.

---

# 11. Session Preview Use Case

One major benefit of screen reconstruction is preview.

The extractor should support a lightweight preview payload:

```json
{
  "bottomLines": [
    "Running tests...",
    "FAIL parser_test.cpp",
    "Press enter to continue"
  ],
  "promptVisible": true,
  "confirmationPromptDetected": true
}
```

This can be extremely useful for:

- mobile session cards
- notifications
- watch view
- “needs attention” summaries

This may become one of the best UX features later.

---

# 12. Suggested V1 Scope

Do not implement everything at once.

Recommended v1:

## Phase 1
Implement:
- ANSI stats collector
- bottom line extraction
- confirmation prompt detection
- promptVisible detection
- dynamicStatusLineDetected

This is already highly valuable.

---

## Phase 2
Add:
- spinner detection
- progress detection
- line rewrite tracker

This improves task-state inference.

---

## Phase 3
Add:
- menu detection
- preview summaries
- confidence scoring
- provider-specific hint packs

---

# 13. Data Retention Model

The extractor should work on recent windows, not full history.

Recommended retained state:

- recent PTY chunk window (10–30s)
- recent ANSI stats window
- visible screen buffer snapshot
- recent changed rows
- bottom line history

Do not retain unlimited semantic history inside this module.

Long-term event history belongs elsewhere.

---

# 14. Example Rule Sketches

## 14.1 Confirmation prompt

```text
if bottomLines match any confirmation regex
and screenStable == true
then confirmationPromptDetected = true
```

---

## 14.2 Dynamic status line

```text
if bottomRowRewriteRate > threshold
and newline growth is low
then dynamicStatusLineDetected = true
```

---

## 14.3 Spinner

```text
if carriageReturnRate high
and same-row small mutations repeat
then spinnerDetected = true
```

---

## 14.4 Progress

```text
if repeated same-row rewrite
and visible line contains percent / progress bar pattern
then progressDetected = true
```

---

# 15. Suggested Regex Families

The regex set should remain generic and minimal.

## 15.1 Confirmation
Examples:

```text
\[y\/n\]
\([yY]\/[nN]\)
press enter
continue\?
approve
confirm
select an option
```

## 15.2 Prompt
Examples:

```text
^\$ $
^> $
^>>> $
[:>] $
```

## 15.3 Progress
Examples:

```text
[0-9]{1,3}%
\[[=\-# >]+\]
running
compiling
testing
building
```

These should be configurable, not hardcoded forever.

---

# 16. Implementation Guidance

## 16.1 Do not build this into the terminal transport layer

The PTY transport layer should remain dumb:

- read bytes
- write bytes
- forward chunks

The extractor must be a separate semantic layer.

---

## 16.2 Do not couple directly to one provider

Never write logic like:

- if Claude says X
- if Gemini says Y

Provider packs may exist later, but the core extractor must stay generic.

---

## 16.3 Keep the extractor explainable

For every hint emitted, the runtime should be able to explain why.

Example:

```json
{
  "spinnerDetected": true,
  "explanation": [
    "high carriage return rate",
    "same row rewritten 12 times in 5s",
    "newline growth low"
  ]
}
```

This is optional but very useful for debugging.

---

# 17. Recommended Final Role

The Terminal Semantic Extractor should become:

> A weak-signal semantic layer that converts terminal redraw behavior and visible screen content into stable hints for session supervision.

It should not be the source of truth.

But it can become one of the most powerful differentiators in Vibe, because most systems stop at:

- PTY bytes
- xterm rendering

and never elevate terminal behavior into:

- supervision hints
- session preview
- attention-aware UX