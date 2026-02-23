# Debug Logging Standards

This document defines the debug logging standards for WillEQ. All new code and refactored code MUST follow these standards.

**Implementation**: `include/common/logging.h`

## Overview

The logging system provides:
- Multiple severity levels (NONE through TRACE)
- Module-specific filtering (whitelist, blacklist, per-module overrides)
- Runtime level configuration (startup, config file, signals, slash commands)
- Consistent output formatting to stdout/stderr
- Compile-time stripping of DEBUG/TRACE in release builds

## Log Levels

Levels are hierarchical. Setting a level enables that level and all levels above it (lower numeric value).

| Level | Value | Purpose |
|-------|-------|---------|
| `NONE` | 0 | Errors only - minimal output for production use |
| `FATAL` | 1 | Unrecoverable errors causing immediate termination |
| `ERROR` | 2 | Errors that prevent normal operation but allow continued execution |
| `WARN` | 3 | Unexpected conditions that may indicate problems |
| `INFO` | 4 | Significant operational events (startup, shutdown, connections) |
| `DEBUG` | 5 | Detailed information useful for debugging |
| `TRACE` | 6 | Very detailed execution flow (function entry/exit, loop iterations) |

### Default Behavior (NONE / Level 0)

When no log level is specified or level is set to `NONE`, the application runs in **quiet mode**:
- FATAL and ERROR messages are always output (to stderr)
- No INFO, DEBUG, or TRACE output
- Minimal stdout output - only critical operational messages
- This is the production default for clean but safe operation

Errors should never be silenced - they indicate problems that need attention.

### Level Selection Guidelines

- **FATAL**: Application cannot continue. Examples: failed to initialize critical subsystem, out of memory
- **ERROR**: Operation failed but application continues. Examples: failed to load a texture, packet parse error, connection dropped
- **WARN**: Something unexpected but handled. Examples: missing optional file, deprecated feature used, retry succeeded after failure
- **INFO**: Normal but significant events. Examples: connected to server, entered zone, loaded model, configuration applied
- **DEBUG**: Development-useful details. Examples: packet contents, state transitions, cache hits/misses
- **TRACE**: Granular execution flow. Examples: function entry/exit, individual loop iterations, every packet received

## Modules

Each logical component has a module identifier. Modules can be enabled/disabled independently.

### Defined Modules

| Module | Enum | Description |
|--------|------|-------------|
| `NET` | `MOD_NET` | Network layer (connections, packet send/receive) |
| `NET_PACKET` | `MOD_NET_PACKET` | Packet parsing and serialization |
| `LOGIN` | `MOD_LOGIN` | Login server communication |
| `WORLD` | `MOD_WORLD` | World server communication |
| `ZONE` | `MOD_ZONE` | Zone server communication |
| `ENTITY` | `MOD_ENTITY` | Entity tracking and updates |
| `MOVEMENT` | `MOD_MOVEMENT` | Player and entity movement |
| `COMBAT` | `MOD_COMBAT` | Combat system |
| `SPELL` | `MOD_SPELL` | Spell casting system |
| `INVENTORY` | `MOD_INVENTORY` | Inventory management |
| `GRAPHICS` | `MOD_GRAPHICS` | Rendering system |
| `GRAPHICS_LOAD` | `MOD_GRAPHICS_LOAD` | Asset loading (models, textures, zones) |
| `CAMERA` | `MOD_CAMERA` | Camera control |
| `INPUT` | `MOD_INPUT` | User input handling |
| `AUDIO` | `MOD_AUDIO` | Sound system |
| `PATHFIND` | `MOD_PATHFIND` | Pathfinding and navigation |
| `MAP` | `MOD_MAP` | Zone map and collision |
| `UI` | `MOD_UI` | User interface |
| `CONFIG` | `MOD_CONFIG` | Configuration loading/saving |
| `MAIN` | `MOD_MAIN` | Main application logic |

New modules may be added as needed. Module names should be short, uppercase, and descriptive.

## Output Format

All log output follows this format:

```
[TIMESTAMP] [LEVEL] [MODULE] message
```

### Format Specification

- **TIMESTAMP**: ISO 8601 with milliseconds: `YYYY-MM-DD HH:MM:SS.mmm`
- **LEVEL**: 5-character padded level name: `FATAL`, `ERROR`, `WARN `, `INFO `, `DEBUG`, `TRACE`
- **MODULE**: Module name in brackets, variable width
- **message**: The log message, may span multiple lines for complex data

