# MegaCustom Master Improvement Plan

This document captures the senior-level improvement plan for the overall app, with special focus on the logging system because it is currently one of the weakest operational surfaces.

The goal is not to add random features. The goal is to make MegaCustom feel like a reliable operations tool: predictable, inspectable, recoverable, and easy to use during real work.

## Executive Direction

The app should move toward one shared operational model:

```text
Plan -> Validate -> Run Job -> Track -> Recover -> History
```

Every major workflow should fit that model:

- watermarking
- distribution
- member registry changes
- course distribution
- downloader to watermark handoff
- bulk rename
- cloud copy
- folder mapping
- smart sync
- multi-account transfer

Today the app has many strong individual panels, but too much behavior is panel-local. That makes logs noisy, errors hard to understand, recovery inconsistent, and repeated workflows harder than they need to be.

## Current Logging Problems

The current logging system has useful pieces, but it is not yet a professional operational log.

Observed problems:

- Logs are cluttered with low-value progress messages.
- Important errors are mixed with routine noise.
- Some operations log to `LogManager`, some only emit panel-local messages, and many still use raw `qDebug()` / `qWarning()`.
- The GUI log viewer is table-heavy but not diagnosis-focused.
- The viewer does not clearly answer "what happened, why, what was affected, and what should I do next?"
- Activity logs and distribution history feel like separate worlds.
- Logs do not consistently carry a shared job ID, member ID, source path, destination path, operation type, retry count, or cleanup state.
- There is no clear event taxonomy, so similar actions can be logged under different names.
- Progress spam can make the log look broken even when the app is working.
- There is no support-bundle style export that captures logs, settings summary, app version, build hash, current job state, and recent errors together.

Technical concerns to verify during implementation:

- Confirm whether activity logs survive restart in the GUI, not only during the current process.
- Confirm whether details passed into `LogManager::log(..., details)` are preserved.
- Confirm whether `clear logs` actually clears persisted files, not just the in-memory cache.
- Confirm whether export pulls from durable history or only recent cached entries.
- Confirm whether search covers message, action, details, member, job, file, and destination fields.
- Confirm whether the Qt message handler is adding useful signal or just noise.

## Logging System Target

Logging should become an operational event system, not just a text dump.

Each log event should have structured fields:

- timestamp
- level
- category
- event name
- human message
- technical details
- job ID
- operation type
- member ID
- member name
- source path
- destination path
- local output path
- MEGA account ID/email if relevant
- status
- retry count
- error code
- recommended action
- cleanup state
- correlation ID for related events

Example:

```json
{
  "timestamp": 1781421000000,
  "level": "warning",
  "category": "distribution",
  "event": "upload_paused_disk_full",
  "jobId": "job_2026_06_14_001",
  "memberId": "john_smith",
  "sourcePath": "/tmp/latest-wm/John Smith/Course 1",
  "destinationPath": "/NHB+ Courses/John Smith/Course 1",
  "message": "Upload paused because local disk space is below the safe threshold.",
  "recommendedAction": "Clean temp workspace or free disk space, then resume the job.",
  "cleanupState": "empty_cloud_folders_may_exist"
}
```

## Logging UX Target

The log viewer should answer practical questions fast:

- What failed?
- What is still running?
- What was skipped?
- What was completed?
- Which member was affected?
- Which course/file/folder was affected?
- Where did it upload?
- Can this be retried safely?
- Is cleanup needed?
- What happened right before the error?

Recommended tabs:

- **Live Jobs**: current and recent active job events, grouped by job.
- **Issues**: warnings/errors only, deduplicated and actionable.
- **Activity**: normal chronological event stream.
- **Distribution History**: who got what, when, where.
- **Member Timeline**: events filtered by selected member.
- **Debug**: raw Qt/system logs, hidden behind an advanced toggle.

Recommended controls:

- Search across all structured fields.
- Quick filters: Errors, Warnings, Today, Current Job, Current Member, Needs Cleanup, Retriable.
- Group by job, member, category, or operation.
- Expand a row to show full structured details.
- Copy row, copy job report, copy support bundle.
- Open related member, open related destination, open related local file/folder.
- One-click cleanup or retry when the event supports it.

## Logging Implementation Plan

### Phase L1: Stabilize Current Logging

Goal: make the current system trustworthy before redesigning it.

Tasks:

- Preserve `details` fields correctly.
- Persist activity logs in a parseable format.
- Load recent activity from disk on app startup.
- Make `clear logs` remove or truncate persisted logs.
- Make export use durable log history.
- Make search case-insensitive and cover all important fields.
- Reduce noisy progress logging to debug-level or job-progress state instead of activity log spam.
- Add tooltips and clearer filter labels in the log viewer.
- Add a details pane for selected log rows.

Definition of done:

- Logs survive app restart.
- Clearing logs visibly clears persisted activity and distribution history.
- Export contains the same filtered rows the user sees.
- Searching by member ID, job ID, file name, or error text works.
- Downloader/watermark/distribution progress no longer floods normal activity logs.

### Phase L2: Event Taxonomy

Goal: make log names consistent across the app.

Create a central list of event names:

- `job.created`
- `job.started`
- `job.paused`
- `job.resumed`
- `job.cancelled`
- `job.completed`
- `job.failed`
- `job.cleanup_required`
- `member.matched`
- `member.skipped`
- `member.duplicated`
- `member.audit_failed`
- `watermark.file_started`
- `watermark.file_completed`
- `watermark.file_failed`
- `distribution.folder_created`
- `distribution.upload_started`
- `distribution.upload_completed`
- `distribution.upload_failed`
- `routing.match_high_confidence`
- `routing.match_low_confidence`
- `storage.disk_low`
- `storage.quota_low`
- `cleanup.started`
- `cleanup.completed`

