#include "client/output/output_renderer.h"
#include "client/output/null_renderer.h"
#include "client/output/console_renderer.h"

// Graphics renderer is optional
#ifdef EQT_HAS_GRAPHICS
// Note: IrrlichtRenderer adapter would be included here
// For now, we only have the base classes
#endif

namespace eqt {
namespace output {

std::unique_ptr<IOutputRenderer> createRenderer(RendererType type) {
    switch (type) {
        case RendererType::Null:
            return std::make_unique<NullRenderer>();

        case RendererType::Console:
            return std::make_unique<ConsoleRenderer>();

        case RendererType::IrrlichtSoftware:
        case RendererType::IrrlichtGPU:
#ifdef EQT_HAS_GRAPHICS
            // Note: When IrrlichtRenderer is adapted to implement IOutputRenderer,
            // create and return it here. For now, fall through to console.
            // return std::make_unique<IrrlichtRendererAdapter>(type == RendererType::IrrlichtGPU);
#endif
            // Fall back to console if graphics not available
            return std::make_unique<ConsoleRenderer>();

        case RendererType::ASCII:
        case RendererType::MUD:
        case RendererType::TopDown:
            // Future renderer types - fall back to console for now
            return std::make_unique<ConsoleRenderer>();

        default:
            return std::make_unique<NullRenderer>();
    }
}

} // namespace output
} // namespace eqt
