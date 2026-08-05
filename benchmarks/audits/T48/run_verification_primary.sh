#!/bin/bash
# T48 primary verification driver: real vendor NPC, real /vendor + /vendorlist
# + /buy + /sell commands, driven ENTIRELY BY PIPED STDIN (no keyboard, no
# renderer mouse click) -- must show the full command-channel route to
# VendorBuyIntent/VendorSellIntent (and VendorToggleIntent, added alongside
# them once scoping showed the mandated "open a vendor" step needed it too)
# reached live for the first time. No scratch hooks -- BuyFromVendor()/
# SellToVendor() are reached only through the SAME intent path the renderer
# gesture uses.
#
#   /vendor
#     -> pushes VendorToggleIntent{}                (T48, this unit)
#     -> ProcessBridgeIntents() (eq_bridge.cpp:866) -> RequestOpenVendor(npcId)
#   /vendorlist
#     -> reads m_vendor_items directly (T48, this unit; no intent involved --
#        it's a read, not an action)
#   /buy <slot> [qty]
#     -> pushes VendorBuyIntent{npcId=GetVendorNpcId(), slot, qty} (T48)
#     -> ProcessBridgeIntents() (eq_bridge.cpp:1069) -> BuyFromVendor(...)
#   /sell <slot> [qty]
#     -> pushes VendorSellIntent{...} (T48)
#     -> ProcessBridgeIntents() (eq_bridge.cpp:1072) -> SellToVendor(...)
#
# Same fifo-driver shape as T33/T34 (benchmarks/audits/T34/run_verification_
# primary.sh), adapted per T48's plan.
#
# Vendor NPC: id 771 "SummonResupplyAgent" (merchant_id=1000001, class=41=
# CLASS_MERCHANT, faction_required=-100 on every merchantlist row -- a
# GM-summonable, always-sells resupply vendor with ~20 stocked slots;
# confirmed live against the DB before this run). Spawned at the GM's own
# position via #npctypespawn, same technique T32/T33/T34 used for their mob.
set -u

RUNDIR="/mnt/projects/fable/wt/t1/benchmarks/audits/T48"
# T48 plan: run in BOTH trees, since headless intent processing (T45) is
# newly enabled -- BIN/LOG_SUFFIX are overridable so the same script drives
# both the EQT_GRAPHICS=ON tree's binary (default, still run with
# --no-graphics like T33/T34) and the EQT_GRAPHICS=OFF ("nogfx") tree's.
BIN="${WILLEQ_BIN:-/mnt/projects/fable/wt/t1/build/bin/willeq}"
LOG_SUFFIX="${LOG_SUFFIX:-primary}"
CFG="${RUNDIR}/willeq_t48.json"
LOG="${RUNDIR}/transcript_${LOG_SUFFIX}.txt"
FIFO="${RUNDIR}/cmds_${LOG_SUFFIX}.fifo"

rm -f "${FIFO}" "${LOG}" "${LOG}.driver"
mkfifo "${FIFO}"

cd /mnt/projects/fable/wt/t1

echo "== starting willeq (headless, piped stdin, -d 5/DEBUG) ==" | tee -a "${LOG}.driver"
"${BIN}" -c "${CFG}" --no-graphics -d 5 < "${FIFO}" > "${LOG}" 2>&1 &
WPID=$!
echo "willeq pid=${WPID}" | tee -a "${LOG}.driver"

exec 3<>"${FIFO}"

send() {
    echo "$1" >&3
    echo "[driver] sent: $1 ($(date +%s.%N))" >> "${LOG}.driver"
}

wait_for_log() {
    local pattern="$1" max="$2" waited=0
    while [ "${waited}" -lt "${max}" ]; do
        if grep -a -qE "${pattern}" "${LOG}" 2>/dev/null; then
            echo "[driver] matched '${pattern}' after ${waited}s" >> "${LOG}.driver"
            return 0
        fi
        if ! kill -0 "${WPID}" 2>/dev/null; then
            echo "[driver] process exited while waiting for '${pattern}'" >> "${LOG}.driver"
            return 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    echo "[driver] TIMEOUT waiting for '${pattern}' after ${max}s" >> "${LOG}.driver"
    return 1
}

# 1. Wait for full zone-in.
wait_for_log "Zone connection complete! Player position confirmed" 60

# 2. Baseline liveness probe.
send "/who"
sleep 2

# 2b. Testchar starts at 0pp/0g/0s/0c (confirmed live against the DB before
#     this run) -- not enough to buy anything from the vendor below, and the
#     server silently drops an unaffordable HC_OP_ShopPlayerBuy request (no
#     error packet at all -- confirmed by this script's own first, money-less
#     run: the /buy send reached BuyFromVendor() and sent the packet fine,
#     but no HC_OP_ShopPlayerBuy response, "You purchased", or inventory
#     change ever arrived). #givemoney is a real GM command (command_add in
#     command.cpp), not a scratch hook -- same class of action as
#     #npctypespawn below. It targets the caster when the current target
#     isn't a player character (givemoney.cpp:29-32), so this is safe to
#     send even before/after /target-ing the merchant NPC.
send "/say #givemoney 500 0 0 0"
wait_for_log "Money updated:" 15
sleep 1