Rules:

- Event names should be stable and searchable.
- UI text can be friendly, but stored event names should be consistent.
- Similar operations should not invent new event names in each panel.

### Phase L3: Job Correlation

Goal: every operation should have a job ID.

Add job IDs to:

- watermark runs
- auto-upload pipeline runs
- distribution runs
- downloader runs
- course distribution runs
- bulk rename runs
- cloud copy runs
- sync runs
- member import/sync runs

Each job should have:

- job ID
- operation type
- status
- created/started/finished timestamps
- affected members
- source roots
- destination roots
- counts: planned, completed, skipped, failed
- last error
- resume state
- cleanup state

This is the foundation for the future Job Center.

### Phase L4: Better Viewer

Goal: make logs useful during real work.

Upgrade `LogViewerPanel` into an operations console:

- Default to Issues/Jobs, not raw activity.
- Show current job summaries first.
- Show errors grouped by job and member.
- Deduplicate repeated errors.
- Show recommended action per issue.
- Add detail drawer with full structured event data.
- Add copy buttons for row, job report, and support bundle.
- Add quick links to related member, destination path, and source path.

### Phase L5: Support Bundle

Goal: make troubleshooting easier.

Add **Export Support Bundle**:

- filtered logs
- recent errors
- current job states
- failed job details
- member audit summary
- app version
- commit hash/build date
- OS/platform
- config summary without secrets
- FFmpeg/Python availability
- temp directory free space
- MEGA account quota summary if available

Never include:

- passwords
- session tokens
- MEGA auth tokens
- full secret config values

### Phase L6: Structured Storage

Goal: move from ad hoc files to queryable storage if needed.

Recommended backend:

- SQLite for job/event history.
- JSONL export for portable debug files.
- Retention settings by event type.

Tables:

- `jobs`
- `events`
- `job_members`
- `distribution_records`
- `cleanup_tasks`
- `support_exports`

This should only happen after L1-L4 prove the data model.

## App-Wide Master Improvements

### 1. App-Wide Job Center

Add one shared Job Center for all long-running operations.

It should show:

- running jobs
- queued jobs
- paused jobs
- failed jobs
- completed jobs
- per-member progress
- current file/folder
- estimated finish time
- retry count
- disk/quota state
- cleanup pending state

Controls:

- pause
- resume
- cancel
- retry failed
- clean temp files
- copy report
- open related panel

Why this matters:

- The app becomes easier to trust.
- Progress is no longer scattered across panels.
- Recovery becomes possible.
- Logs become tied to real jobs instead of loose messages.

### 2. Universal Preview / Dry Run

Every destructive or large operation should be able to generate a plan before running.

Examples:

- These files will be watermarked.
- These members will receive output.
- These cloud folders will be created.
- These files will be uploaded.
- These items will be renamed.
- These members are skipped because config is missing.
- These folders may conflict.

The user should approve a plan, not guess what a button will do.

Recommended UI:

- `Preview Plan`
- `Run Plan`
- `Export Plan`
- warnings before execution
- exact counts before execution

### 3. Health Center / Audit Center

Create a centralized Health Center that checks:

- member registry problems
- duplicate members
- invalid cloud paths
- inactive members in active groups
- missing distribution folders
- invalid watermark presets
- broken saved mappings
- missing dependencies
- disk space
- temp folder access
- MEGA account quota
- stale search index
- pending cleanup
- empty folders left by failed jobs

Each finding should have:

- severity
- affected object
- explanation
- recommended fix
- one-click fix when safe

### 4. Smart Routing Engine

Routing and matching should be centralized.

It should answer:

- Who is this content for?
- Which group does it belong to?
- Which member matched?
- Why did it match?
- How confident is the match?
- What destination path will be used?
- Is manual review needed?

This protects distribution/course workflows from accidental wrong-member routing.

### 5. Saved Workflows / Recipes

Turn repeated operational patterns into saved workflows.

Examples:

- Watermark NHB+ course and distribute.
- Watermark FF group only.
- Send per-member course folders to each member's course destination.
- Import members from CSV, audit, then update groups.
- Clean previous watermark temp files.
- Bulk rename course folders, then upload.

Workflow fields:

- source folder
- group
- routing mode
- watermark preset
- destination template
- cleanup policy
- conflict policy
- overwrite policy
- auto-upload setting
- required preflight checks

### 6. Operation History

Add a real history layer:

- member
- source file/folder
- output file/folder
- destination MEGA path
- job ID
- account used
- time started
- time completed
- success/failure
- file size
- checksum if available
- retries
- error details

This should answer:

```text
What did this member receive?
When?
Was it watermarked?
Which version?
Where is it in MEGA?
Did upload finish?
```

### 7. Crash Resume and Cleanup

Every long-running operation should save checkpoints.

Checkpoint examples:

- job created
- source scan complete
- member batch started
- watermark output created
- upload started
- upload complete
- local cleanup complete
- cloud cleanup needed
- job complete

After a crash or disk-full pause, the app should offer:

- resume
- inspect
- clean up
- abandon safely

This directly prevents clutter like empty folders created after a failed run.

### 8. Member Registry as a Real Admin System

The registry should become a managed data system.

Recommended upgrades:

