#!/usr/bin/perl
# B940 (#258): the B940 match leg -- run the provisioned watcher's OWN
# decision (the same env-read, the same qr::, the same default) against the
# real argv shapes the lane deals with, and fail if the exclusion is wrong
# in either direction:
#   * every zone argv shape MUST be exempt (the R1 kill was a bin/zone line);
#   * no non-zone shape (loginserver/world/ucs/launcher/spire/sh) may
#     match -- the exclusion exempts exactly the zone binaries, nothing else.
#
# usage: PROCESS_WATCHER_EXCLUDE=<compose value> perl b940_watcher_match_test.pl
# Exit 0 = all PASS; 1 = at least one FAIL.
use strict;
use warnings FATAL => 'all';

# The provisioned watcher's decision, copied verbatim from
# b940_patch_watcher.pl's declaration (same env name, same qr//, same
# default). If the two ever drift, this leg and the container disagree.
my $re = defined $ENV{PROCESS_WATCHER_EXCLUDE} && $ENV{PROCESS_WATCHER_EXCLUDE} ne ''
    ? qr/$ENV{PROCESS_WATCHER_EXCLUDE}/
    : qr/gcc|mysql|wget|unzip|node|go-build|tar|world|JetBrains|vscode|remote-dev-server|gzip/i;

# Full ps lines as the watcher sees them (`pcpu pid user args`), from the
# real fleet (refactor/B925/README.md's kill log, server_process_count's
# grep shapes). The provisioned value is a bare body with no case flag:
# the zone binary's argv is what it is, and the /i belongs only to the
# upstream default regex, not to the lane's exclusion.
my @zone_shapes = (
    "36.5 1317 eqemu ./bin/zone iceclad",
    "37.0 1841 eqemu bin/zone overthere",
    "38.9 2536 eqemu /bin/zone",
    "40.0 9999 eqemu /home/eqemu/server/bin/zone qeynos2",
);
my @non_zone = (
    "12.0 4001 eqemu ./bin/loginserver",
    "18.0 4002 eqemu ./bin/world",
    "9.0  4003 eqemu ./bin/ucs",
    "25.0 4004 eqemu eqemu-server:launcher start",
    "31.0 4005 eqemu ./bin/spire http:serve --port=3000",
    "45.0 4006 eqemu /bin/sh",
);

my $ok = 1;
foreach my $z (@zone_shapes) {
    if ($z =~ $re) {
        print "PASS  zone line exempted: $z\n";
    }
    else {
        print "FAIL  zone line NOT exempted: $z\n";
        $ok = 0;
    }
}
foreach my $n (@non_zone) {
    if ($n =~ $re) {
        print "FAIL  non-zone line matches the exclusion: $n\n";
        $ok = 0;
    }
    else {
        print "PASS  non-zone stays under the threshold rule: $n\n";
    }
}
exit($ok ? 0 : 1);
