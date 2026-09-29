#pragma once

// B918 (issue #237): the process-wide record that the run is ending because
// of a FATAL, so main() can return a non-zero exit code for it.
//
// Most FATALs in the client already end the process non-zero (they
// std::exit(1)). Two families did not: they log FATAL and then take the
// ORDINARY quit path, which main() used to turn into 0 -- indistinguishable
// from a clean /q:
//   - the zone loading thread failing (Application::checkLoadingComplete(),
//     recorded through eqt::ZoneLoadHandoff), and
//   - ZoneComponent::LoadZoneSources() (an unopenable/unparsable zone archive
//     on the game thread, e.g. a zone the client has no .s3d for).
// A FATAL-then-quit site calls RecordFatalExit() before it requests the quit;
// main() reads FatalExitRecorded() through Application::fatalShutdown() and
// eqt::scenario::ProcessExitCode() maps it to kClientFatalExit (4).

#include <string>

namespace eqt {

/** Record that the run is ending because of a FATAL. The first reason wins. */
void RecordFatalExit(const std::string& reason);

/** True once RecordFatalExit() has been called in this process. */
bool FatalExitRecorded();

/** The first recorded reason ("" when none). */
std::string FatalExitReason();

/** Tests only: forget a recorded FATAL. */
void ResetFatalExitForTest();

}  // namespace eqt