- merge duplicate members
- compare two members
- member change history
- last distributed date
- last watermarked date
- last failure
- assigned groups count
- warning badges in the table
- import preview with dedupe
- CSV column mapping
- WordPress sync diff view
- safe delete with usage checks
- archive/deactivate instead of destructive delete

### 9. Consistent Command / Action System

Centralize common actions:

- rename
- duplicate
- delete
- audit
- preview
- run
- pause
- resume
- cancel
- export
- copy report
- open destination
- cleanup
- rebuild index

Each action should have:

- label
- icon
- tooltip
- enabled/disabled reason
- keyboard shortcut if useful
- confirmation policy
- logging policy

Disabled buttons should explain why.

Example:

```text
Select at least one completed watermarked file before sending to Distribution.
```

### 10. Professional Error Handling

Errors should be structured and actionable.

Every error should answer:

- What failed?
- Why did it fail?
- What was already completed?
- What is still pending?
- Is retry safe?
- Is cleanup needed?
- What should the user do next?

Good error dialog buttons:

- Retry
- Resume Later
- Clean Up
- Open Details
- Copy Report

### 11. Global App Search / Command Palette

Add a search box that can find:

- members
- groups
- jobs
- logs
- cloud paths
- local mappings
- presets
- settings
- commands/actions

Example search:

```text
john
```

Should show:

- John Smith member record
- groups containing John
- recent jobs for John
- distribution folder
- audit warnings
- related logs

### 12. Guided Setup Checklist

Add a first-run/readiness checklist:

- MEGA account connected
- storage/quota checked
- FFmpeg found
- Python found if needed
- temp folder writable
- member registry loaded
- at least one group exists
- distribution base path configured
- watermark preset created
- test upload completed

### 13. Template Manager

Make templates first-class:

- destination path templates
- watermark text templates
- folder naming templates
- course distribution templates
- group-specific defaults
- live preview with sample member
- validation warnings

Example:

```text
/NHB+ Courses/{member_name}/{course_name}
```

Preview:

```text
/NHB+ Courses/John Smith/OfferLab 2026
```

### 14. Normal Mode and Power Mode

Use progressive disclosure:

- default mode: common safe workflows
- advanced drawer: routing, conflict, overwrite, cleanup policies
- expert mode: raw paths, debug logs, matching thresholds

This keeps the app powerful without overwhelming normal use.

### 15. Deployment Identity

Show build identity in the app:

- version
- commit hash
- build date
- platform
- config mode: portable or standard

This prevents remote desktop confusion when checking whether the server pulled the latest version.

## Recommended Build Order

The best order is foundation first:

1. Stabilize logging backend and viewer.
2. Add job IDs and structured event taxonomy.
3. Add Job Center.
4. Add Universal Preview / Dry Run.
5. Add Health Center.
6. Add crash resume and cleanup.
7. Add operation history.
8. Centralize routing/matching.
9. Upgrade member registry into a managed admin system.
10. Add saved workflows.
11. Add command palette/global search.
12. Add support bundle export and deployment identity.

## Practical Milestones

### Milestone 1: Trustworthy Logs

Deliverables:

- durable activity logs
- clean log viewer
- issue filters
- selected-row details
- less noisy progress logging
- reliable clear/export

### Milestone 2: Job Foundation

Deliverables:

- shared job model
- job IDs in logs
- job states
- job summaries
- operation counts

### Milestone 3: Safer Execution

Deliverables:

- preview plans
- validation before execution
- blocked runs for unsafe inputs
- clearer conflict handling

### Milestone 4: Recovery

Deliverables:

- checkpoints
- resume
- cleanup tasks
- failed-job inspection

### Milestone 5: Professional Admin Layer

Deliverables:

- Health Center
- Registry merge/dedupe
- Operation History
- Member timeline
- Support bundle

## Execution Backlog

This backlog converts the master plan into implementation-ready work. Each task should stay small enough to build, test, and review independently.

Task fields:

- **ID**: stable identifier for discussion, commits, and tracking.
- **Priority**: `P0` blocks trust/safety, `P1` high-value foundation, `P2` polish or follow-up.
- **Depends on**: tasks that should land first.
- **Scope**: where the change should be made.
- **Acceptance**: concrete proof that the task is complete.

Implementation rule:

- Finish Milestone 1 before building the bigger Job Center.
- Do not add more panel-local logs while Milestone 1 is in progress.
- Do not introduce SQLite event storage until durable JSONL/file behavior is stable and the event model is agreed.
- Every task that changes runtime behavior should include at least one manual verification note in the final implementation summary.

### Milestone 1 Backlog: Trustworthy Logs

Goal: make the current logging system reliable, less noisy, and useful before redesigning the whole operations layer.

#### LOG-001: Verify Current Logging Behavior

Priority: P0

Depends on: none

Scope:

- `src/core/LogManager.cpp`
- `include/core/LogManager.h`
- `qt-gui/src/widgets/LogViewerPanel.cpp`
- `qt-gui/src/main.cpp`

Acceptance:

- Confirm whether activity logs survive app restart.
- Confirm whether `details` passed into `LogManager::log(..., details)` are stored, shown, and exported.
- Confirm whether `clearAll()` deletes/truncates persisted activity, error, and distribution logs.
- Confirm whether export uses only in-memory entries or persisted entries.
- Confirm whether GUI search covers message, action, details, member ID, job ID, and file path.
- Record findings in commit summary or a short implementation note.

Notes:

- This is a verification task, not a refactor.
- It should happen first because it defines the exact bug list.

#### LOG-002: Preserve Log Details Correctly

Priority: P0

Depends on: LOG-001

Scope:

- `LogManager::log`
- `LogManager::logWithContext`
- log serialization
- log viewer details display

Acceptance:

- Calling `LogManager::log(level, category, action, message, details)` preserves `details`.
- Details are visible in the selected-row details view or tooltip.
- Details are included in export.
- Existing callers keep compiling.

#### LOG-003: Store Activity Logs in Parseable Format

Priority: P0

Depends on: LOG-001, LOG-002

Scope:

- `LogEntry::toJson`
- `LogEntry::fromJson`
- `LogManager::writeToFile`
- existing activity log write path

Acceptance:

- New activity log entries are written as JSONL or another explicitly parseable format.
- Each line contains all structured fields available on `LogEntry`.
- Old plain-text logs do not crash the loader.
- Error logs still remain easy to inspect manually.

Recommendation:

- Prefer JSONL for activity logs.
- Keep human-readable export as a separate output format.

#### LOG-004: Load Recent Activity From Disk on Startup

Priority: P0

Depends on: LOG-003

Scope:

- `LogManager` constructor
- `loadRecentEntries`
- log directory handling

Acceptance:

- Restarting the app still shows recent activity entries in the Log Viewer.
- Loader reads recent activity files up to the configured cache limit.
- Loader handles missing, empty, corrupted, and old-format lines safely.
- Entries remain newest-first in viewer queries.

#### LOG-005: Make Clear Logs Actually Clear Persisted Logs

Priority: P0

Depends on: LOG-003, LOG-004

Scope:

- `LogManager::clearAll`
- Log Viewer clear button

Acceptance:

- Clear removes or truncates activity logs.
- Clear removes or truncates error logs.
- Clear removes or truncates distribution history.
- In-memory cache is cleared.
- Viewer refresh shows zero entries afterward.
- New logs continue writing correctly after clear.

Safety:

- Keep confirmation dialog.
- The dialog should state that persisted log files will be cleared.

#### LOG-006: Make Export Match What the Viewer Shows

Priority: P0

Depends on: LOG-004

Scope:

- `LogManager::exportLogs`
- `LogViewerPanel::onExportClicked`
- filter construction

Acceptance:

- Export respects level, category, search, and date filters.
- Export includes durable entries, not only current-process cache.
- Export supports at least human-readable text.
- If JSON is selected, export uses valid JSONL or JSON array consistently.
- Export failure gives a useful error message.

#### LOG-007: Improve Search Coverage and Matching

Priority: P1

Depends on: LOG-004

Scope:

- `LogManager::getEntries`
- `LogManager::search`
- `LogViewerPanel`

Acceptance:

- Search is case-insensitive.
- Search covers action, message, details, member ID, file path, and job ID.
- Searching by member ID finds member-related logs.
- Searching by file name finds logs where full path contains that file.
- Search with empty text behaves as no search filter.

#### LOG-008: Add Selected Log Details Pane

Priority: P1

Depends on: LOG-002

Scope:

- `LogViewerPanel`

Acceptance:

- Selecting an activity row shows full details without relying on truncated table cells.
- Details pane includes timestamp, level, category, action, message, details, member ID, file path, and job ID when available.
- Details pane supports copy.
- Empty selection shows a useful neutral state.

#### LOG-009: Reduce Progress Spam in Normal Activity Logs

Priority: P0

Depends on: LOG-001

Scope:

- downloader progress logging
- watermark progress logging
- distribution progress logging
- Qt message handler filtering

Acceptance:

- Per-line downloader/progress output no longer floods normal activity logs.
- High-frequency progress either updates job state, uses debug level, or is rate-limited.
- Errors and completion events remain visible.
- Debug logs can still be enabled for diagnosis.

Rule:

- Normal activity logs should capture state transitions, not every progress tick.

#### LOG-010: Add Issue-Focused Filters

Priority: P1

Depends on: LOG-007

Scope:

- `LogViewerPanel`

Acceptance:

- Add quick filters for Errors, Warnings, Today, and Needs Attention.
- Existing level/category filters still work.
- Filter labels are clear and have tooltips.
- Count label reflects current filtered results.

#### LOG-011: Separate Debug/System Noise From Operational Events

Priority: P1

Depends on: LOG-009

Scope:

- Qt message handler
- `LogManager`
- `LogViewerPanel`

Acceptance:

- Raw Qt/system logs can be viewed when needed.
- Normal operational activity is not dominated by Qt debug messages.
- Warnings/errors from Qt still appear in Issues unless explicitly suppressed.
- Debug view is clearly labeled as diagnostic.

#### LOG-012: Add Log Row Copy and Report Copy Actions

Priority: P1

Depends on: LOG-008

Scope:

- `LogViewerPanel`
- existing `CopyHelper`

Acceptance:

- User can copy selected row as readable text.
- User can copy selected row as structured JSON/text if available.
- User can copy current filtered report.
- Empty selection disables row copy or explains why.

#### LOG-013: Improve Distribution History Reliability

Priority: P1

Depends on: LOG-001

Scope:

- `DistributionRecord`
- `LogManager::recordDistribution`
- `LogManager::updateDistributionStatus`
- `LogViewerPanel` distribution tab

Acceptance:

- Updating a distribution record does not create confusing duplicate history rows.
- Distribution rows show job ID when available.
- Failed records show full error detail.
- Filtering by member and status works after restart.

#### LOG-014: Add Retention Controls

Priority: P2

Depends on: LOG-005

Scope:

- `LogManager::cleanOldLogs`
- settings integration
- optional Log Viewer maintenance UI

Acceptance:

