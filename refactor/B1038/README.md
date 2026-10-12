# B1038 (#358): the headless build (EQT_GRAPHICS=OFF) compiles again on Linux

Source: B1031 (#351), the macOS probe's Linux cross-check
(`review/B1031/linux_crosscheck_pass2.tsv`). Evidence for this unit:
`review/B1038/` (outside the checkout).

## RED (fable `b47fd97d`, dev-u24, GCC)

    cmake -S . -B build-headless -DCMAKE_BUILD_TYPE=Release -DEQT_GRAPHICS=OFF -DEQT_BUILD_TESTS=ON
    cmake --build build-headless -j4 -- -k

(The generator is Unix Makefiles here, so `-k`, not ninja's `-k 0`.)
`review/B1038/red_configure.log` (exit 0), `review/B1038/red_build.log`:
**BUILD_EXIT=2** after 1,470 s, `grep -c "error:"` = **1**:
`src/client/application.cpp:1087:21: error: 'm_renderer' was not declared in this scope`.

`make -k` does not cross a failed target, so every target linking
`willeq_core` was never attempted. To get the complete census, the 124 object
files the build left unbuilt (`review/B1038/red_unbuilt_objects.txt`, every
`*.cpp.o` rule in the tree's `build.make` files with no object on disk) were
compiled one at a time with B1031's `linux_crosscheck.sh`
(`review/B1038/red_crosscheck.tsv`). **Exactly 5 fail, 119 compile:**

| Object | errors | first error |
| --- | --- | --- |
| `willeq_core` `src/client/application.cpp` | 1 | `m_renderer` not declared (line 1087) |
| `test_data_paths` `tests/test_data_paths.cpp` | 1 | `hotkey_manager.h:3: irrlicht.h: No such file` |
| `test_detail_view_distance` `tests/test_detail_view_distance.cpp` | 1 | `detail_types.h:4: irrlicht.h: No such file` |
| `test_ranged_attack_wire` `tests/test_ranged_attack_wire.cpp` | 1 | `hotkey_manager.h:3: irrlicht.h: No such file` |
| `test_zone_connection_flow` `tests/test_zone_connection_flow_identity.cpp` | 9 | `BridgeComponent` has no member `CreateZoneLoadSnapshot` (line 353) |

These are the issue's 5 TUs; there are no others.

**cereal.** `grep -rn "cereal/" include src`:

    include/common/net/daybreak_structs.h:3:#include <cereal/cereal.hpp>
    include/common/net/packet.h:10:#include <cereal/cereal.hpp>
    include/common/net/packet.h:11:#include <cereal/archives/binary.hpp>

`grep -n -i cereal CMakeLists.txt` is empty (there is no `cmake/*.cmake`; only
`cmake/toolchains/`). dev-u24 has it from `libcereal-dev`
(`dpkg -S /usr/include/cereal/cereal.hpp`); the ARM image copies v1.3.2 into
its sysroot's `/usr/include` (`docker/Dockerfile.arm-noble`, next to GLM).
