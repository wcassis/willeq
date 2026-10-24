/*
 * T251: the one place a local cast refusal becomes a chat line. See
 * include/client/spell/cast_refusal.h and refactor/T251_missing_component_
 * message.md.
 */

#include "client/spell/cast_refusal.h"
#include "client/client_context.h"

namespace EQ {

std::string castRefusalMessage(eqt::ClientContext& ctx, CastResult result)
{
    const uint32_t string_id = castRefusalStringId(result);
    if (string_id == 0) {
        return "";
    }
    return ctx.strings.GetStringMessage(string_id);
}

} // namespace EQ