- Retention days can be configured.
- Old activity logs are cleaned safely.
- Distribution history retention is either documented or configurable.
- Cleanup never removes current-day active log unexpectedly.

#### LOG-015: Add Support Bundle Export, Basic Version

Priority: P1

Depends on: LOG-006, LOG-008

Scope:

- `LogViewerPanel`
- `LogManager`
- settings summary helper if needed

Acceptance:

- User can export a support bundle folder or zip.
- Bundle includes filtered logs, recent errors, app version, platform, log directory, and dependency status if available.
- Bundle excludes secrets, sessions, passwords, and tokens.
- Bundle export result shows path and any skipped sections.

#### LOG-016: Add Logging Developer Guidelines

Priority: P1

Depends on: LOG-009

Scope:

- `docs/DEVELOPER_GUIDE.md` or a new logging section in this document

Acceptance:

- Document when to use activity logs vs debug logs.
- Document required fields for member/distribution/watermark events.
- Document naming style for event actions.
- Document examples of good and bad log messages.

#### LOG-017: Add Manual Test Checklist for Logs

Priority: P1

Depends on: LOG-001 through LOG-013

Scope:

- `docs/MASTER_IMPROVEMENT_PLAN.md` or a dedicated QA checklist

Acceptance:

- Checklist covers startup, log creation, restart, search, filters, export, clear, and support bundle.
- Checklist covers downloader, watermark, distribution, and member events.
- Checklist includes expected result for each step.

#### LOG-018: Decide Whether to Move to SQLite

Priority: P2

Depends on: LOG-001 through LOG-017

Scope:

- architecture decision note

Acceptance:

- Decision recorded: stay with JSONL for now or migrate to SQLite.
- Decision includes tradeoffs for query speed, durability, corruption recovery, portability, and implementation cost.
- If SQLite is chosen, create a separate milestone instead of mixing it into Milestone 1.

### Milestone 2 Backlog: Job Foundation

Goal: create a shared job model so logs, progress, recovery, and history all refer to the same operation.

#### JOB-001: Define Job Model

Priority: P0

Depends on: LOG-002, LOG-003

Scope:

- new lightweight job model header/source or existing utils layer

Acceptance:

- Job has ID, type, status, created/started/finished timestamps, counts, source roots, destination roots, and last error.
- Job status supports queued, running, paused, completed, failed, cancelled, and cleanup required.
- Job can serialize to durable storage.

#### JOB-002: Add Job IDs to Watermarking

Priority: P1

Depends on: JOB-001

Scope:

- `WatermarkPanel`
- `WatermarkerController`
- `Watermarker`
- related logs

Acceptance:

- Each watermark run has a job ID.
- All watermark start/complete/fail events include job ID.
- Per-member batches can be tied to the parent job.

#### JOB-003: Add Job IDs to Distribution

Priority: P1

Depends on: JOB-001

Scope:

- `DistributionPanel`
- `DistributionController`
- `DistributionPipeline`
- distribution history

Acceptance:

- Each distribution run has a job ID.
- Folder creation, upload, skip, fail, pause, and completion events include job ID.
- Distribution history can filter by job ID.

#### JOB-004: Add Job IDs to Downloader and Auto-Send

Priority: P1

Depends on: JOB-001

Scope:

- `DownloaderPanel`
- downloader worker
- auto-send to watermark bridge

Acceptance:

- Download batch has a job ID.
- Auto-send carries source job ID into watermark handoff or records a parent/child relationship.
- Failed downloads are grouped by job.

#### JOB-005: Add Job Summary View

Priority: P1

Depends on: JOB-002, JOB-003, JOB-004

Scope:

- `LogViewerPanel` first, full Job Center later

Acceptance:

- User can see recent jobs with status and counts.
- Selecting a job filters related log events.
- Failed jobs show last error and affected members/files.

### Milestone 3 Backlog: Safer Execution

Goal: show users exactly what will happen before running high-risk operations.

#### PLAN-001: Define Plan Model

Priority: P0

Depends on: JOB-001

Scope:

- shared planning model

Acceptance:

- Plan supports actions, warnings, blockers, affected members, source paths, destination paths, and estimated counts.
- Plan can be rendered in a table/dialog.
- Plan can be exported.

#### PLAN-002: Distribution Dry Run

Priority: P1

Depends on: PLAN-001, JOB-003

Scope:

- `DistributionPanel`
- routing/matching logic

Acceptance:

- User can preview matched members, cloud folders to create, upload destinations, skipped items, and blockers.
- Run button is blocked when plan has P0 blockers.
- User can copy/export the plan.

#### PLAN-003: Watermark Dry Run

Priority: P1

Depends on: PLAN-001, JOB-002

Scope:

- `WatermarkPanel`

Acceptance:

- User can preview selected members, selected files, expected output paths, estimated disk needs, and missing member data.
- Low disk space is a blocker or explicit pause condition.

#### PLAN-004: Bulk Rename Dry Run Standardization

Priority: P2

Depends on: PLAN-001

Scope:

- cloud drive bulk rename
- search bulk rename

Acceptance:

- Rename preview uses the shared plan language.
- Conflicts and unchanged names are clearly separated.

### Milestone 4 Backlog: Recovery and Cleanup

Goal: failed or paused operations should leave clear recovery paths instead of clutter.

#### REC-001: Define Checkpoint Model

Priority: P0

Depends on: JOB-001

Scope:

- shared checkpoint storage

Acceptance:

- Checkpoints can record planned, started, completed, failed, skipped, and cleanup-needed states.
- Checkpoints survive restart.
- Checkpoints are tied to job IDs.

