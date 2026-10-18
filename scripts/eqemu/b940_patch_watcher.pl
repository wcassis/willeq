#!/usr/bin/perl
# B940 (#258): make process-watcher.pl's exclusion pattern env-driven.
#
# The eqemu-server image's watcher (assets/scripts/process-watcher.pl,
# bind-mounted into the container) kill -9s any process whose ps line is
# not in an allowlist and whose lifetime-average CPU exceeds 30 % after
# 60 s (refactor/B925/README.md). The allowlist regex omits zone, so a
# freshly-respawned zone, busy loading a client, reads >30 % in its first
# minute and is killed (iceclad ~65-75 s x86, overthere ~38 s on the Pi).
#
# This edit keeps the original regex as the DEFAULT (a stack without the
# env var behaves exactly as before) and lets the lane's compose file
# override it via PROCESS_WATCHER_EXCLUDE, which the watcher compiles with
# qr// so the value is a genuine regex. The lanes supply
# (bin|/bin)/zone -- a bare body, no delimiters, no backslashes (both are
# eaten by the compose env -> shell chain; watcher_provision.sh's match
# leg proves the surviving value on every run) -- the exact bin/zone argv
# shapes the tree's own server_process_count already greps for.
#
# The two edits are plain LITERAL string splices (no s///): the inserted
# text itself contains regexes with / and |, which would break a
# replacement-side s///. Both splices refuse when their needle is absent
# or duplicated, and perl -c runs on the patched text in a sibling file
# BEFORE it replaces the original.
#
# Idempotent: refuses when the B940 marker is already present.
#
# usage: b940_patch_watcher.pl <process-watcher.pl>
use strict;
use warnings FATAL => 'all';

my $path = shift or die "usage: b940_patch_watcher.pl <process-watcher.pl>\n";
open my $fh, '<', $path or die "cannot read $path: $!\n";
my $src = do { local $/; <$fh> };
close $fh;

if ($src =~ /B940/) {
    print "watcher: already provisioned (B940 marker present)\n";
    exit 0;
}

# The declaration inserted right after the threshold line: the env-driven
# pattern (upstream regex verbatim as the fallback) plus a startup marker
# line -- the marker is the verification anchor provision scripts grep.
my $anchor = "my \$cpu_threshold       = 30;";
my $declare = <<'DECL';
# B940 (#258): the exclusion pattern is env-driven. The default is the
# upstream allowlist verbatim, so a stack without PROCESS_WATCHER_EXCLUDE
# behaves exactly as before; the eqemu lanes set it via docker-compose.yml
# (the value is compiled with qr// below) so a zone busy loading a client
# is not killed by the 30%/60s rule.
my $process_re = defined $ENV{PROCESS_WATCHER_EXCLUDE} && $ENV{PROCESS_WATCHER_EXCLUDE} ne ''
    ? qr/$ENV{PROCESS_WATCHER_EXCLUDE}/
    : qr/gcc|mysql|wget|unzip|node|go-build|tar|world|JetBrains|vscode|remote-dev-server|gzip/i;
print "[process-watcher] exclusion pattern: ", ($ENV{PROCESS_WATCHER_EXCLUDE} // '(upstream default)'), "\n";

DECL

my $needle = <<'OLD';
        if ($process =~ /gcc|mysql|wget|unzip|node|go-build|tar|world|JetBrains|vscode|remote-dev-server|gzip/i) {
            next;
        }
OLD

my $repl = <<'NEW';
        # B940 (#258): env-driven exclusion; the default is the upstream
        # allowlist, so an unset PROCESS_WATCHER_EXCLUDE is a no-op.
        if ($process =~ $process_re) {
            next;
        }
NEW

sub splice_once {  # splice_once <src-ref> <needle> <replacement> <label>
    my ($src, $n, $r, $label) = @_;
    my $i = index($$src, $n);
    die "watcher: the $label is not in the file -- refused\n" if $i < 0;
    die "watcher: the $label occurs more than once -- refused\n"
        if index($$src, $n, $i + 1) >= 0;
    substr($$src, $i, length($n)) = $r;
}

splice_once(\$src, $anchor, "${anchor}\n" . $declare, "threshold anchor line");
splice_once(\$src, $needle, $repl, "allowlist block");

# Syntax-check the NEW text before touching the file.
(my $sib = $path) =~ s/\.pl$/.b940pl/;
open my $out, '>', $sib or die "cannot write $sib: $!\n";
print $out $src;
close $out;
my $rc = system('perl', '-c', $sib);
if ($rc != 0) {
    unlink $sib;
    die "watcher: perl -c FAILED on the patched text (rc=${rc}) -- file left untouched\n";
}
open $out, '>', $path or die "cannot write $path: $!\n";
print $out $src;
close $out;
unlink $sib;
print "watcher: provisioned (exclusion is now PROCESS_WATCHER_EXCLUDE-driven, default = upstream)\n";