# 3. Spawn the vendor NPC at the GM's own position (distance=0, no
#    pathfinding/targeting-range concerns).
send "/say #npctypespawn 771"
wait_for_log "Spawned SummonResupplyAgent|Spawned .*771" 15
sleep 1

send "/list"
sleep 2

# befallen accumulates persistent spawns across runs (same T33/T34 finding),
# so resolve the FRESH spawn's exact name from the transcript rather than
# guessing.
FRESH_NAME=$(grep -a -oE "New spawn: SummonResupplyAgent[0-9]+" "${LOG}" | tail -n1 | sed -E 's/New spawn: //')
echo "[driver] fresh spawn name resolved to '${FRESH_NAME}'" >> "${LOG}.driver"
if [ -z "${FRESH_NAME}" ]; then
    echo "[driver] FATAL: could not resolve fresh spawn name from transcript" >> "${LOG}.driver"
fi

# 4. Target the vendor, then open its window -- command-channel only (/target,
#    then T48's own /vendor -- no mouse, no renderer).
send "/target ${FRESH_NAME}"
sleep 1
send "/vendor"
wait_for_log "Opened vendor window for" 15
sleep 1

# 5. List its inventory -- reads m_vendor_items, command-channel only.
send "/vendorlist"
sleep 2

# Resolve a real vendor slot + item name/price from the transcript's own
# /vendorlist output rather than assuming the DB's slot numbering.
VLIST_LINE=$(grep -a -oE '\[slot [0-9]+\] [^-]+- [^(]+\(qty: [^)]+\)' "${LOG}" | head -n1)
echo "[driver] first vendorlist line: ${VLIST_LINE}" >> "${LOG}.driver"
BUY_SLOT=$(echo "${VLIST_LINE}" | grep -oE '\[slot [0-9]+\]' | grep -oE '[0-9]+')
echo "[driver] resolved buy slot: ${BUY_SLOT}" >> "${LOG}.driver"
if [ -z "${BUY_SLOT}" ]; then
    echo "[driver] FATAL: could not resolve a vendor slot from /vendorlist output" >> "${LOG}.driver"
fi

# 6. THE BUY: command-channel /buy <slot>, pushing VendorBuyIntent -- the
#    headline event of this unit.
send "/buy ${BUY_SLOT}"
wait_for_log "You purchased" 15
sleep 1

# Resolve the inventory slot the purchase landed in, from the generic
# ItemPacket fallback publish line (eq.cpp, HC_OP_ItemPacket handler) --
# same technique T34 used to get an authoritative id from the transcript
# instead of guessing.
BUY_INV_LINE=$(grep -a -E "Publishing InventorySlotChanged \(generic ItemPacket fallback\)" "${LOG}" | tail -n1)
echo "[driver] buy inventory-slot-changed line: ${BUY_INV_LINE}" >> "${LOG}.driver"
BUY_INV_SLOT=$(echo "${BUY_INV_LINE}" | grep -oE 'slot=[0-9]+' | head -n1 | grep -oE '[0-9]+')
echo "[driver] resolved purchased-item inventory slot: ${BUY_INV_SLOT}" >> "${LOG}.driver"

# 7. Confirm the item is really there before selling it back (an empty-slot
#    sell would prove nothing -- same discipline T34 used for looting an
#    empty corpse).
send "/vendorlist"
sleep 1

# 8. THE SELL: command-channel /sell <inventory slot>, pushing
#    VendorSellIntent -- sells the just-purchased item back to the same
#    vendor.
if [ -n "${BUY_INV_SLOT}" ]; then
    send "/sell ${BUY_INV_SLOT}"
    wait_for_log "You sold" 15
    sleep 1
else
    echo "[driver] FATAL: no resolved inventory slot -- skipping /sell" >> "${LOG}.driver"
fi

# 9. Final /vendorlist + /who as a liveness/consistency bookend.
send "/vendorlist"
send "/who"
sleep 2

# 10. Close the vendor window cleanly via the command channel too.
send "/vendor"
wait_for_log "Closing vendor window for NPC" 15
sleep 1

# 11. Clean shutdown. /camp has a real 30s in-game timer (T33/T34 finding).
send "/camp"
wait_for_log "shutdown complete|Application shutdown" 40

waited=0
while kill -0 "${WPID}" 2>/dev/null && [ "${waited}" -lt 20 ]; do
    sleep 1
    waited=$((waited + 1))
done
if kill -0 "${WPID}" 2>/dev/null; then
    echo "[driver] process still alive after camp+20s -- sending /quit and then SIGTERM" >> "${LOG}.driver"
    send "/quit"
    sleep 3
    kill "${WPID}" 2>/dev/null || true
    sleep 2
fi

exec 3>&-
wait "${WPID}" 2>/dev/null
EXITCODE=$?
echo "[driver] willeq exit code: ${EXITCODE}" | tee -a "${LOG}.driver"
echo "DONE exit=${EXITCODE}"