#### REC-002: Disk-Full Pause and Resume State

Priority: P0

Depends on: REC-001, JOB-002, JOB-003

Scope:

- watermarking
- distribution
- auto-upload pipeline

Acceptance:

- Disk-full condition pauses the job.
- App does not continue creating empty folders after a hard storage blocker.
- Resume knows which member/file should continue next.
- Cleanup-needed state is visible.

#### REC-003: Cleanup Tasks

Priority: P1

Depends on: REC-001

Scope:

- temp workspace
- empty cloud folder cleanup candidates

Acceptance:

- Failed jobs can list cleanup tasks.
- User can inspect before cleanup.
- Cleanup logs exactly what was removed or skipped.

### Milestone 5 Backlog: Professional Admin Layer

Goal: give the app stronger operational admin tools after the foundation is trustworthy.

#### ADMIN-001: Health Center Shell

Priority: P1

Depends on: LOG-010, JOB-005

Scope:

- new panel or upgraded Log Viewer/Settings area

Acceptance:

- Shows health findings grouped by severity.
- Includes member audit results.
- Includes dependency, disk, temp folder, and quota checks where available.

#### ADMIN-002: Member Merge Tool

Priority: P1

Depends on: existing member audit and duplicate member feature

Scope:

- `MemberRegistry`
- `MemberRegistryPanel`

Acceptance:

- User can compare two members.
- User can choose winning fields.
- Group membership and history references are preserved where possible.
- Merge is logged with before/after summary.

#### ADMIN-003: Member Timeline

Priority: P1

Depends on: LOG-007, JOB-002, JOB-003

Scope:

- member registry panel
- log/history viewer

Acceptance:

- User can open a member and see recent jobs, distributions, failures, and registry changes.
- Timeline can filter by event type.

#### ADMIN-004: Operation History Panel

Priority: P1

Depends on: JOB-005, LOG-013

Scope:

- Log Viewer or new History panel

Acceptance:

- User can answer who got what, when, where, and whether it completed.
- History can filter by member, job, date, status, and destination.

### Implementation Slices

Use these slices to keep work reviewable.

#### Slice A: Logging Foundation

Recommended tasks:

- LOG-001
- LOG-002
- LOG-003
- LOG-004
- LOG-005
- LOG-006

Expected result:

- Logs are durable, clearable, exportable, and visible after restart.

#### Slice B: Logging Usability

Recommended tasks:

- LOG-007
- LOG-008
- LOG-009
- LOG-010
- LOG-011
- LOG-012

Expected result:

- Log Viewer becomes useful for diagnosis instead of just showing clutter.

#### Slice C: Distribution and Support Reliability

Recommended tasks:

- LOG-013
- LOG-015
- LOG-016
- LOG-017

Expected result:

- Distribution history is more trustworthy and support exports become possible.

#### Slice D: Job Foundation

Recommended tasks:

- JOB-001
- JOB-002
- JOB-003
- JOB-004
- JOB-005

Expected result:

- Major operations share job identity and can be grouped.

#### Slice E: Plan and Recovery

Recommended tasks:

- PLAN-001
- PLAN-002
- PLAN-003
- REC-001
- REC-002
- REC-003

Expected result:

- High-risk work is previewable, stoppable, resumable, and cleanable.

## Acceptance Standard

For future implementation work from this document, a task is not done unless:

- the code builds locally, unless the user explicitly pauses the build;
- the behavior is manually verified or the reason it could not be verified is stated;
- new UI controls have clear labels and tooltips;
- disabled actions explain why they are disabled;
- errors include the next safe action when possible;
- logs include enough context to diagnose the action later;
- unrelated files and user-local data are not touched.

## First Implementation Recommendation

Start with **Slice A: Logging Foundation**.

Why:

- It fixes the most obvious reliability bugs.
- It makes future work easier to debug.
- It creates a trustworthy base before larger job/recovery systems.
- It avoids building a Job Center on top of logs that cannot yet be trusted.

Suggested first commit sequence:

1. `LOG-001`: verify and document current behavior.
2. `LOG-002`: preserve details.
3. `LOG-003`: write parseable activity logs.
4. `LOG-004`: load recent activity from disk.
5. `LOG-005`: clear persisted logs safely.
6. `LOG-006`: export durable filtered logs.

## Implementation Notes

### 2026-06-14 Logging Foundation Pass

Scope:

- Started with Slice A and the highest-value part of Slice B.

Implemented:

- `LOG-002`: preserve log `details` through `LogManager::log` and contextual entries.
- `LOG-003`: write new activity log entries as parseable JSONL-style lines.
- `LOG-004`: load recent activity entries from persisted activity log files during startup and log-directory changes.
- `LOG-005`: clear persisted activity logs, error logs, and distribution history from `clearAll()`.
- `LOG-006`: export filtered durable activity entries as readable text or JSONL based on extension.
- Partial `LOG-007`: activity search is case-insensitive and covers message, action, details, member ID, file path, job ID, level, and category.
- Partial `LOG-008`: Log Viewer now has a selected-entry details pane.
- Partial `LOG-009`: downloader worker progress no longer floods normal activity logs; routine progress is debug-level while warnings/errors remain visible.

Verification required:

- Build Qt GUI.
- Confirm a new activity entry appears in the Log Viewer after restart.
- Confirm details from Qt warnings/errors are visible in the details pane and export.
- Confirm `Clear Logs` removes persisted files and the viewer refreshes empty.
- Confirm `.json` / `.jsonl` exports contain JSONL and `.txt` export contains readable text.
- Confirm downloader progress no longer floods normal activity logs during a real download.

