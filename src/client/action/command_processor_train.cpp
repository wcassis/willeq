// T280 -- CommandProcessor::cmdTrain / cmdTrainClose (T264 F8).
//
// Split out of command_processor.cpp for the same reason cmdBank was (T269):
// that TU sits at check_file_sizes.sh's 3000-line FAIL ceiling.
//
// /train has two jobs and they are told apart by the ARGUMENT, not by two
// commands, because that is how the player thinks about it:
//
//   /train              -> open the trainer window with the current target
//                          (the pre-T280 behavior, unchanged: T50's
//                          target/npc_type/class/distance pre-validation
//                          lives in EverQuest::RequestTraining()).
//   /train <skill|id>   -> train that skill at the ALREADY-OPEN session.
//   /train list         -> what the open session offers, with prices.
//   /trainclose         -> end the session (OP_GMEndTraining).
//
// No parsing here beyond "was there an argument". The skill vocabulary and
// every refusal that needs the session's caps belong to EverQuest, which is
// where the caps the server sent actually live -- CommandProcessor's
// GameState has no skill-cap list to check against, and inventing one here
// would be the second, weaker copy T277 warned about.

#include "client/action/command_processor.h"

#include <string>

namespace eqt {
namespace action {

ActionResult CommandProcessor::cmdTrain(const std::string& args) {
    if (args.empty()) {
        return m_dispatcher.trainWithTarget();
    }
    return m_dispatcher.trainSkill(args);
}

ActionResult CommandProcessor::cmdTrainClose(const std::string& args) {
    (void)args;
    return m_dispatcher.closeTrainer();
}

}  // namespace action
}  // namespace eqt