### Examples

```
[2026-02-23 14:32:01.234] [INFO ] [NET] Connected to zone server 192.168.1.100:7000
[2026-02-23 14:32:01.456] [DEBUG] [ENTITY] Spawned entity id=1234 name="a_guard" race=1 class=3
[2026-02-23 14:32:01.789] [TRACE] [NET_PACKET] Received OP_SpawnAppearance size=12 data=0x00 0x01 0x02...
[2026-02-23 14:32:02.001] [ERROR] [GRAPHICS_LOAD] Failed to load texture "missing.dds": file not found
```

### Output Destinations

- `FATAL`, `ERROR`, `WARN`: stderr
- `INFO`, `DEBUG`, `TRACE`: stdout

## Runtime Configuration

### Command-Line Arguments

```bash
# Set global log level
./willeq -c willeq.json --log-level=DEBUG

# Enable specific modules at specific levels
./willeq -c willeq.json --log-level=INFO --log-module=NET:DEBUG --log-module=ENTITY:TRACE

# Whitelist: only show specific modules (all others suppressed)
./willeq -c willeq.json --log-level=DEBUG --log-only=NET,ENTITY,ZONE

# Blacklist: suppress specific modules
./willeq -c willeq.json --log-level=DEBUG --log-exclude=NET_PACKET,GRAPHICS

# Per-module overrides take precedence over whitelist/blacklist
./willeq -c willeq.json --log-level=DEBUG --log-only=NET --log-module=ENTITY:TRACE
```

### Configuration File (willeq.json)

```json
{
    "logging": {
        "level": "INFO",
        "only": ["NET", "ENTITY", "ZONE"],
        "exclude": ["NET_PACKET"],
        "modules": {
            "NET": "DEBUG",
            "ENTITY": "TRACE"
        }
    }
}
```

Priority: `--log-module` per-module overrides > `--log-only`/`--log-exclude` > global level.

If both `only` and `exclude` are specified, `only` takes precedence.

### Live Modification

The logging system supports runtime level changes without restart:

1. **Signal handlers**: `SIGUSR1` increases global level, `SIGUSR2` decreases
2. **Slash command**: `/debug <level>` sets the global log level (0-6)
3. **API function**: `LogManager::Instance().SetGlobalLevel(level)` callable from debugger or code

### Environment Variables

Environment variables override config file but are overridden by command-line:

```bash
EQT_LOG_LEVEL=DEBUG ./willeq -c willeq.json
EQT_LOG_MODULES="NET:TRACE,ENTITY:DEBUG" ./willeq -c willeq.json
```

## Implementation API

### Logging Macros

Use macros for all logging. In debug builds (`EQT_DEBUG`), all levels are available. In release builds, only FATAL and ERROR are compiled in; WARN through TRACE become no-ops.

```cpp
// Standard logging macros - use these for all new code
LOG_FATAL(module, format, ...)
LOG_ERROR(module, format, ...)
LOG_WARN(module, format, ...)
LOG_INFO(module, format, ...)
LOG_DEBUG(module, format, ...)
LOG_TRACE(module, format, ...)

// Conditional logging - only evaluate arguments if condition AND level enabled
LOG_DEBUG_IF(module, condition, format, ...)

// Lazy evaluation for expensive computations
LOG_DEBUG_LAZY(module, msg, lambda)
```

The `module` argument uses the `MOD_*` enum values (e.g., `MOD_NET`, `MOD_ENTITY`).

### Format Strings

Use `fmt` library format syntax (project dependency):

```cpp
LOG_DEBUG(MOD_NET, "Received packet opcode={:#06x} size={}", opcode, size);
LOG_INFO(MOD_ENTITY, "Entity {} spawned at ({:.2f}, {:.2f}, {:.2f})", name, x, y, z);
LOG_ERROR(MOD_GRAPHICS_LOAD, "Failed to load texture \"{}\": {}", filename, error);
```

### Expensive Computations

Wrap expensive-to-compute log data in conditionals:

```cpp
// BAD - toString() called even if DEBUG is disabled
LOG_DEBUG(MOD_NET_PACKET, "Packet contents: {}", packet.toString());

// GOOD - only compute if logging is enabled
if (ShouldLog(MOD_NET_PACKET, LOG_DEBUG)) {
    LOG_DEBUG(MOD_NET_PACKET, "Packet contents: {}", packet.toString());
}

// ALSO GOOD - use lazy evaluation macro
LOG_DEBUG_LAZY(MOD_NET_PACKET, "Packet contents: ", [&]{ return packet.toString(); });
```