### 2026-06-14 Log Viewer Usability Pass

Scope:

- Continued Slice B without introducing the full Job Center yet.

Implemented:

- `LOG-010`: added quick filters for All Activity, Needs Attention, Errors Only, Today, and Debug/System.
- `LOG-012`: added Copy Details and Copy Report actions for visible activity/distribution data.
- Continued `LOG-008`: selected distribution rows now expose full structured details, not only status tooltips.
- Continued `LOG-013`: distribution history now displays job IDs directly and includes job/member/source/destination/timing/error context in copied details.

Verification required:

- Build Qt GUI.
- Confirm quick filters combine correctly with search/category/date filters.
- Confirm Copy Details works for both Activity and Distribution tabs.
- Confirm Copy Report copies visible rows with headers.
- Confirm distribution history job IDs display without crowding the table.

### 2026-06-14 Job Foundation Pass

Scope:

- Started Slice D with a shared job model and first-pass wiring for the three most important long-running workflows.

Implemented:

- `JOB-001`: added `OperationJobStore`, a lightweight durable job store for recent jobs.
- Jobs now have ID, type, status, title, summary, error, created/started/updated/finished timestamps, planned/completed/failed/skipped counts, and metadata.
- Job status supports queued, running, paused, completed, failed, cancelled, and cleanup required.
- Job records serialize to `operation_jobs.json` in the app config directory.
- Job progress persistence is throttled so frequent UI progress updates do not spam disk writes.
- Job lifecycle events are logged through `LogManager` with stable `job.*` actions and job IDs.
- `JOB-002`: `WatermarkPanel` creates a job per watermark run, tracks progress/failures, and maps disk-full stops to paused jobs.
- `JOB-003`: `DistributionPanel` creates jobs for local/cloud copy runs and adopts existing `DistributionController` job IDs for controller-driven uploads.
- `JOB-004`: `DownloaderPanel` creates jobs for download batches, tracks progress/failures/cancellation, and logs auto-send-to-watermark under the source download job.

Verification completed:

- Qt GUI build passed with `cmake --build qt-gui/build-qt --parallel 2`.

Still pending:

- `JOB-002`: wire deeper `WatermarkerController` / engine-level events to the same parent job.
- `JOB-003`: expose job filters in distribution history and ensure every lower-level folder/upload skip event carries the job ID.
- `JOB-004`: carry parent/child job relationships from downloader auto-send into the created watermark job.
- Expand the first job summary into the full Job Center with retry, cleanup, and resume controls.

### 2026-06-15 Job Summary View Pass

Scope:

- Continued Slice D by exposing the shared job model inside the Log Viewer.

Implemented:

- `JOB-005`: added a Jobs tab to `LogViewerPanel`.
- Recent jobs now show updated time, status, type, title, progress counts, summary, last error, and job ID.
- Jobs can be filtered by operation type and lifecycle status.
- Selecting a job writes its job ID into the Activity search, resets conflicting activity filters, and refreshes related log events.
- Failed and paused jobs expose last error/status context in the table and copyable details pane.
- Copy Details and Copy Report now work for Jobs, Activity, and Distribution tabs.
- Clear Logs now also clears persisted job summaries to avoid orphaned job rows after log cleanup.

Verification completed:

- Qt GUI build passed with `cmake --build qt-gui/build-qt --parallel 2`.

Still pending:

- Add a richer Job Center with retry failed, cleanup required, and resume actions.
- Persist parent/child job relationships for downloader-to-watermark handoffs.

### 2026-06-15 Job Actions Pass

Scope:

- Continued the Job Center path by making selected jobs actionable, not only readable.

Implemented:

- Added selected-job actions to the Jobs tab: Copy Job ID, Show Activity, and Open Panel.
- `Open Panel` routes download jobs to Downloader, watermark jobs to Watermark, and distribution jobs to Distribution through the existing sidebar/content stack.
- `Show Activity` switches to Activity and keeps the selected job ID as the activity search.
- Distribution History now has a job ID filter.
- Selecting a distribution job fills the Distribution History job filter automatically.

Verification completed:

- Qt GUI build passed with `cmake --build qt-gui/build-qt --parallel 2`.

Still pending:

- Add retry failed, cleanup required, and resume controls once operation-specific resume contracts exist.
- Persist parent/child job relationships for downloader-to-watermark handoffs.
- Add open related member/destination/file actions for rows that carry those fields.

### 2026-06-15 Download Retry Contract Pass

Scope:

- Added the first safe retry contract instead of exposing generic retry buttons that cannot rebuild a real run.

Implemented:

- New download jobs persist original URL list and downloader settings in job metadata.
- Retried download jobs record `retryOfJobId` in the new job metadata.
- Downloader can rebuild its queue/settings from a selected download job and start the retry.
- Jobs tab now has a Retry action.
- Retry is enabled only for completed, failed, or cancelled download jobs; active jobs and unsupported job types remain disabled.
- MainWindow routes retry requests from the Log Viewer to the Downloader panel.

Verification completed:

- Qt GUI build passed with `cmake --build qt-gui/build-qt --parallel 2`.

Still pending:

- Add distribution retry only after distribution jobs persist selected tasks, source/destination plan, conflict policy, and move/copy mode.
- Add cleanup actions for disk-full paused watermark jobs and partial distribution folders.

### 2026-06-15 Watermark Retry Contract Pass

Scope:

- Added a safe watermark retry contract by persisting the watermark run plan instead of trying to infer it from table rows after the fact.

Implemented:

- New watermark jobs persist original file paths, selected member IDs, mode, watermark text/templates, output strategy, source root, encoding settings, metadata fields, auto-upload setting, and custom upload path.
- Retried watermark jobs record `retryOfJobId` in the new job metadata.
- Watermark panel can rebuild its queue/settings/member selection from a selected watermark job and start the retry through the normal validation path.
- Jobs tab Retry is now enabled for terminal download and watermark jobs.
- MainWindow routes retry requests to Downloader or Watermark based on the selected job type.

Verification completed:

- Qt GUI build passed with `cmake --build qt-gui/build-qt --parallel 2`.

Still pending:

- Add cleanup actions for disk-full paused watermark jobs and partial distribution folders.
- Add parent/child job display for download-to-watermark retries and handoffs.

### 2026-06-16 Distribution Retry Contract Pass

Scope:

- Completed the third safe retry contract by persisting exact distribution run plans before exposing Distribution retry through the Jobs tab.

Implemented:

- Normal cloud/local distribution jobs now persist exact source-to-destination tasks, member IDs, move/copy mode, conflict handling, destination creation setting, source type, source path, month, group, broadcast state, Smart Route state, and watermark-suffix cleanup setting.
- Direct watermark-to-distribution upload jobs now persist the member-to-local-file upload map before the controller handoff starts.
- Retried distribution jobs record `retryOfJobId` in the new job metadata.
- Distribution panel can retry saved direct-upload jobs and saved folder copy/move task jobs from the Jobs tab.
- Retry skips missing local files/tasks with a warning instead of launching broken empty work.
- MainWindow routes Distribution retry requests from Log Viewer to the Distribution panel.
- Jobs tab Retry is now enabled for terminal download, watermark, and distribution jobs with saved metadata.

Verification completed:

- Qt GUI build passed with `cmake --build qt-gui/build-qt --parallel 2`.

Still pending:

- Add cleanup actions for disk-full paused watermark jobs and partial distribution folders.
- Add parent/child job display for download-to-watermark retries and handoffs.

### 2026-06-16 Watermark Disk-Full Resume Pass

Scope:

- Fixed the unsafe runtime resume path after a disk-full watermark pause. This is separate from Jobs-tab retry: resume continues the paused table state, while retry rebuilds a new run from saved metadata.

Implemented:

- A disk-full pause now preserves the paused watermark job ID for the next resume attempt.
- The Start button changes to `Resume Watermarking` while a disk-full pause is active.
- Resume builds an explicit row checkpoint plan from the current table instead of rebuilding all selected files and members from scratch.
- Completed/uploaded rows are skipped during resume.
- Existing completed local outputs are carried into the member auto-upload batch so partial-member resumes do not strand already-created files.
- Pending/error rows are retried only if their source file still exists.
- Partial failed output files are removed before retrying that row, reducing numbered duplicate outputs caused by existing partial files.
- Mutable controls are locked while a disk-full pause is active, so source files, members, templates, output paths, and auto-upload settings cannot drift before resume.
- Final job completion counts are derived from the table state, not only the rows processed after clicking Resume.

Verification completed:

- Qt GUI build passed with `cmake --build qt-gui/build-qt --parallel 2`.

Still pending:

- Add explicit cleanup actions for cloud folders that already existed before this pass.
- Add cleanup actions for partial distribution folders.
- Add parent/child job display for download-to-watermark retries and handoffs.

### 2026-06-16 Watermark Local Cleanup Pass

Scope:

- Added the first preview-first cleanup action for Watermark jobs. This cleanup is intentionally local-only and conservative: it deletes only files/folders the loaded Watermark table can prove are partial artifacts from the selected job.

Implemented:

- Jobs tab now includes a `Cleanup` action.
- Cleanup is enabled for paused, failed, or cleanup-required Watermark jobs.
- MainWindow routes cleanup requests to the Watermark panel.
- Watermark cleanup requires live loaded table state for the selected job; older jobs after restart show a clear "not enough cleanup metadata" warning instead of risky deletes.
- Cleanup previews partial local output files and empty local member/output folders before deleting anything.
- Completed/uploaded outputs are kept.
- MEGA cloud folders are not deleted in this pass.
- Cleanup removes only local partial files from unfinished/error rows and empty folders that contain no non-cleanup files.
- Cleanup writes a compact `cleanupRuns` history into job metadata.
- Cleanup logs `cleanup.started`, `cleanup.completed`, or `cleanup.failed` with the job ID.
- Failed cleanup marks the job as `cleanup_required`; successful cleanup on paused jobs keeps the job paused so the user can resume.

Verification completed:

- Qt GUI build passed with `cmake --build qt-gui/build-qt --parallel 2`.

Still pending:

- Persist per-row cleanup checkpoints so cleanup can work safely after app restart.
- Add explicit cleanup actions for cloud folders that already existed before this pass.
- Add cleanup actions for partial distribution folders.
- Add parent/child job display for download-to-watermark retries and handoffs.

## Non-Goals

Avoid these until the foundation is fixed:

- More unrelated panels.
- More one-off buttons that bypass shared job/history/log systems.
- More panel-local progress systems.
- More raw `qDebug()` output as a substitute for operational events.
- Complex automation before preview/recovery exists.

## Principle

The app should always be able to explain itself:

```text
What am I about to do?
What am I doing now?
What did I finish?
What failed?
What is safe to retry?
What needs cleanup?
Where is the evidence?
```

If a feature cannot answer those questions, it should not be considered production-grade yet.