### Target-Specific Entity Logging

For debugging a specific entity, use `LogTargetEntity` which always prints when the spawn ID matches the tracked target (set via `SetTrackedTargetId()`):

```cpp
LogTargetEntity(spawn_id, "Entity moved to ({:.1f}, {:.1f}, {:.1f})", x, y, z);
```

Output format: `[TIMESTAMP] [DEBUG] [ENTITY:<spawn_id>] message`

## Legacy Macros

The following legacy macros exist for backward compatibility. They route through the standard logging system but use default modules. **Migrate to `LOG_*` macros when touching existing code.**

| Legacy Macro | Equivalent |
|---|---|
| `LogError(...)` | `LOG_ERROR(MOD_MAIN, ...)` |
| `LogWarning(...)` | `LOG_WARN(MOD_MAIN, ...)` |
| `LogInfo(...)` | `LOG_INFO(MOD_MAIN, ...)` |
| `LogDebug(...)` | `LOG_DEBUG(MOD_MAIN, ...)` |
| `LogTrace(...)` | `LOG_TRACE(MOD_MAIN, ...)` |
| `LogNetClient(...)` | `LOG_DEBUG(MOD_NET, ...)` |
| `LogNetClientDetail(...)` | `LOG_TRACE(MOD_NET, ...)` |
| `LogPacket(...)` | `LOG_TRACE(MOD_NET_PACKET, ...)` |
| `LogPathfinding(...)` | `LOG_DEBUG(MOD_PATHFIND, ...)` |
| `LogCombat(...)` | `LOG_DEBUG(MOD_COMBAT, ...)` |
| `LogChat(...)` | `LOG_DEBUG(MOD_MAIN, ...)` |

## Best Practices

### DO

- Log at appropriate levels - most messages should be DEBUG or TRACE
- Include context in messages: IDs, names, values that aid debugging
- Log state transitions and decision points
- Log all errors with enough context to diagnose
- Use structured data when logging complex objects
- Keep messages concise but informative

### DON'T

- Don't log sensitive data (passwords, keys, personal info)
- Don't log in tight loops at DEBUG or higher (use TRACE, and consider sampling)
- Don't use logging for normal control flow
- Don't log the same information at multiple levels
- Don't log without a module - always categorize
- Don't embed newlines in single log messages (exception: multi-line data dumps at TRACE)

### Message Style

```cpp
// GOOD - concise, includes relevant data
LOG_INFO(MOD_ZONE, "Entering zone {} (id={})", zone_name, zone_id);
LOG_ERROR(MOD_NET, "Connection failed to {}:{} - {}", host, port, error_msg);
LOG_DEBUG(MOD_ENTITY, "Entity {} moved from ({:.1f},{:.1f}) to ({:.1f},{:.1f})",
          entity_id, old_x, old_y, new_x, new_y);

// BAD - verbose, missing context, or redundant
LOG_INFO(MOD_ZONE, "The player is now entering a new zone...");  // Too verbose
LOG_ERROR(MOD_NET, "Error occurred");  // No context
LOG_DEBUG(MOD_ENTITY, "Entity moved");  // Missing all relevant data
```

### Performance Considerations

1. TRACE and DEBUG levels compile to no-ops in release builds (`!EQT_DEBUG`)
2. Log macros short-circuit before evaluating format arguments via `ShouldLog()`
3. Avoid heap allocations in hot-path logging
4. Use string_view for string parameters where possible
5. Buffer output and flush periodically, not on every line (except ERROR/FATAL)

## Migration Notes

Existing logging code does not all follow these standards. When modifying existing code:

1. Convert touched logging statements to `LOG_*` macros with appropriate `MOD_*` module
2. Adjust levels based on the guidelines above
3. Do not undertake bulk conversion without explicit request

## Summary Checklist

When adding logging to new or modified code:

- [ ] Used `LOG_*` macro (not legacy `LogDebug`/`LogInfo`/etc.)
- [ ] Used appropriate log level
- [ ] Assigned correct `MOD_*` module
- [ ] Included relevant context (IDs, names, values)
- [ ] No sensitive data logged
- [ ] Expensive computations wrapped in `ShouldLog()` check
- [ ] Format string uses fmt syntax
- [ ] Message is concise and actionable
